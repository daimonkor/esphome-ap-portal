#pragma once
// ap_portal — the device's own access point next to the router connection.
// Access point keeper, DNS, DHCP, quiet mode with channel lock, client events, the landing page
// (a template, see ap_portal_core.h), gate passes, network scan, access point password.

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include <esp_event.h>
#include <esp_wifi_types.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "esphome/components/web_server_base/web_server_base.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include "ap_portal_core.h"

namespace esphome::ap_portal {

class ApPortal : public Component, public AsyncWebHandler {
 public:
  // ── configuration (from YAML) ──
  void set_active(bool active) { this->active_ = active; }
  void set_active_lambda(std::function<bool()> &&f) { this->active_lambda_ = std::move(f); }
  void set_ssid_suffix_mac(bool v) { this->ssid_suffix_mac_ = v; }
  void set_password_persist(bool v) { this->password_persist_ = v; }
  void add_log_exclude_prefix(const std::string &p) { this->log_exclude_prefixes_.push_back(p); }
  void set_dns_enabled(bool v) { this->dns_enabled_ = v; }
  void set_mode(uint8_t m) { this->route_.mode = static_cast<ap_portal_core::Mode>(m); }
  void set_landing_path(const std::string &p) { this->route_.landing_path = p; }
  void set_html(const std::string &h) { this->html_ = h; }
  void set_html_gz(const uint8_t *data, size_t len) {
    this->html_gz_ = data;
    this->html_gz_len_ = len;
  }
  void add_static_var(const std::string &name, const std::string &value) {
    this->static_vars_.emplace_back(name, value);
  }
  void add_lambda_var(const std::string &name, std::function<std::string()> &&f) {
    this->lambda_vars_.emplace_back(name, std::move(f));
  }
  void set_enter_path(const std::string &p) { this->route_.enter_path = p; }
  void set_enter_window(uint32_t ms) { this->passes_.set_window(ms); }
  void add_pass_path(const std::string &p) { this->route_.pass_paths.push_back(p); }

  // ── component ──
  void setup() override;
  void loop() override;
  void dump_config() override;
  void on_shutdown() override;  // deep sleep / reboot: close DNS, give the radio its channels back
  // After network (event loop), before wifi (250) and web_server (249): the access point
  // settings reach ESPHome before Wi-Fi starts, and our HTTP handler goes in first.
  float get_setup_priority() const override { return setup_priority::WIFI + 10.0f; }

  // ── HTTP (runs in the web server task) ──
  bool canHandle(AsyncWebServerRequest *request) const override;
  void handleRequest(AsyncWebServerRequest *request) override;

  // ── actions ──
  void scan();
  void set_password(const std::string &password);
  void reset_password();

  // ── state for lambdas ──
  bool is_active() const { return this->running_; }
  uint32_t client_count() const;
  std::string ap_ip() const;
  std::string ssid() const;
  bool is_scanning() const { return this->scanning_; }
  bool has_custom_password() const { return this->custom_password_; }
  // diagnostics (for tests and sensors)
  bool is_quiet() const { return this->quiet_ours_; }
  uint8_t locked_channel() const { return this->lock_.locked() ? this->lock_.channel() : 0; }
  uint32_t lock_widens() const { return this->lock_.widens(); }
  uint32_t ap_raises() const { return this->ap_raises_; }
  uint32_t quiet_starts() const { return this->quiet_starts_; }

  // ── triggers ──
  void add_on_client_connected(std::function<void(std::string)> &&cb) {
    this->on_client_connected_.add(std::move(cb));
  }
  void add_on_client_disconnected(std::function<void(std::string)> &&cb) {
    this->on_client_disconnected_.add(std::move(cb));
  }
  void add_on_ap_restored(std::function<void()> &&cb) { this->on_ap_restored_.add(std::move(cb)); }
  void add_on_portal_start(std::function<void()> &&cb) { this->on_portal_start_.add(std::move(cb)); }
  void add_on_portal_stop(std::function<void()> &&cb) { this->on_portal_stop_.add(std::move(cb)); }
  void add_on_landing(std::function<void(std::string, std::string)> &&cb) { this->on_landing_.add(std::move(cb)); }
  void add_on_enter(std::function<void(std::string, std::string)> &&cb) { this->on_enter_.add(std::move(cb)); }

 protected:
  bool want_active_() const { return this->active_lambda_ ? this->active_lambda_() : this->active_; }

  // ESP-IDF event task -> loop(): only a queue, no state is touched there.
  // The web server task -> loop() uses the same queue for the hooks.
  struct Event {
    uint8_t type;  // EV_*
    uint8_t mac[6];
    uint32_t ip;      // network byte order
    char path[48];    // EV_LANDING
  };
  enum : uint8_t { EV_JOIN = 1, EV_LEAVE, EV_AP_STOP, EV_IP, EV_SCAN_DONE, EV_LANDING, EV_ENTER };
  static void on_wifi_event_(void *arg, esp_event_base_t base, int32_t id, void *data);
  static void on_ip_event_(void *arg, esp_event_base_t base, int32_t id, void *data);
  void post_(const Event &ev);
  void register_events_();
  void drain_events_();

  bool ap_up_() const;
  void keep_ap_(uint32_t now);
  void stop_ap_();
  void configure_dhcp_();
  void dns_tick_();
  void dns_close_();
  void quiet_tick_(uint32_t now, bool router);
  void lock_apply_(uint8_t channel);
  void lock_release_();
  uint32_t ap_ip_be_() const;

