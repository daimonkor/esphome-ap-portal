#include "ap_portal.h"
#include "landing_page.h"

#include <cerrno>
#include <cstring>

#include <esp_http_server.h>
#include <driver/gpio.h>
#include <esp_netif.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <lwip/sockets.h>
#include "dhcpserver/dhcpserver.h"

#include "esphome/components/captive_portal/captive_portal.h"
#include "esphome/components/wifi/scan_list.h"
#include "esphome/components/wifi/wifi_component.h"
#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome::ap_portal {

static const char *const TAG = "ap_portal";

// Stage-1 measurements (2026-11-18) behind these numbers: see ap_portal_core.h.
static constexpr uint32_t AP_RAISE_DELAY_MS = 500;      // let ESPHome finish what switched the AP off
static constexpr uint32_t QUIET_END_ROUTER_MS = 3000;   // router back and stable
static constexpr uint8_t MAX_CLIENTS = 4;
static constexpr uint8_t DHCP_FIRST = 100, DHCP_LAST = 110;

static const char *mode_name(ap_portal_core::Mode m) {
  switch (m) {
    case ap_portal_core::Mode::REDIRECT:
      return "redirect";
    case ap_portal_core::Mode::PAGE:
      return "page";
    case ap_portal_core::Mode::GATE:
      return "gate";
  }
  return "?";
}

static std::string mac_str(const uint8_t *m) {
  char b[18];
  snprintf(b, sizeof(b), "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
  return b;
}

static esp_netif_t *ap_netif() { return esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"); }

static std::string ip_str(uint32_t ip_be) {
  char b[16];
  const uint8_t *p = reinterpret_cast<const uint8_t *>(&ip_be);
  snprintf(b, sizeof(b), "%u.%u.%u.%u", p[0], p[1], p[2], p[3]);
  return b;
}

// IPv4 of a socket address; IPv4-mapped IPv6 too (lwIP with IPv6 gives those).
static uint32_t v4_of(const sockaddr_storage &a) {
  if (a.ss_family == AF_INET)
    return reinterpret_cast<const sockaddr_in *>(&a)->sin_addr.s_addr;
#if LWIP_IPV6
  if (a.ss_family == AF_INET6) {
    const uint8_t *b = reinterpret_cast<const sockaddr_in6 *>(&a)->sin6_addr.s6_addr;
    static const uint8_t PREFIX[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff};
    if (std::memcmp(b, PREFIX, 12) == 0) {
      uint32_t ip;
      std::memcpy(&ip, b + 12, 4);
      return ip;
    }
  }
#endif
  return 0;
}

// web_server_base keeps its handler list protected and the class is final. Forwarding a request
// past captive_portal needs that list, so it is read through a member pointer obtained by explicit
// template instantiation (allowed to name private members). Read-only, web server task only.
namespace {
struct HandlersTag {
  using type = std::vector<AsyncWebHandler *> web_server_base::WebServerBase::*;
};
HandlersTag::type handlers_member(HandlersTag);
template<typename Tag, typename Tag::type M> struct HandlersRobber {
  friend typename Tag::type handlers_member(Tag) { return M; }
};
template struct HandlersRobber<HandlersTag, &web_server_base::WebServerBase::handlers_>;
}  // namespace

static constexpr uint32_t PASSWORD_APPLY_DELAY_MS = 2000;  // the HTTP answer leaves first
static constexpr uint32_t SCAN_TIMEOUT_MS = 20000;
static constexpr uint32_t SCAN_RETRY_MS = 2000;
static constexpr uint32_t LAMBDA_REFRESH_MS = 1000;
static constexpr uint32_t PASSWORD_MAGIC = 0xA9F0C0DE;
struct PasswordRecord {
  uint32_t magic;
  uint8_t len;
  char pw[64];
};

// ───────────────────────────── setup / loop ─────────────────────────────

void ApPortal::setup() {
  this->boot_ms_ = millis();
  this->home_api_.parent = this;
  if (this->reset_pin_ != nullptr)
    this->reset_pin_->setup();
  auto *w = wifi::global_wifi_component;
  if (w == nullptr) {
    this->mark_failed();
    return;
  }
  // ESPHome's own fallback access point would reconfigure ours and drop clients; we keep the
  // access point ourselves, so it never starts.
  w->set_ap_timeout(0);

  // Before Wi-Fi starts (setup priority WIFI + 10): everyone who reads get_ap() — displays,
  // QR codes, the device page — sees the real SSID.
  if (this->ssid_suffix_mac_) {
    auto ap = w->get_ap();
    uint8_t mac[6];
    get_mac_address_raw(mac);
    const std::string ssid = ap_portal_core::ssid_with_suffix(std::string(ap.get_ssid()), mac);
    ap.set_ssid(ssid);
    w->set_ap(ap);
  }
  this->events_ = xQueueCreate(16, sizeof(Event));

  // Password kept by ap_portal.set_password, applied before Wi-Fi starts.
  {
    auto ap = w->get_ap();
    this->firmware_password_ = std::string(ap.get_password().c_str(), ap.get_password().size());
    if (this->password_persist_) {
      this->password_pref_ = global_preferences->make_preference<PasswordRecord>(fnv1_hash("ap_portal_password"));
      this->power_cycle_boot_();  // may erase the saved password before it is read
      PasswordRecord rec{};
      if (this->password_pref_.load(&rec) && rec.magic == PASSWORD_MAGIC && rec.len <= 63) {
        const std::string pw(rec.pw, rec.len);
        if (ap_portal_core::valid_ap_password(pw)) {
          ap.set_password(pw);
          w->set_ap(ap);
          this->custom_password_ = pw != this->firmware_password_;
          ESP_LOGI(TAG, "Access point password from flash (%s)", pw.empty() ? "open" : "WPA2");
        }
      }
    }
  }

  // Fixed for the web server task.
  this->ssid_snap_ = std::string(w->get_ap().get_ssid().c_str(), w->get_ap().get_ssid().size());
  this->host_ = App.get_name();
  this->title_ = std::string(App.get_friendly_name().c_str(), App.get_friendly_name().size());
  if (this->title_.empty())
    this->title_ = this->host_;

  // Before web_server (setup priority 249): our handler is asked first.
  if (web_server_base::global_web_server_base != nullptr) {
    web_server_base::global_web_server_base->add_handler_without_auth(this);
    // The API from the home network for the device's own site: behind web_server's login, if set.
    if (this->api_from_home_)
      web_server_base::global_web_server_base->add_handler(&this->home_api_);
  } else {
    ESP_LOGE(TAG, "web_server_base missing: no landing page");
  }
}

void ApPortal::loop() {
  auto *w = wifi::global_wifi_component;
  if (w == nullptr)
    return;
  const uint32_t now = millis();
  // The default event loop exists only after `network` set up; registering earlier fails silently.
  if (!this->events_registered_)
    this->register_events_();
  this->drain_events_();

  if (this->password_pending_ && ap_portal_core::reached(now, this->password_apply_at_)) {
    this->password_pending_ = false;
    this->apply_password_(this->password_pending_value_, this->password_pending_save_);
    this->password_pending_value_.clear();
  }
  if (this->scanning_ && ap_portal_core::elapsed(now, this->scan_started_ms_, SCAN_TIMEOUT_MS)) {
    ESP_LOGW(TAG, "scan: no result in %u s", (unsigned) (SCAN_TIMEOUT_MS / 1000));
    this->scanning_ = false;
    if (this->scan_suppressing_) {
      w->release_roaming_suppression();
      this->scan_suppressing_ = false;
    }
  }
  this->reset_tick_(now);
  this->http_requests_tick_(now);
  if (this->scan_retry_at_ != 0 && ap_portal_core::reached(now, this->scan_retry_at_)) {
    this->scan_retry_at_ = 0;
    this->scan_start_();
  }

  const bool want = this->want_active_();
  if (want != this->running_) {
    this->running_ = want;
    if (want) {
      ESP_LOGI(TAG, "Portal on");
      this->on_portal_start_.call();
    } else {
      ESP_LOGI(TAG, "Portal off");
      this->stop_ap_();
      this->on_portal_stop_.call();
    }
  }
  if (!this->running_)
    return;
  this->refresh_lambda_vars_(now);

  const bool router = w->is_connected();
  if (router) {
    // One radio: connected, the access point serves on the router's channel. Remember it — after
    // the router is lost the radio wanders through ESPHome's scans and its current channel means
    // nothing. Ask the driver for the association itself, not for the radio channel: ESPHome may
    // still report "connected" for a moment after Wi-Fi was restarted, while the radio already sits
    // on channel 1 (2026-11-18: remembered 1 with the router on 9). Without an association this
    // call fails and the remembered channel stays.
    wifi_ap_record_t info{};
    if (esp_wifi_sta_get_ap_info(&info) == ESP_OK && info.primary != 0)
      this->ap_channel_ = info.primary;
  }
  this->keep_ap_(now);
  if (!this->ap_up_()) {
    this->dhcp_done_ = false;
    this->dns_close_();
    this->quiet_tick_(now, router);
    return;
  }
  if (!this->dhcp_done_)
    this->configure_dhcp_();
  if (this->dns_enabled_)
    this->dns_tick_();
  this->quiet_tick_(now, router);
}

void ApPortal::dump_config() {
  ESP_LOGCONFIG(TAG,
                "AP portal:\n"
                "  Active: %s\n"
                "  SSID: %s%s\n"
                "  Password persist: %s\n"
                "  DNS: %s\n"
                "  Landing mode: %s, path %s",
                this->active_lambda_ ? "lambda" : YESNO(this->active_), this->ssid().c_str(),
                this->ssid_suffix_mac_ ? " (MAC suffix)" : "", YESNO(this->password_persist_),
                YESNO(this->dns_enabled_), mode_name(this->route_.mode), this->route_.landing_path.c_str());
  if (this->route_.mode == ap_portal_core::Mode::GATE) {
    ESP_LOGCONFIG(TAG, "  Enter path: %s, window %u min, pass paths: %u", this->route_.enter_path.c_str(),
                  (unsigned) (this->passes_.window() / 60000), (unsigned) this->route_.pass_paths.size());
  }
  if (captive_portal::global_captive_portal == nullptr)
    ESP_LOGW(TAG, "  captive_portal missing: no quiet mode without a router");
}

void ApPortal::on_shutdown() {
  this->dns_close_();
  this->lock_release_();
  std::lock_guard<std::mutex> g(this->mu_);
  this->passes_.clear();
}

// ───────────────────────────── events ─────────────────────────────

void ApPortal::post_(const Event &ev) {
  if (this->events_ != nullptr)
    xQueueSend(this->events_, &ev, 0);  // full queue: drop, client_count() asks the driver anyway
}

void ApPortal::on_ip_event_(void *arg, esp_event_base_t, int32_t id, void *data) {
  if (id != IP_EVENT_AP_STAIPASSIGNED)
    return;
  auto *e = static_cast<ip_event_ap_staipassigned_t *>(data);
  Event ev{};
  ev.type = EV_IP;
  ev.ip = e->ip.addr;
  std::memcpy(ev.mac, e->mac, 6);
  static_cast<ApPortal *>(arg)->post_(ev);
}

void ApPortal::on_wifi_event_(void *arg, esp_event_base_t, int32_t id, void *data) {
  auto *self = static_cast<ApPortal *>(arg);
  Event ev{};
  switch (id) {
    case WIFI_EVENT_AP_STACONNECTED:
      ev.type = EV_JOIN;
      std::memcpy(ev.mac, static_cast<wifi_event_ap_staconnected_t *>(data)->mac, 6);
      break;
    case WIFI_EVENT_AP_STADISCONNECTED:
      ev.type = EV_LEAVE;
      std::memcpy(ev.mac, static_cast<wifi_event_ap_stadisconnected_t *>(data)->mac, 6);
      break;
    case WIFI_EVENT_AP_STOP:
      ev.type = EV_AP_STOP;
      self->ap_stop_ms_ = millis();
      break;
    case WIFI_EVENT_SCAN_DONE:
      ev.type = EV_SCAN_DONE;
      break;
    case WIFI_EVENT_STA_DISCONNECTED:
      self->sta_reason_ = static_cast<wifi_event_sta_disconnected_t *>(data)->reason;
      return;
    case WIFI_EVENT_STA_CONNECTED:
      self->sta_reason_ = 0;
      return;
    default:
      return;
  }
  self->post_(ev);
}

void ApPortal::register_events_() {
  esp_err_t e = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &ApPortal::on_wifi_event_, this,
                                                    nullptr);
  if (e == ESP_OK) {
    this->events_registered_ = true;
    esp_err_t e2 = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_AP_STAIPASSIGNED, &ApPortal::on_ip_event_,
                                                        this, nullptr);
    if (e2 != ESP_OK)
      ESP_LOGW(TAG, "IP event handler: %s (gate passes by IP only)", esp_err_to_name(e2));
  } else if (e != ESP_ERR_INVALID_STATE) {  // invalid state = event loop not there yet, retry next loop
    ESP_LOGE(TAG, "Wi-Fi event handler: %s", esp_err_to_name(e));
    this->events_registered_ = true;  // do not spam; client triggers stay silent
  }
}

