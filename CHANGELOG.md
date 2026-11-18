# Changelog

Versions stay 0.x until all three devices (motion sensor, HiveScale, Deye monitor) run on the library.

## Unreleased

### Added
- Stage 4: the landing page. Redirect / page / gate, a small template engine (`{{var}}`,
  `{{{raw}}}`, `{{#if}}` / `{{#unless}}` / `{{else}}`) for the built-in page and your own `html`,
  own variables from strings or lambdas (`vars:`), hooks `on_landing` and `on_enter`. While
  `captive_portal` runs, every GET from the access point goes through `ap_portal`: the device's
  pages still work, ESPHome's Wi-Fi form and `/wifisave` are not reachable. Gate passes by MAC + IP.
- `ap_portal.scan` (background scan with the router, ESPHome's scan without), `set_password`
  (applied 2 s after the answer, kept with `password_persist`), `reset_password`,
  `has_custom_password()`.
- Stage 3, the core: the access point stays up next to the router connection (ESPHome's fallback
  access point is switched off, ours is raised again after ESPHome turns it off), `manual_ip` and
  DHCP (.100–.110, DNS = access point) applied to it, own DNS on the access point address,
  client join/leave events (`on_client_connected` / `on_client_disconnected`, `on_ap_restored`,
  `on_portal_start` / `on_portal_stop`), `active:` switches everything on and off.
- Quiet mode: the access point up and no router (with or without clients) -> `captive_portal` is started so ESPHome
  stops full scans and adapter restarts; the radio is locked to the access point's channel with a
  20 s full search every 3 min; off 3 s after the router is back. The access point is raised on the
  router's last channel, never on the channel the radio happens to scan. After boot ESPHome gets
  30 s to find the router first; until the router was connected once there is no channel lock.
- `ssid_suffix_mac` applied before Wi-Fi starts.
- Component skeleton: configuration schema, build checks, triggers and actions (`ap_portal.scan`,
  `ap_portal.set_password`, `ap_portal.reset_password`). The access point itself is not managed yet.
- `ap_portal_core.h`: request routing (redirect / page / gate), per-client gate passes,
  DNS answers, quiet-mode and channel-lock decisions, timers that survive the `millis()` wrap.
- Host tests for the core, config tests for every build check, CI on ESPHome 2026.10.2.
- Examples: hello world, motion sensor (service mode), HiveScale (apiary, no router), Deye (gate).
