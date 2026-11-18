# Changelog

Versions stay 0.x until all three devices (motion sensor, HiveScale, Deye monitor) run on the library.

## Unreleased

### Added
- Component skeleton: configuration schema, build checks, triggers and actions (`ap_portal.scan`,
  `ap_portal.set_password`, `ap_portal.reset_password`). The access point itself is not managed yet.
- `ap_portal_core.h`: request routing (redirect / page / gate), per-client gate passes,
  DNS answers, quiet-mode and channel-lock decisions, timers that survive the `millis()` wrap.
- Host tests for the core, config tests for every build check, CI on ESPHome 2026.10.2.
- Examples: hello world, motion sensor (service mode), HiveScale (apiary, no router), Deye (gate).