void ApPortal::drain_events_() {
  if (this->events_ == nullptr)
    return;
  Event ev;
  while (xQueueReceive(this->events_, &ev, 0) == pdTRUE) {
    if (ev.type == EV_JOIN) {
      ESP_LOGD(TAG, "Client %s joined", mac_str(ev.mac).c_str());
      this->on_client_connected_.call(mac_str(ev.mac));
    } else if (ev.type == EV_LEAVE) {
      ESP_LOGD(TAG, "Client %s left", mac_str(ev.mac).c_str());
      ap_portal_core::Mac m;
      std::memcpy(m.b, ev.mac, 6);
      {
        std::lock_guard<std::mutex> g(this->mu_);
        this->passes_.revoke(m);
        this->clients_.remove(m);
      }
      this->on_client_disconnected_.call(mac_str(ev.mac));
    } else if (ev.type == EV_IP) {
      ap_portal_core::Mac m;
      std::memcpy(m.b, ev.mac, 6);
      std::lock_guard<std::mutex> g(this->mu_);
      this->clients_.set(ev.ip, m);
    } else if (ev.type == EV_SCAN_DONE) {
      if (this->scanning_)
        ESP_LOGD(TAG, "scan: done");
      this->scanning_ = false;
      if (this->scan_suppressing_) {
        wifi::global_wifi_component->release_roaming_suppression();
        this->scan_suppressing_ = false;
      }
    } else if (ev.type == EV_LANDING) {
      this->on_landing_.call(ip_str(ev.ip), std::string(ev.path));
    } else if (ev.type == EV_ENTER) {
      ESP_LOGD(TAG, "Gate: pass for %s (%s)", mac_str(ev.mac).c_str(), ip_str(ev.ip).c_str());
      this->on_enter_.call(mac_str(ev.mac), ip_str(ev.ip));
    } else if (ev.type == EV_AP_STOP) {
      if (this->running_)
        this->ap_stopped_by_esphome_ = true;
    }
  }
}

