#include "ap_portal.h"

#include "esphome/core/log.h"

namespace esphome::ap_portal {

static const char *const TAG = "ap_portal";

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

void ApPortal::setup() {
  // Stage 2: configuration only. Stage 3 adds the access point keeper, DNS, DHCP and quiet mode;
  // stage 4 the landing page and actions.
}

void ApPortal::loop() {}

void ApPortal::dump_config() {
  ESP_LOGCONFIG(TAG,
                "AP portal:\n"
                "  Active: %s\n"
                "  SSID suffix MAC: %s\n"
                "  Password persist: %s\n"
                "  DNS: %s\n"
                "  Landing mode: %s, path %s",
                this->active_lambda_ ? "lambda" : YESNO(this->active_), YESNO(this->ssid_suffix_mac_),
                YESNO(this->password_persist_), YESNO(this->dns_enabled_), mode_name(this->route_.mode),
                this->route_.landing_path.c_str());
  if (this->route_.mode == ap_portal_core::Mode::GATE) {
    ESP_LOGCONFIG(TAG, "  Enter path: %s, window %u min, pass paths: %u", this->route_.enter_path.c_str(),
                  (unsigned) (this->passes_.window() / 60000), (unsigned) this->route_.pass_paths.size());
  }
  ESP_LOGCONFIG(TAG, "  Custom HTML: %s (%u bytes)", YESNO(!this->html_.empty()), (unsigned) this->html_.size());
  ESP_LOGW(TAG, "Stage 2 build: configuration only, the access point is not managed yet");
}

void ApPortal::scan() { ESP_LOGW(TAG, "scan: not implemented yet (stage 4)"); }

void ApPortal::set_password(const std::string &password) {
  if (!ap_portal_core::valid_ap_password(password)) {
    ESP_LOGW(TAG, "set_password: rejected (empty or 8..63 printable characters)");
    return;
  }
  ESP_LOGW(TAG, "set_password: not implemented yet (stage 4)");
}

void ApPortal::reset_password() { ESP_LOGW(TAG, "reset_password: not implemented yet (stage 4)"); }

uint32_t ApPortal::client_count() const { return 0; }
std::string ApPortal::ap_ip() const { return {}; }
std::string ApPortal::ssid() const { return {}; }

}  // namespace esphome::ap_portal
