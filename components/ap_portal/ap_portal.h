#pragma once
// ap_portal — the device's own access point next to the router connection.
// Stage 3: access point keeper, DNS, DHCP, quiet mode with channel lock, client events.
// Stage 4 adds the landing page, gate and the actions (see ap_portal_core.h for the logic).

#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include <esp_event.h>
#include <esp_wifi_types.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "ap_portal_core.h"

namespace esphome::ap_portal {

class ApPortal : public Component {
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
  void set_title(const std::string &t) { this->title_ = t; }
  void set_accent_color(const std::string &c) { this->accent_color_ = c; }
  void set_icon_svg(const std::string &s) { this->icon_svg_ = s; }
  void set_primary_label(const std::string &l) { this->primary_label_ = l; }
  void set_secondary(const std::string &label, const std::string &href) {
    this->secondary_label_ = label;
    this->secondary_href_ = href;
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

 protected:
  bool want_active_() const { return this->active_lambda_ ? this->active_lambda_() : this->active_; }

  // ESP-IDF event task -> loop(): only a queue, no state is touched there.
  struct Event {
    uint8_t type;  // EV_*
    uint8_t mac[6];
  };
  enum : uint8_t { EV_JOIN = 1, EV_LEAVE = 2, EV_AP_STOP = 3 };
  static void on_wifi_event_(void *arg, esp_event_base_t base, int32_t id, void *data);
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
  ap_portal_core::PassTable passes_;
  std::string html_, title_, accent_color_{"#3d7bff"}, icon_svg_, primary_label_{"Open device page"};
  std::string secondary_label_, secondary_href_;

  bool running_{false};
  bool scanning_{false};

  CallbackManager<void(std::string)> on_client_connected_;
  CallbackManager<void(std::string)> on_client_disconnected_;
  CallbackManager<void()> on_ap_restored_;
  CallbackManager<void()> on_portal_start_;
  CallbackManager<void()> on_portal_stop_;
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