// ───────────────────────────── access point ─────────────────────────────

bool ApPortal::ap_up_() const {
  wifi_mode_t m;
  return esp_wifi_get_mode(&m) == ESP_OK && (m == WIFI_MODE_APSTA || m == WIFI_MODE_AP);
}

// ESPHome switches the access point off when it connects to the router (disable_ap_) and when
// Wi-Fi restarts. Raise it again from loop() at a quiet moment — never from the event handler,
// never while Wi-Fi is disabled or ESPHome is in a roaming scan.
void ApPortal::keep_ap_(uint32_t now) {
  auto *w = wifi::global_wifi_component;
  if (w->is_disabled())
    return;
  wifi_mode_t mode;
  if (esp_wifi_get_mode(&mode) != ESP_OK)
    return;  // driver not started yet
  if (mode == WIFI_MODE_APSTA || mode == WIFI_MODE_AP) {
    this->ap_was_up_ = true;
    return;
  }
  if (!ap_portal_core::elapsed(now, this->ap_stop_ms_.load(), AP_RAISE_DELAY_MS))
    return;
  if (w->is_roaming_scan_active())
    return;

  const auto ap = w->get_ap();
  const std::string ssid(ap.get_ssid());
  const std::string pass(ap.get_password());
  if (ssid.empty())
    return;
  wifi_config_t c{};
  const size_t sl = std::min<size_t>(ssid.size(), sizeof(c.ap.ssid));
  std::memcpy(c.ap.ssid, ssid.data(), sl);
  c.ap.ssid_len = sl;
  // The router's last channel (remembered in loop()), never the radio's current one: without the
  // router the radio may be mid-scan anywhere (2026-11-18: raised on channel 5 with the router on 9).
  uint8_t ch = this->ap_channel_;
  if (ch == 0)
    ch = ap.has_channel() ? ap.get_channel() : 1;
  c.ap.channel = ch;
  c.ap.max_connection = MAX_CLIENTS;
  c.ap.beacon_interval = 100;
  c.ap.ssid_hidden = ap.get_hidden() ? 1 : 0;
  if (pass.size() >= 8) {
    c.ap.authmode = WIFI_AUTH_WPA2_PSK;
    std::memcpy(c.ap.password, pass.data(), std::min<size_t>(pass.size(), sizeof(c.ap.password) - 1));
  } else {
    c.ap.authmode = WIFI_AUTH_OPEN;
  }
  const esp_err_t e1 = esp_wifi_set_mode(WIFI_MODE_APSTA);
  const esp_err_t e2 = esp_wifi_set_config(WIFI_IF_AP, &c);
  this->ap_raises_++;
  this->dhcp_done_ = false;
  if (e1 != ESP_OK || e2 != ESP_OK) {
    ESP_LOGW(TAG, "Access point raise failed: mode %s, config %s", esp_err_to_name(e1), esp_err_to_name(e2));
    return;
  }
  ESP_LOGI(TAG, "Access point '%s' up on channel %u", ssid.c_str(), (unsigned) ch);
  // "Restored" only when we had raised it before: the first raise after boot (ESPHome switches the
  // access point off on its first connect) is not a restore.
  if (this->ap_raises_ > 1 && this->ap_was_up_ && this->ap_stopped_by_esphome_)
    this->on_ap_restored_.call();
  this->ap_stopped_by_esphome_ = false;
  this->ap_was_up_ = true;
}

void ApPortal::stop_ap_() {
  this->dns_close_();
  auto *cp = captive_portal::global_captive_portal;
  if (this->quiet_ours_ && cp != nullptr && cp->is_active())
    cp->end();
  this->quiet_ours_ = false;
  this->lock_release_();
  {
    std::lock_guard<std::mutex> g(this->mu_);
    this->passes_.clear();
    this->clients_.clear();
  }
  wifi_mode_t m;
  if (esp_wifi_get_mode(&m) == ESP_OK && m == WIFI_MODE_APSTA)
    esp_wifi_set_mode(WIFI_MODE_STA);
  this->ap_was_up_ = false;
  this->dhcp_done_ = false;
}

// Address and DHCP of an access point we raised ourselves: ESPHome applies manual_ip only to the
// fallback access point it starts itself, ours would otherwise get 192.168.4.1.
void ApPortal::configure_dhcp_() {
  esp_netif_t *apn = ap_netif();
  if (apn == nullptr)
    return;
  this->dhcp_done_ = true;
  const auto ap = wifi::global_wifi_component->get_ap();
  esp_netif_ip_info_t info{};
  if (ap.get_manual_ip().has_value()) {
    const auto &mi = ap.get_manual_ip().value();
    info.ip = (esp_ip4_addr_t) mi.static_ip;
    info.gw = (esp_ip4_addr_t) mi.gateway;
    info.netmask = (esp_ip4_addr_t) mi.subnet;
  } else {
    esp_netif_get_ip_info(apn, &info);  // ESP-IDF default, 192.168.4.1
  }
  esp_netif_dhcps_stop(apn);
  esp_err_t e = esp_netif_set_ip_info(apn, &info);
  const uint32_t net = info.ip.addr & info.netmask.addr;  // network byte order
  dhcps_lease_t lease{};
  lease.enable = true;
  lease.start_ip.addr = net | (static_cast<uint32_t>(DHCP_FIRST) << 24);
  lease.end_ip.addr = net | (static_cast<uint32_t>(DHCP_LAST) << 24);
  esp_netif_dhcps_option(apn, ESP_NETIF_OP_SET, ESP_NETIF_REQUESTED_IP_ADDRESS, &lease, sizeof(lease));
  uint8_t offer = 0x03;  // OFFER_ROUTER | OFFER_DNS
  esp_netif_dhcps_option(apn, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &offer, sizeof(offer));
  esp_netif_dns_info_t dns{};
  dns.ip.type = ESP_IPADDR_TYPE_V4;
  dns.ip.u_addr.ip4.addr = info.ip.addr;
  esp_netif_set_dns_info(apn, ESP_NETIF_DNS_MAIN, &dns);
  esp_netif_dhcps_start(apn);
  ESP_LOGD(TAG, "Access point address " IPSTR ", DHCP .%u-.%u, DNS = access point (%s)", IP2STR(&info.ip),
           DHCP_FIRST, DHCP_LAST, esp_err_to_name(e));
}

uint32_t ApPortal::ap_ip_be_() const {
  esp_netif_t *apn = ap_netif();
  esp_netif_ip_info_t info{};
  if (apn == nullptr || esp_netif_get_ip_info(apn, &info) != ESP_OK)
    return 0;
  return info.ip.addr;
}

