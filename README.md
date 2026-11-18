# ap_portal for ESPHome

> **Status: 0.x, under construction.** The configuration, build checks and the decision logic
> are in place and tested; the access point management itself lands in the next stages.
> Do not use it on a device you depend on yet.

Your ESP32 keeps **its own Wi-Fi access point next to the router connection**. A phone that joins
it gets the **"Sign in to network"** window and lands on your device page — with or without a
router.

What it does that ESPHome's `captive_portal` does not:

- the access point stays up after the device has connected to the router;
- your own landing page: a redirect, a page, or a **gate** (the panel opens only after a button);
- requests from the home network are never touched — only clients of the access point see the landing;
- **quiet mode**: with no router, ESPHome normally scans every channel and restarts the adapter,
  and phones on the access point lose packets or drop (measured: 45 % loss). `ap_portal` keeps the
  radio on the access point's channel instead (measured: 0.3 % loss) and still finds the router when
  it comes back, even on another channel;
- the SSID can carry the MAC suffix, the password can be changed from the device and survives reboots.

Works on any ESP32 with the ESP-IDF framework (tested on ESP32-C3 and ESP32-S3).
Tested with **ESPHome 2026.10.x**; the build warns on other versions.

## Quick start

```yaml
wifi:
  ssid: my-router
  password: router-password
  ap:
    ssid: My-Device
    password: device1234
    manual_ip:                  # 100.64.4.1: Android shows the sign-in window (a private address does not)
      static_ip: 100.64.4.1
      gateway: 100.64.4.1
      subnet: 255.255.255.0

web_server:
  version: 3
  local: true                   # the page works without internet

external_components:
  - source: github://daimonkor/esphome-ap-portal@v0.1.0
    components: [ap_portal]

ap_portal:
```

SSID, password and address come from `wifi: ap:` — they are never duplicated in `ap_portal`.

## Configuration

```yaml
ap_portal:
  id: portal
  active: true                  # true / false / !lambda — when the access point, DNS and page run
  ssid_suffix_mac: false        # MyDevice -> MyDevice-4FD30C
  password_persist: false       # keep the password set by ap_portal.set_password across reboots
  log_exclude_prefixes: []      # paths not logged at VERBOSE, e.g. [/sd/]
  dns:
    enabled: true
  landing:
    mode: redirect              # redirect (302) / page (200) / gate
    path: /welcome
    # built-in page:
    title: My Device            # default: friendly_name
    accent_color: "#3d7bff"
    icon_svg: {file: icon.svg}  # or inline "<svg ...>"
    primary_label: Open device page
    secondary: {label: Router Wi-Fi, href: "/#wifi"}
    # or your own page instead of all the keys above:
    # html: {file: landing.html}
    # gate only:
    # enter_path: /enter        # the button: grants this client a pass, then 302 to /
    # enter_window: 30min       # a pass ends after this long without requests
    # pass_paths: [/history.bin]  # never intercepted
  on_client_connected:          # mac (std::string)
  on_client_disconnected:       # mac
  on_ap_restored:               # ESPHome switched the access point off, ap_portal brought it back
  on_portal_start:
  on_portal_stop:
```

Actions: `ap_portal.scan` (background scan without dropping the router), `ap_portal.set_password`
(`password:` empty = open network, or 8..63 characters), `ap_portal.reset_password`.

From lambdas: `id(portal).is_active()`, `client_count()`, `ap_ip()`, `ssid()`, `is_scanning()`.

## Build checks

| Situation | Result |
|---|---|
| Framework is not ESP-IDF | error |
| No `wifi: ap:` | error |
| No `manual_ip` in `wifi: ap:`, or a private address | warning: Android will not show the sign-in window |
| SSID + MAC suffix longer than 32 bytes | error |
| `html` together with the built-in page keys | error |
| `enter_path`, `enter_window`, `pass_paths` without `mode: gate` | error |
| ESPHome other than 2026.10.x | warning |

## Examples

- [`examples/hello_world.yaml`](examples/hello_world.yaml) — the smallest config
- [`examples/motion_sensor.yaml`](examples/motion_sensor.yaml) — access point only in service mode, password from the page
- [`examples/hivescale.yaml`](examples/hivescale.yaml) — beehive scale at the apiary, usually no router
- [`examples/deye.yaml`](examples/deye.yaml) — battery monitor, gate mode, must work during power outages

## Development

```sh
make -C tests/host                          # core logic on your computer
pip install "esphome==2026.10.2" pytest
python3 -m pytest tests/config              # examples + every build check
```
