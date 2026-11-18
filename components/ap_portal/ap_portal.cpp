#include "ap_portal.h"

#include <cerrno>
#include <cstring>

#include <esp_netif.h>
#include <esp_wifi.h>
#include <lwip/sockets.h>
#include "dhcpserver/dhcpserver.h"

#include "esphome/components/captive_portal/captive_portal.h"
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

// ───────────────────────────── setup / loop ─────────────────────────────

void ApPortal::setup() {
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

  const bool router = w->is_connected();
  if (router) {
    // One radio: connected, the access point serves on the router's channel. Remember it — after
    // the router is lost the radio wanders through ESPHome's scans and its current channel means
    // nothing.
    uint8_t ch = 0;
    wifi_second_chan_t sec;
    if (esp_wifi_get_channel(&ch, &sec) == ESP_OK && ch != 0)
      this->ap_channel_ = ch;
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
}

// ───────────────────────────── events ─────────────────────────────

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
    default:
      return;
  }
  if (self->events_ != nullptr)
    xQueueSend(self->events_, &ev, 0);  // full queue: drop, client_count() asks the driver anyway
}

void ApPortal::register_events_() {
  esp_err_t e = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &ApPortal::on_wifi_event_, this,
                                                    nullptr);
  if (e == ESP_OK) {
    this->events_registered_ = true;
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
      this->passes_.revoke(m);
      this->on_client_disconnected_.call(mac_str(ev.mac));
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
  if (this->ap_was_up_ && this->ap_stopped_by_esphome_)
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
  this->passes_.clear();
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

  if (!this->quiet_ours_ && !cp->is_active() && ap_portal_core::quiet_wanted({router, ap})) {
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

  // Lock to the access point's own channel, not to wherever the radio is right now.
  wifi_config_t apc{};
  uint8_t ch = 0;
  if (esp_wifi_get_config(WIFI_IF_AP, &apc) == ESP_OK)
    ch = apc.ap.channel;
  if (ch == 0)
    ch = this->ap_channel_;
  switch (this->lock_.update(this->quiet_ours_ && cp->is_active(), ch, now)) {
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

// ───────────────────────────── actions (stage 4) ─────────────────────────────

void ApPortal::scan() { ESP_LOGW(TAG, "scan: not implemented yet (stage 4)"); }

void ApPortal::set_password(const std::string &password) {
  if (!ap_portal_core::valid_ap_password(password)) {
    ESP_LOGW(TAG, "set_password: rejected (empty or 8..63 printable characters)");
    return;
  }
  ESP_LOGW(TAG, "set_password: not implemented yet (stage 4)");
}

void ApPortal::reset_password() { ESP_LOGW(TAG, "reset_password: not implemented yet (stage 4)"); }

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