// ───────────────────────────── dns ─────────────────────────────
// Bound to the access point's address with SO_REUSEADDR: lwIP delivers to the most specific
// binding first, so captive_portal's own DNS (bound to any address) never sees our clients.
void ApPortal::dns_tick_() {
  const uint32_t ip = this->ap_ip_be_();
  if (ip == 0)
    return;
  if (this->dns_sock_ >= 0 && this->dns_bound_ip_ != ip)
    this->dns_close_();
  if (this->dns_sock_ < 0) {
    int s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s < 0)
      return;
    int one = 1;
    ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(53);
    a.sin_addr.s_addr = ip;
    if (::bind(s, reinterpret_cast<sockaddr *>(&a), sizeof(a)) != 0) {
      ESP_LOGW(TAG, "DNS bind failed (errno %d)", errno);
      ::close(s);
      return;
    }
    ::fcntl(s, F_SETFL, O_NONBLOCK);
    this->dns_sock_ = s;
    this->dns_bound_ip_ = ip;
    ESP_LOGD(TAG, "DNS on the access point address");
  }
  uint8_t buf[300];
  for (int k = 0; k < 4; k++) {  // a few queries per loop, never block
    sockaddr_in from{};
    socklen_t fl = sizeof(from);
    const int n = ::recvfrom(this->dns_sock_, buf, sizeof(buf) - 16, 0, reinterpret_cast<sockaddr *>(&from), &fl);
    if (n <= 0)
      return;
    const size_t len = ap_portal_core::dns_answer(buf, (size_t) n, buf, sizeof(buf), ip);
    if (len > 0)
      ::sendto(this->dns_sock_, buf, len, 0, reinterpret_cast<sockaddr *>(&from), fl);
  }
}

void ApPortal::dns_close_() {
  if (this->dns_sock_ >= 0)
    ::close(this->dns_sock_);
  this->dns_sock_ = -1;
  this->dns_bound_ip_ = 0;
}

// ───────────────────────────── quiet mode ─────────────────────────────

void ApPortal::quiet_tick_(uint32_t now, bool router) {
  auto *cp = captive_portal::global_captive_portal;
  if (cp == nullptr)
    return;
  if (this->quiet_ours_ && !cp->is_active()) {
    // ESPHome ends the portal itself in disable_ap_() when it connects to the router.
    this->quiet_ours_ = false;
    ESP_LOGD(TAG, "Quiet mode ended by ESPHome (router back)");
  }
  const bool ap = this->ap_up_();

  const bool settled = this->ap_channel_ != 0 || now - this->boot_ms_ >= ap_portal_core::QUIET_BOOT_GRACE_MS;
  if (!this->quiet_ours_ && !cp->is_active() && ap_portal_core::quiet_wanted({router, ap, settled})) {
    cp->start();
    this->quiet_ours_ = true;
    this->quiet_starts_++;
    this->router_since_ = 0;
    ESP_LOGI(TAG, "Quiet mode on: no router, %u client(s)", (unsigned) this->client_count());
  }
  if (this->quiet_ours_) {
    if (router) {
      if (this->router_since_ == 0)
        this->router_since_ = now ? now : 1;
      if (ap_portal_core::elapsed(now, this->router_since_, QUIET_END_ROUTER_MS)) {
        cp->end();
        this->quiet_ours_ = false;
        ESP_LOGI(TAG, "Quiet mode off: router back");
      }
    } else {
      this->router_since_ = 0;
    }
  }

  // Lock to the router's last channel — the access point serves there too (one radio). Never to
  // the radio's current channel, never to a guess.
  const uint8_t ch = this->ap_channel_;
  // While switching the router network the new one may be on another channel: quiet mode stays
  // (no scans, no adapter restarts — the access point keeps running), the channel lock does not.
  const bool quiet = this->quiet_ours_ && cp->is_active() && !this->switch_pending_;
  switch (this->lock_.update(ap_portal_core::lock_wanted(quiet, ch), ch, now)) {
    case ap_portal_core::ChannelLock::Action::LOCK:
      this->lock_apply_(this->lock_.channel());
      break;
    case ap_portal_core::ChannelLock::Action::RELEASE:
      this->lock_release_();
      break;
    default:
      break;
  }
}

// ESPHome's blind connect attempts probe every channel; limiting the radio to the access point's
// channel keeps the clients' packets flowing. Restored for the periodic full search and as soon
// as quiet mode ends.
void ApPortal::lock_apply_(uint8_t channel) {
  if (!this->country_saved_) {
    if (esp_wifi_get_country(&this->country_orig_) != ESP_OK)
      return;
    this->country_saved_ = true;
  }
  wifi_country_t c = this->country_orig_;
  c.schan = channel;
  c.nchan = 1;
  c.policy = WIFI_COUNTRY_POLICY_MANUAL;
  const esp_err_t e = esp_wifi_set_country(&c);
  ESP_LOGD(TAG, "Radio locked to channel %u: %s", (unsigned) channel, esp_err_to_name(e));
}

void ApPortal::lock_release_() {
  if (!this->country_saved_)
    return;
  const esp_err_t e = esp_wifi_set_country(&this->country_orig_);
  ESP_LOGD(TAG, "Radio channel lock released: %s", esp_err_to_name(e));
}

// ───────────────────────────── switching the router network ─────────────────────────────
// save_wifi_sta() alone does not reconnect while the device is connected (2026-11-18: saved,
// nothing happened), so the router connection is dropped after it. While switching, quiet mode
// keeps the access point up but the channel is not locked: the new router may be on another
// channel. If the new network
// does not take within SWITCH_TIMEOUT_MS, the previous one is put back.
static constexpr uint32_t SWITCH_TIMEOUT_MS = 30000;

static std::string connected_ssid() {
  wifi_ap_record_t info{};
  if (esp_wifi_sta_get_ap_info(&info) != ESP_OK)
    return {};
  const char *s = reinterpret_cast<const char *>(info.ssid);
  return std::string(s, strnlen(s, sizeof(info.ssid)));
}

void ApPortal::switch_start_(const std::string &ssid, const std::string &psk, uint32_t now,
                             const std::string *old_ssid, const std::string *old_psk) {
  auto *w = wifi::global_wifi_component;
  if (old_ssid != nullptr) {
    this->switch_old_ssid_ = *old_ssid;
    this->switch_old_psk_ = *old_psk;
  } else if (!this->switch_pending_) {  // a second request keeps the first fallback
    const auto old = w->get_sta();
    this->switch_old_ssid_ = std::string(old.get_ssid().c_str(), old.get_ssid().size());
    this->switch_old_psk_ = std::string(old.get_password().c_str(), old.get_password().size());
  }
  ESP_LOGI(TAG, "Wi-Fi page: switching to the router network '%s' (fallback '%s' in %u s)", ssid.c_str(),
           this->switch_old_ssid_.c_str(), (unsigned) (SWITCH_TIMEOUT_MS / 1000));
  {
    std::lock_guard<std::mutex> g(this->mu_);
    this->switch_reverted_ = false;
    this->switch_previous_ = this->switch_old_ssid_;
  }
  this->switch_target_ = ssid;
  this->switch_pending_ = true;
  this->switch_deadline_ = now + SWITCH_TIMEOUT_MS;
  this->switch_reconnect_(ssid, psk);
}

// Save the network and make ESPHome go to it. Only the router connection is dropped, never the
// whole Wi-Fi: wifi.configure-style disable()/enable() also took the access point down and phones
// went off to another network (2026-11-18). After esp_wifi_disconnect() ESPHome reconnects to the
// station it now has, the new one.
void ApPortal::switch_reconnect_(const std::string &ssid, const std::string &psk) {
  auto *w = wifi::global_wifi_component;
  const bool was_connected = w->is_connected();
  w->save_wifi_sta(ssid, psk);  // saved by ESPHome, kept across reboots; reconnects only if not connected
  if (was_connected)
    esp_wifi_disconnect();
}