  // HTTP
  struct Classified {
    ap_portal_core::Route route{ap_portal_core::Route::PASS};
    ap_portal_core::Claim claim{ap_portal_core::Claim::NONE};
    uint32_t client_ip{0};
    std::string path;
  };
  Classified classify_(AsyncWebServerRequest *request);
  void send_landing_(AsyncWebServerRequest *request, const Classified &c);
  void send_redirect_(AsyncWebServerRequest *request, const std::string &location);
  void forward_(AsyncWebServerRequest *request);
  ap_portal_core::Vars page_vars_(const Classified &c);
  void refresh_lambda_vars_(uint32_t now);

  // scan, password
  void scan_start_();
  void apply_password_(const std::string &password, bool save);

  QueueHandle_t events_{nullptr};
  bool events_registered_{false};
  std::atomic<uint32_t> ap_stop_ms_{0};

  bool ap_was_up_{false};
  bool ap_stopped_by_esphome_{false};
  bool dhcp_done_{false};
  uint32_t ap_raises_{0};

  int dns_sock_{-1};
  uint32_t dns_bound_ip_{0};

  bool quiet_ours_{false};
  uint32_t quiet_starts_{0};
  uint32_t router_since_{0};
  uint32_t boot_ms_{0};
  uint8_t ap_channel_{0};  // the router's last channel; the access point is raised on it
  ap_portal_core::ChannelLock lock_;
  bool country_saved_{false};
  wifi_country_t country_orig_{};

  bool active_{true};
  std::function<bool()> active_lambda_;
  bool ssid_suffix_mac_{false};
  bool password_persist_{false};
  std::vector<std::string> log_exclude_prefixes_;
  bool dns_enabled_{true};
  ap_portal_core::RouteConfig route_;

  // Shared with the web server task, under mu_.
  mutable std::mutex mu_;
  ap_portal_core::PassTable passes_;
  ap_portal_core::ClientTable clients_;
  ap_portal_core::Vars lambda_snap_;
  Classified cls_;  // canHandle -> handleRequest of the same request (web server task only)

  std::string html_;  // the template; empty = built-in page
  const uint8_t *html_gz_{nullptr};
  size_t html_gz_len_{0};
  ap_portal_core::Vars static_vars_;
  std::vector<std::pair<std::string, std::function<std::string()>>> lambda_vars_;
  uint32_t lambda_refresh_ms_{0};
  std::string ssid_snap_, host_, title_;  // fixed after setup()

  bool running_{false};
  bool scanning_{false};
  bool scan_suppressing_{false};
  uint32_t scan_started_ms_{0};
  uint32_t scan_retry_at_{0};

  std::string firmware_password_;
  bool custom_password_{false};
  bool password_pending_{false};
  bool password_pending_save_{false};
  std::string password_pending_value_;
  uint32_t password_apply_at_{0};
  ESPPreferenceObject password_pref_;

  CallbackManager<void(std::string)> on_client_connected_;
  CallbackManager<void(std::string)> on_client_disconnected_;
  CallbackManager<void()> on_ap_restored_;
  CallbackManager<void()> on_portal_start_;
  CallbackManager<void()> on_portal_stop_;
  CallbackManager<void(std::string, std::string)> on_landing_;
  CallbackManager<void(std::string, std::string)> on_enter_;
};

// ── triggers ──
class ClientConnectedTrigger : public Trigger<std::string> {
 public:
  explicit ClientConnectedTrigger(ApPortal *parent) {
    parent->add_on_client_connected([this](std::string mac) { this->trigger(mac); });
  }
};
class ClientDisconnectedTrigger : public Trigger<std::string> {
 public:
  explicit ClientDisconnectedTrigger(ApPortal *parent) {
    parent->add_on_client_disconnected([this](std::string mac) { this->trigger(mac); });
  }
};
class ApRestoredTrigger : public Trigger<> {
 public:
  explicit ApRestoredTrigger(ApPortal *parent) {
    parent->add_on_ap_restored([this]() { this->trigger(); });
  }
};
class PortalStartTrigger : public Trigger<> {
 public:
  explicit PortalStartTrigger(ApPortal *parent) {
    parent->add_on_portal_start([this]() { this->trigger(); });
  }
};
class PortalStopTrigger : public Trigger<> {
 public:
  explicit PortalStopTrigger(ApPortal *parent) {
    parent->add_on_portal_stop([this]() { this->trigger(); });
  }
};

class LandingTrigger : public Trigger<std::string, std::string> {
 public:
  explicit LandingTrigger(ApPortal *parent) {
    parent->add_on_landing([this](std::string ip, std::string path) { this->trigger(ip, path); });
  }
};
class EnterTrigger : public Trigger<std::string, std::string> {
 public:
  explicit EnterTrigger(ApPortal *parent) {
    parent->add_on_enter([this](std::string mac, std::string ip) { this->trigger(mac, ip); });
  }
};

// ── actions ──
template<typename... Ts> class ScanAction : public Action<Ts...> {
 public:
  explicit ScanAction(ApPortal *parent) : parent_(parent) {}
  void play(const Ts &...x) override { this->parent_->scan(); }

 protected:
  ApPortal *parent_;
};

template<typename... Ts> class SetPasswordAction : public Action<Ts...> {
 public:
  explicit SetPasswordAction(ApPortal *parent) : parent_(parent) {}
  TEMPLATABLE_VALUE(std::string, password)
  void play(const Ts &...x) override { this->parent_->set_password(this->password_.value(x...)); }

 protected:
  ApPortal *parent_;
};

template<typename... Ts> class ResetPasswordAction : public Action<Ts...> {
 public:
  explicit ResetPasswordAction(ApPortal *parent) : parent_(parent) {}
  void play(const Ts &...x) override { this->parent_->reset_password(); }

 protected:
  ApPortal *parent_;
};

}  // namespace esphome::ap_portal
