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
    # variables of the built-in page (usable in your own html too):
    title: My Device            # default: friendly_name
    accent_color: "#3d7bff"     # button text turns white or black by contrast
    icon_svg: {file: icon.svg}  # or inline "<svg ...>"
    primary_label: Open device page
    secondary: {label: Router Wi-Fi, href: "/#wifi"}
    # your own page instead of the built-in one (a template, see below):
    # html: {file: landing.html}
    # vars:                     # your own template variables
    #   battery: !lambda return to_string((int) id(soc).state);
    #   fw: "1.2.3"
    # gate only:
    # enter_path: /enter        # the button: grants this client a pass, then 302 to /
    # enter_window: 30min       # a pass ends after this long without requests
    # pass_paths: [/history.bin]  # never intercepted
  on_client_connected:          # mac (std::string)
  on_client_disconnected:       # mac
  on_ap_restored:               # ESPHome switched the access point off, ap_portal brought it back
  on_portal_start:
  on_portal_stop:
  password_reset:               # with password_persist: back to the password from wifi: ap:
    power_cycles: 4             # RESET button (or power) 4 times in a row, each start under 10 s; 0 = off
    # pin: GPIO2                # a button held while running; default: the board's BOOT button
    hold: 5s                    # (GPIO0 on ESP32/S2/S3, GPIO9 on C3/C6/H2); pin: false = off
  on_power_cycle:               # count — a quick start in a series (blink an LED)
  on_password_reset:            # source: "power_cycles" or "button"
  on_landing:                   # ip, path — the landing page was shown
  on_enter:                     # mac, ip — gate: a client pressed the button
```

Actions: `ap_portal.scan` (background scan without dropping the router), `ap_portal.set_password`
(`password:` empty = open network, or 8..63 characters; applied 2 s later so the page that asked
gets its answer first; kept across reboots with `password_persist: true`),
`ap_portal.reset_password` (back to the password from `wifi: ap:`, the saved one is erased).

From lambdas: `id(portal).is_active()`, `client_count()`, `ap_ip()`, `ssid()`, `is_scanning()`,
`has_custom_password()`.

### Forgot the access point password

With `password_persist: true` a password set from the page survives reboots and reflashing, so
there is a way back that needs no network:

- press **RESET** on the board 4 times, waiting 2-3 s after each press (each start must stay
  under 10 s) — the 4th start comes up with the password from `wifi: ap:`;
- or hold **BOOT** for 5 s while the device runs.

Only power-on and RESET-button starts count: deep-sleep wake-ups, OTA and software restarts,
crashes and brown-outs never do. If the BOOT pin is used by something else in your
configuration, holding BOOT is switched off (the build says so); set `pin:` to your own button.

### Landing page templates

The built-in page and your own `html` go through the same small template engine:

| Syntax | Meaning |
|---|---|
| `{{name}}` | the variable, HTML-escaped |
| `{{{name}}}` | the variable as is (an SVG, a prepared fragment) |
| `{{#if name}}…{{else}}…{{/if}}` | shown when the variable is not empty, `0` or `false`; nests |
| `{{#unless name}}…{{/unless}}` | the opposite |

Built-in variables: `title`, `ap_ip`, `sta_ip` (empty without the router), `host` (for
`http://{{host}}.local/`), `ssid`, `enter` (`enter_path` in gate mode, otherwise `/`), `landing`,
`mode`, `clients`, plus the styling keys above (`accent_color`, `accent_text`, `icon_svg`,
`primary_label`, `secondary_label`, `secondary_href`). Lambdas in `vars:` run in the main loop once
a second, never in the web server task. A page without `{{` is sent gzip-compressed from flash.

```html
<h1>{{title}}</h1>
<p>Battery {{battery}} %</p>
{{#if sta_ip}}<a href="http://{{sta_ip}}/">Home network</a>{{/if}}
<a href="{{enter}}">Open the panel</a>
```

While `captive_portal` runs (quiet mode without a router), every GET from the access point goes
through `ap_portal`: the device's pages work as usual, ESPHome's Wi-Fi form is never shown.

## Build checks

| Situation | Result |
|---|---|
| Framework is not ESP-IDF | error |
| No `wifi: ap:` | error |
| No `manual_ip` in `wifi: ap:`, or a private address | warning: Android will not show the sign-in window |
| SSID + MAC suffix longer than 32 bytes | error |
| `password_reset: power_cycles` not below `safe_mode: num_attempts` | error |
| `password_reset` BOOT pin used elsewhere | warning, holding BOOT is off |
| A `vars:` name that is not `[a-z_][a-z0-9_]*` or is a built-in variable | error |
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