void ApPortal::switch_tick_(uint32_t now) {
  if (!this->switch_pending_)
    return;
  auto *w = wifi::global_wifi_component;
  if (w->is_connected() && connected_ssid() == this->switch_target_) {
    ESP_LOGI(TAG, "Wi-Fi page: connected to '%s'", this->switch_target_.c_str());
    this->switch_pending_ = false;
    return;
  }
  if (!ap_portal_core::reached(now, this->switch_deadline_))
    return;
  this->switch_pending_ = false;
  if (this->switch_old_ssid_.empty() || this->switch_old_ssid_ == this->switch_target_) {
    ESP_LOGW(TAG, "Wi-Fi page: '%s' did not connect, nothing to fall back to", this->switch_target_.c_str());
    return;
  }
  ESP_LOGW(TAG, "Wi-Fi page: '%s' did not connect in %u s (%s), back to '%s'", this->switch_target_.c_str(),
           (unsigned) (SWITCH_TIMEOUT_MS / 1000), ap_portal_core::sta_error_kind(this->sta_reason_.load()),
           this->switch_old_ssid_.c_str());
  {
    std::lock_guard<std::mutex> g(this->mu_);
    this->switch_reverted_ = true;
  }
  this->switch_reconnect_(this->switch_old_ssid_, this->switch_old_psk_);
  this->switch_old_psk_.clear();
}

// "Save, connect later": the network is saved (so a restart uses it) but the device stays where
// it is while someone works on the access point. It switches once the last client has left for
// LATER_IDLE_MS, or when asked; the fallback is the network it was on when the page saved.
static constexpr uint32_t LATER_IDLE_MS = 10000;

void ApPortal::later_save_(const std::string &ssid, const std::string &psk) {
  auto *w = wifi::global_wifi_component;
  if (!this->later_pending_) {
    const auto old = w->get_sta();
    this->later_old_ssid_ = std::string(old.get_ssid().c_str(), old.get_ssid().size());
    this->later_old_psk_ = std::string(old.get_password().c_str(), old.get_password().size());
  }
  this->later_ssid_ = ssid;
  this->later_psk_ = psk;
  this->later_pending_ = true;
  this->later_idle_since_ = 0;
  {
    std::lock_guard<std::mutex> g(this->mu_);
    this->later_ssid_snap_ = ssid;
  }
  w->save_wifi_sta(ssid, psk);  // connected: saved only, no reconnect — exactly what is wanted here
  ESP_LOGI(TAG, "Wi-Fi page: '%s' saved, the device stays on '%s' until the access point is free",
           ssid.c_str(), this->later_old_ssid_.c_str());
}

void ApPortal::later_apply_(uint32_t now, const char *why) {
  ESP_LOGI(TAG, "Wi-Fi page: applying the saved network '%s' (%s)", this->later_ssid_.c_str(), why);
  this->later_pending_ = false;
  {
    std::lock_guard<std::mutex> g(this->mu_);
    this->later_ssid_snap_.clear();
    this->target_ssid_ = this->later_ssid_;
    this->sta_reason_ = 0;
  }
  this->switch_start_(this->later_ssid_, this->later_psk_, now, &this->later_old_ssid_, &this->later_old_psk_);
  this->later_psk_.clear();
  this->later_old_psk_.clear();
}

void ApPortal::later_cancel_() {
  ESP_LOGI(TAG, "Wi-Fi page: '%s' cancelled, staying on '%s'", this->later_ssid_.c_str(),
           this->later_old_ssid_.c_str());
  this->later_pending_ = false;
  {
    std::lock_guard<std::mutex> g(this->mu_);
    this->later_ssid_snap_.clear();
  }
  wifi::global_wifi_component->save_wifi_sta(this->later_old_ssid_, this->later_old_psk_);
  this->later_psk_.clear();
  this->later_old_psk_.clear();
}

void ApPortal::later_tick_(uint32_t now) {
  if (!this->later_pending_)
    return;
  if (this->client_count() > 0) {
    this->later_idle_since_ = 0;
    return;
  }
  if (this->later_idle_since_ == 0)
    this->later_idle_since_ = now ? now : 1;
  if (ap_portal_core::elapsed(now, this->later_idle_since_, LATER_IDLE_MS))
    this->later_apply_(now, "nobody on the access point");
}

// ───────────────────────────── password reset on the spot ─────────────────────────────

static const char *reset_reason_name(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON: return "power on";
    case ESP_RST_EXT: return "external";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_DEEPSLEEP: return "deep sleep";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_USB: return "USB";
    default: return "other";
  }
}

// In setup(), before the saved password is read. The count lives in flash (RTC memory does not
// survive a power cut) and is synced at once: the next power cut may come within a second.
void ApPortal::power_cycle_boot_() {
  if (this->power_cycles_ == 0)
    return;
  const esp_reset_reason_t why = esp_reset_reason();
  const bool counted = why == ESP_RST_POWERON || why == ESP_RST_EXT;
  this->cycles_pref_ = global_preferences->make_preference<uint8_t>(fnv1_hash("ap_portal_power_cycles"));
  uint8_t stored = 0;
  this->cycles_pref_.load(&stored);
  const auto st = ap_portal_core::power_cycle_step(stored, counted, this->power_cycles_);
  if (st.count != stored || st.reset) {
    this->cycles_pref_.save(&st.count);
  }
  if (st.reset) {
    PasswordRecord rec{};  // magic 0: nothing saved
    this->password_pref_.save(&rec);
    this->reset_by_cycles_ = true;
    ESP_LOGW(TAG, "%u quick starts in a row: access point password back to firmware", this->power_cycles_);
  } else if (st.count > 0) {
    this->power_cycle_count_ = st.count;
    this->cycle_clear_pending_ = true;
    ESP_LOGI(TAG, "Quick start %u/%u (%s); %u more within %u s each resets the access point password", st.count,
             this->power_cycles_, reset_reason_name(why), this->power_cycles_ - st.count,
             (unsigned) (ap_portal_core::POWER_CYCLE_WINDOW_MS / 1000));
  }
  if (st.count != stored || st.reset)
    global_preferences->sync();
}

void ApPortal::reset_tick_(uint32_t now) {
  if (!this->boot_hooks_done_) {  // triggers from loop(), not setup()
    this->boot_hooks_done_ = true;
    if (this->power_cycle_count_ > 0)
      this->on_power_cycle_.call(this->power_cycle_count_);
    if (this->reset_by_cycles_)
      this->on_password_reset_.call("power_cycles");
    if (this->boot_pin_ >= 0) {
      gpio_config_t io{};
      io.pin_bit_mask = 1ULL << this->boot_pin_;
      io.mode = GPIO_MODE_INPUT;
      io.pull_up_en = GPIO_PULLUP_ENABLE;
      gpio_config(&io);
    }
  }
  // Running longer than the window: this start was a normal one, the series is over.
  if (this->cycle_clear_pending_ && now >= ap_portal_core::POWER_CYCLE_WINDOW_MS) {
    this->cycle_clear_pending_ = false;
    uint8_t zero = 0;
    this->cycles_pref_.save(&zero);
    global_preferences->sync();
  }
  bool pressed = false;
  if (this->reset_pin_ != nullptr) {
    pressed = this->reset_pin_->digital_read();
  } else if (this->boot_pin_ >= 0) {
    pressed = gpio_get_level((gpio_num_t) this->boot_pin_) == 0;  // BOOT pulls to ground
  } else {
    return;
  }
  if (this->reset_hold_.update(pressed, now)) {
    ESP_LOGW(TAG, "Reset button held: access point password back to firmware");
    this->password_pending_ = false;
    this->apply_password_(this->firmware_password_, true);
    this->on_password_reset_.call("button");
  }
}

// ───────────────────────────── HTTP ─────────────────────────────

ApPortal::Classified ApPortal::classify_(AsyncWebServerRequest *request) {
  Classified c;
  char ubuf[AsyncWebServerRequest::URL_BUF_SIZE];
  c.path = std::string(request->url_to(ubuf).c_str());
  ap_portal_core::Request r;
  r.get = request->method() == HTTP_GET;
  auto host = request->get_header("Host");
  if (host.has_value())
    r.host = host.value();
  r.path = c.path;
  r.raw = request->hasParam(ap_portal_core::WRAP_RAW_ARG);

  // Which interface: the address the client connected to is ours on that interface.
  const int fd = httpd_req_to_sockfd(*request);
  sockaddr_storage local{}, peer{};
  socklen_t ll = sizeof(local), pl = sizeof(peer);
  const uint32_t ap_ip = this->ap_ip_be_();
  if (fd >= 0 && ::getsockname(fd, reinterpret_cast<sockaddr *>(&local), &ll) == 0)
    r.from_ap = ap_ip != 0 && v4_of(local) == ap_ip;
  c.from_ap = r.from_ap;
  if (fd >= 0 && ::getpeername(fd, reinterpret_cast<sockaddr *>(&peer), &pl) == 0)
    c.client_ip = v4_of(peer);

  if (r.from_ap && this->route_.mode == ap_portal_core::Mode::GATE) {
    std::lock_guard<std::mutex> g(this->mu_);
    r.has_pass = this->passes_.check(c.client_ip, millis());
  }
  c.route = ap_portal_core::route(r, this->route_);
  auto *cp = captive_portal::global_captive_portal;
  c.claim = ap_portal_core::claim(c.route, r.get, cp != nullptr && cp->is_active());

  if (r.get && r.from_ap) {
    bool log = true;
    for (const auto &p : this->log_exclude_prefixes_)
      if (ap_portal_core::has_prefix(c.path, p))
        log = false;
    if (log) {
      ESP_LOGV(TAG, "GET %s%s from %s -> %u", r.host.c_str(), c.path.c_str(), ip_str(c.client_ip).c_str(),
               (unsigned) c.route);
    }
  }
  return c;
}

bool ApPortal::canHandle(AsyncWebServerRequest *request) const {
  auto *self = const_cast<ApPortal *>(this);  // the web server API is const; state is ours
  if (!self->running_)
    return false;
  self->cls_ = self->classify_(request);
  return self->cls_.claim != ap_portal_core::Claim::NONE;
}

void ApPortal::handleRequest(AsyncWebServerRequest *request) {
  const Classified c = this->cls_;
  switch (c.route) {
    case ap_portal_core::Route::LANDING: {
      this->send_landing_(request, c);
      Event ev{};
      ev.type = EV_LANDING;
      ev.ip = c.client_ip;
      std::strncpy(ev.path, c.path.c_str(), sizeof(ev.path) - 1);
      this->post_(ev);
      return;
    }
    case ap_portal_core::Route::REDIRECT:
      this->send_redirect_(request, "http://" + ip_str(this->ap_ip_be_()) + this->route_.landing_path);
      return;
    case ap_portal_core::Route::ENTER: {
      Event ev{};
      ev.type = EV_ENTER;
      ev.ip = c.client_ip;
      {
        std::lock_guard<std::mutex> g(this->mu_);
        ap_portal_core::Mac m;
        this->clients_.find(c.client_ip, m);  // unknown MAC (static IP): the pass goes by IP alone
        this->passes_.grant(m, c.client_ip, millis());
        std::memcpy(ev.mac, m.b, 6);
      }
      this->post_(ev);
      this->send_redirect_(request, "/");
      return;
    }
    case ap_portal_core::Route::WRAP: {
      std::string body = ap_portal_core::render(WRAP_PAGE, this->page_vars_(c));
      auto *r = request->beginResponse(200, "text/html", body);
      r->addHeader("Cache-Control", "no-store");
      request->send(r);
      return;
    }
    case ap_portal_core::Route::WIFI:
      this->send_wifi_page_(request, c);
      return;
    case ap_portal_core::Route::API:
      this->handle_api_(request, c);
      return;
    case ap_portal_core::Route::PASS:
      this->forward_(request);
      return;
  }
}

// ───────────────────────────── the Wi-Fi page ─────────────────────────────

bool ApPortal::HomeApi::canHandle(AsyncWebServerRequest *request) const {
  if (!this->parent->route_.api)  // also while the portal is off: the site can still change the router
    return false;
  char ubuf[AsyncWebServerRequest::URL_BUF_SIZE];
  if (!ap_portal_core::has_prefix(std::string(request->url_to(ubuf).c_str()), ap_portal_core::API_PREFIX))
    return false;
  this->parent->cls_ = this->parent->classify_(request);
  return !this->parent->cls_.from_ap;  // access point clients went through the portal handler
}

void ApPortal::HomeApi::handleRequest(AsyncWebServerRequest *request) {
  this->parent->handle_api_(request, this->parent->cls_);
}

void ApPortal::send_wifi_page_(AsyncWebServerRequest *request, const Classified &c) {
  std::string body = ap_portal_core::render(BUILTIN_WIFI, this->page_vars_(c));
  auto *r = request->beginResponse(200, "text/html", body);
  r->addHeader("Cache-Control", "no-store");
  request->send(r);
}

// The web server task only records what was asked; loop() carries it out (http_requests_tick_).
void ApPortal::handle_api_(AsyncWebServerRequest *request, const Classified &c) {
  const std::string &p = c.path;
  const std::string a = std::string(ap_portal_core::API_PREFIX);
  const bool post = request->method() == HTTP_POST;
  if (p == a + "networks") {
    this->send_networks_(request);
    return;
  }
  if (p == a + "client.js" || p == a + "popup.js") {
    std::string js(CLIENT_JS);
    if (p == a + "popup.js")
      js += POPUP_JS;  // one file: the client, then the popup
    auto *r = request->beginResponse(200, "application/javascript", js);
    r->addHeader("Cache-Control", "max-age=300");
    request->send(r);
    return;
  }
  if (!post) {
    request->send(405, "text/plain", "Use POST");
    return;
  }
  if (p == a + "scan") {
    std::lock_guard<std::mutex> g(this->mu_);
    this->http_scan_ = true;
  } else if (p == a + "connect") {
    const std::string ssid = request->arg("ssid");
    const std::string psk = request->arg("psk");
    if (ssid.empty() || ssid.size() > 32 || psk.size() > 64 || (!psk.empty() && psk.size() < 8)) {
      request->send(400, "text/plain", "SSID 1..32, password empty or 8..64");
      return;
    }
    const bool later = request->arg("when") == "later";
    std::lock_guard<std::mutex> g(this->mu_);
    this->http_sta_ssid_ = ssid;
    this->http_sta_psk_ = psk;
    this->http_sta_later_ = later;
    if (!later) {
      this->target_ssid_ = ssid;  // the page asks how this one goes
      this->sta_reason_ = 0;
    }
    this->http_sta_at_ = millis() + (later ? 0 : PASSWORD_APPLY_DELAY_MS);  // the answer leaves first
    this->http_sta_pending_ = true;
  } else if (p == a + "apply" || p == a + "cancel") {
    std::lock_guard<std::mutex> g(this->mu_);
    if (p == a + "apply")
      this->target_ssid_ = this->later_ssid_snap_;
    this->http_later_cmd_ = p == a + "apply" ? 1 : 2;
  } else if (this->wifi_ap_password_ && p == a + "ap_password") {
    const std::string pw = request->arg("password");
    if (!ap_portal_core::valid_ap_password(pw)) {
      request->send(400, "text/plain", "Empty or 8..63 printable characters");
      return;
    }
    std::lock_guard<std::mutex> g(this->mu_);
    this->http_ap_pw_ = pw;
    this->http_ap_pw_reset_ = false;
    this->http_ap_pw_pending_ = true;
  } else if (this->wifi_ap_password_ && p == a + "ap_password_reset") {
    std::lock_guard<std::mutex> g(this->mu_);
    this->http_ap_pw_reset_ = true;
    this->http_ap_pw_pending_ = true;
  } else {
    request->send(404, "text/plain", "Not found");
    return;
  }
  request->send(200, "text/plain", "OK");
}

// Scan results and the router connection as JSON. Hidden and duplicate networks are skipped the
// way ESPHome's own form does; SSIDs are JSON-escaped.
void ApPortal::send_networks_(AsyncWebServerRequest *request) {
  auto *w = wifi::global_wifi_component;
  std::string out = "{\"scanning\":";
  out += this->scanning_ ? "true" : "false";
  wifi_ap_record_t info{};
  const bool connected = w->is_connected() && esp_wifi_sta_get_ap_info(&info) == ESP_OK;
  char esc[32 * JSON_ESCAPE_MAX_EXPANSION + 1];
  out += ",\"connected\":";
  out += connected ? "true" : "false";
  if (connected) {
    const char *ssid = reinterpret_cast<const char *>(info.ssid);
    json_escape_into_buffer(esc, StringRef(ssid, strnlen(ssid, sizeof(info.ssid))));
    std::string ip;
    esp_netif_t *sn = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t si{};
    if (sn != nullptr && esp_netif_get_ip_info(sn, &si) == ESP_OK)
      ip = ip_str(si.ip.addr);
    out += ",\"ssid\":\"" + std::string(esc) + "\",\"ip\":\"" + ip + "\",\"rssi\":" + to_string(info.rssi);
  }
  // Where to find the device again after it changes networks.
  json_escape_into_buffer(esc, StringRef(this->ssid_snap_.c_str(), this->ssid_snap_.size()));
  out += ",\"host\":\"" + this->host_ + "\",\"ap_ip\":\"" + ip_str(this->ap_ip_be_()) + "\",\"ap_ssid\":\"" +
         std::string(esc) + "\",\"ap_active\":" + (this->running_ && this->ap_up_() ? "true" : "false");
  {
    std::string target;
    {
      std::lock_guard<std::mutex> g(this->mu_);
      target = this->target_ssid_;
    }
    if (!target.empty()) {
      json_escape_into_buffer(esc, StringRef(target.c_str(), target.size()));
      const bool on_target = connected && connected_ssid() == target;
      out += ",\"target\":\"" + std::string(esc) + "\",\"sta_error\":\"" +
             (on_target ? "" : ap_portal_core::sta_error_kind(this->sta_reason_.load())) + "\"";
      std::string prev;
      bool reverted;
      {
        std::lock_guard<std::mutex> g(this->mu_);
        prev = this->switch_previous_;
        reverted = this->switch_reverted_;
      }
      json_escape_into_buffer(esc, StringRef(prev.c_str(), prev.size()));
      out += ",\"previous\":\"" + std::string(esc) + "\",\"reverted\":" + (reverted ? "true" : "false");
    }
  }
  {
    std::string later;
    {
      std::lock_guard<std::mutex> g(this->mu_);
      later = this->later_ssid_snap_;
    }
    json_escape_into_buffer(esc, StringRef(later.c_str(), later.size()));
    out += ",\"later\":\"" + std::string(esc) + "\"";
  }
  out += ",\"networks\":[";
  {
    wifi::ScanResultsLock lock(w);
    const auto &results = w->get_scan_result();
    bool first = true;
    for (const auto &scan : results) {
      bool with_auth = false;
      if (!wifi::should_show_scan_entry(results, scan, with_auth))
        continue;
      json_escape_into_buffer(esc, scan.get_ssid());
      if (!first)
        out += ",";
      first = false;
      out += "{\"ssid\":\"" + std::string(esc) + "\",\"rssi\":" + to_string(scan.get_rssi()) +
             ",\"lock\":" + (with_auth ? "true" : "false") + "}";
    }
  }
  out += "]}";
  auto *r = request->beginResponse(200, "application/json", out);
  r->addHeader("Cache-Control", "no-store");
  request->send(r);
}

void ApPortal::http_requests_tick_(uint32_t now) {
  bool scan = false, sta = false, sta_later = false, ap_pw = false, ap_reset = false;
  uint8_t later_cmd = 0;
  std::string ssid, psk, pw;
  {
    std::lock_guard<std::mutex> g(this->mu_);
    if (this->http_scan_) {
      scan = true;
      this->http_scan_ = false;
    }
    if (this->http_sta_pending_ && ap_portal_core::reached(now, this->http_sta_at_)) {
      sta = true;
      this->http_sta_pending_ = false;
      ssid.swap(this->http_sta_ssid_);
      psk.swap(this->http_sta_psk_);
      sta_later = this->http_sta_later_;
    }
    later_cmd = this->http_later_cmd_;
    this->http_later_cmd_ = 0;
    if (this->http_ap_pw_pending_) {
      ap_pw = true;
      ap_reset = this->http_ap_pw_reset_;
      this->http_ap_pw_pending_ = false;
      pw.swap(this->http_ap_pw_);
    }
  }
  if (scan)
    this->scan();
  if (sta && sta_later && wifi::global_wifi_component->is_connected())
    this->later_save_(ssid, psk);
  else if (sta)
    this->switch_start_(ssid, psk, now);
  if (later_cmd == 1 && this->later_pending_)
    this->later_apply_(now, "asked from the page");
  else if (later_cmd == 2 && this->later_pending_)
    this->later_cancel_();
  this->later_tick_(now);
  this->switch_tick_(now);
  if (ap_pw) {
    if (ap_reset)
      this->reset_password();
    else
      this->set_password(pw);  // applied 2 s later, the page got its answer
  }
}

// A PASS while captive_portal runs: the next handler that is not captive_portal, or 404.
void ApPortal::forward_(AsyncWebServerRequest *request) {
  auto *base = web_server_base::global_web_server_base;
  auto *cp = captive_portal::global_captive_portal;
  if (base != nullptr) {
    const auto &handlers = base->*handlers_member(HandlersTag{});
    for (auto *h : handlers) {
      if (h == this || h == static_cast<AsyncWebHandler *>(cp))
        continue;
      if (h->canHandle(request)) {
        h->handleRequest(request);
        return;
      }
    }
  }
  request->send(404, "text/plain", "Not found");
}

ap_portal_core::Vars ApPortal::page_vars_(const Classified &c) {
  ap_portal_core::Vars v = this->static_vars_;  // first: YAML values win over the defaults below
  {
    std::lock_guard<std::mutex> g(this->mu_);
    v.insert(v.end(), this->lambda_snap_.begin(), this->lambda_snap_.end());
  }
  std::string sta;
  esp_netif_t *sn = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  esp_netif_ip_info_t si{};
  if (sn != nullptr && esp_netif_get_ip_info(sn, &si) == ESP_OK && si.ip.addr != 0)
    sta = ip_str(si.ip.addr);
  const bool gate = this->route_.mode == ap_portal_core::Mode::GATE;
  v.emplace_back("title", this->title_);
  v.emplace_back("ap_ip", ip_str(this->ap_ip_be_()));
  v.emplace_back("sta_ip", sta);
  v.emplace_back("host", this->host_);
  v.emplace_back("ssid", this->ssid_snap_);
  v.emplace_back("enter", gate ? this->route_.enter_path : std::string("/"));
  v.emplace_back("landing", this->route_.landing_path);
  v.emplace_back("mode", mode_name(this->route_.mode));
  v.emplace_back("clients", to_string(this->client_count()));
  v.emplace_back("wifi_path", this->route_.wifi_path);
  v.emplace_back("ap_password_block", this->wifi_ap_password_ ? "1" : "");
  v.emplace_back("custom_password", this->custom_password_ ? "1" : "");
  return v;
}

void ApPortal::send_landing_(AsyncWebServerRequest *request, const Classified &c) {
  if (this->html_gz_ != nullptr) {
    auto *r = request->beginResponse(200, "text/html", this->html_gz_, this->html_gz_len_);
    r->addHeader("Content-Encoding", "gzip");
    r->addHeader("Cache-Control", "no-store");
    request->send(r);
    return;
  }
  const std::string &tpl = this->html_.empty() ? std::string(BUILTIN_LANDING) : this->html_;
  std::string body = ap_portal_core::render(tpl, this->page_vars_(c));
  if (body.size() < 8)  // Android takes 200 with an (almost) empty body for "no portal"
    body.append("<!-- ap_portal -->");
  auto *r = request->beginResponse(200, "text/html", body);
  r->addHeader("Cache-Control", "no-store");
  request->send(r);
}

// 302 with a body: some phones treat an empty answer as "internet works".
void ApPortal::send_redirect_(AsyncWebServerRequest *request, const std::string &location) {
  httpd_req_t *r = *request;
  const std::string body = "<a href=\"" + location + "\">" + location + "</a>";
  httpd_resp_set_status(r, "302 Found");
  httpd_resp_set_type(r, "text/html");
  httpd_resp_set_hdr(r, "Location", location.c_str());
  httpd_resp_set_hdr(r, "Cache-Control", "no-store");
  httpd_resp_send(r, body.c_str(), body.size());
}

void ApPortal::refresh_lambda_vars_(uint32_t now) {
  if (this->lambda_vars_.empty())
    return;
  if (this->lambda_refresh_ms_ != 0 && !ap_portal_core::elapsed(now, this->lambda_refresh_ms_, LAMBDA_REFRESH_MS))
    return;
  this->lambda_refresh_ms_ = now ? now : 1;
  ap_portal_core::Vars snap;
  snap.reserve(this->lambda_vars_.size());
  for (auto &kv : this->lambda_vars_)
    snap.emplace_back(kv.first, kv.second());  // lambdas run here, in loop(), never in the web server task
  std::lock_guard<std::mutex> g(this->mu_);
  this->lambda_snap_.swap(snap);
}

// ───────────────────────────── actions ─────────────────────────────

void ApPortal::scan() {
  if (this->scanning_) {
    ESP_LOGD(TAG, "scan: already running");
    return;
  }
  this->scan_start_();
}

// With the router: a background scan — between channels the radio returns to the router's,
// the association stays (no 1-3 s drop like ESPHome's own scan). Results reach ESPHome's
// SCAN_DONE handler as usual, so wifi_info's scan results update. Without the router ESPHome
// scans in its connect cycle anyway: its own scan then.
void ApPortal::scan_start_() {
  auto *w = wifi::global_wifi_component;
  const uint32_t now = millis();
  if (!w->is_connected()) {
    ESP_LOGI(TAG, "scan: no router, ESPHome scan");
    w->start_scanning();
    this->scanning_ = true;
    this->scan_started_ms_ = now;
    return;
  }
  wifi_scan_config_t c{};
  c.show_hidden = false;
  c.scan_type = WIFI_SCAN_TYPE_ACTIVE;
  c.scan_time.active.min = 60;
  c.scan_time.active.max = 120;
  c.coex_background_scan = true;
  w->request_roaming_suppression();  // ESPHome must not take our results for a roaming scan
  const esp_err_t e = esp_wifi_scan_start(&c, false);
  if (e != ESP_OK) {
    w->release_roaming_suppression();
    ESP_LOGW(TAG, "scan: %s, retry in 2 s", esp_err_to_name(e));
    this->scan_retry_at_ = (now + SCAN_RETRY_MS) | 1;
    return;
  }
  this->scan_suppressing_ = true;
  this->scanning_ = true;
  this->scan_started_ms_ = now;
  ESP_LOGI(TAG, "scan: background scan started");
}

void ApPortal::set_password(const std::string &password) {
  if (!ap_portal_core::valid_ap_password(password)) {
    ESP_LOGW(TAG, "set_password: rejected (empty or 8..63 printable characters)");
    return;
  }
  // Applied 2 s later: the page that asked gets its answer before clients are dropped.
  this->password_pending_ = true;
  this->password_pending_save_ = true;
  this->password_pending_value_ = password;
  this->password_apply_at_ = millis() + PASSWORD_APPLY_DELAY_MS;
}

void ApPortal::reset_password() {
  this->password_pending_ = true;
  this->password_pending_save_ = true;
  this->password_pending_value_ = this->firmware_password_;
  this->password_apply_at_ = millis() + PASSWORD_APPLY_DELAY_MS;
}

// Into ESPHome's own configuration (one source: whoever reads get_ap() sees it, our keeper
// raises the access point with it) and into the running access point. Clients are dropped and
// join again with the new password.
void ApPortal::apply_password_(const std::string &password, bool save) {
  auto *w = wifi::global_wifi_component;
  auto ap = w->get_ap();
  ap.set_password(password);
  w->set_ap(ap);
  this->custom_password_ = password != this->firmware_password_;
  if (save && this->password_persist_) {
    PasswordRecord rec{};
    if (this->custom_password_) {
      rec.magic = PASSWORD_MAGIC;
      rec.len = (uint8_t) password.size();
      std::memcpy(rec.pw, password.data(), password.size());
    }  // firmware password: magic 0 = nothing saved
    this->password_pref_.save(&rec);
    global_preferences->sync();
  }
  esp_err_t e = ESP_OK;
  if (this->ap_up_()) {
    wifi_config_t c{};
    e = esp_wifi_get_config(WIFI_IF_AP, &c);
    if (e == ESP_OK) {
      std::memset(c.ap.password, 0, sizeof(c.ap.password));
      if (password.size() >= 8) {
        c.ap.authmode = WIFI_AUTH_WPA2_PSK;
        std::memcpy(c.ap.password, password.data(), std::min<size_t>(password.size(), sizeof(c.ap.password) - 1));
      } else {
        c.ap.authmode = WIFI_AUTH_OPEN;
      }
      e = esp_wifi_set_config(WIFI_IF_AP, &c);
    }
  }
  ESP_LOGW(TAG, "Access point password %s (%s): %s", this->custom_password_ ? "changed" : "back to firmware",
           password.empty() ? "open" : "WPA2", esp_err_to_name(e));
}

// ───────────────────────────── state ─────────────────────────────

uint32_t ApPortal::client_count() const {
  wifi_sta_list_t l;
  if (esp_wifi_ap_get_sta_list(&l) != ESP_OK)
    return 0;
  return (uint32_t) l.num;
}

std::string ApPortal::ap_ip() const {
  const uint32_t ip = this->ap_ip_be_();
  if (ip == 0)
    return {};
  char b[16];
  const uint8_t *p = reinterpret_cast<const uint8_t *>(&ip);
  snprintf(b, sizeof(b), "%u.%u.%u.%u", p[0], p[1], p[2], p[3]);
  return b;
}

std::string ApPortal::ssid() const {
  auto *w = wifi::global_wifi_component;
  return w == nullptr ? std::string() : std::string(w->get_ap().get_ssid());
}

}  // namespace esphome::ap_portal
