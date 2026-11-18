# ap_portal for ESPHome

**English** · [Українська](README.uk.md) · [Русский](README.ru.md)

> **Status: 0.x.** The access point, quiet mode, landing page, scan and password work and are
> tested on an ESP32-S3; moving the first real devices onto the library is next. Until a tagged
> release, pin a commit if you depend on it.

Your ESP32 keeps **its own Wi-Fi access point next to the router connection**. A phone that joins
it gets the **"Sign in to network"** window and lands on your device page — with or without a
router.

What it does that ESPHome's `captive_portal` does not:

- the access point stays up after the device has connected to the router;
- your own landing page: a redirect, a page, or a **gate** (the panel opens only after a button);
- requests from the home network are never touched — only clients of the access point see the landing;
- **quiet mode**: with no router, ESPHome normally scans every channel and restarts the adapter,
  and phones on the access point lose packets or drop (measured: 45 % loss). `ap_portal` keeps the
  radio on the access point's channel instead (measured: under 2 % loss, with the router coming back) and still finds the router when
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
  - source: github://daimonkor/esphome-ap-portal@main   # a tag once v0.1.0 is out
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
  api:                          # /ap_portal/ endpoints + client.js for your own site
    enabled: true
    from_home: false            # also from the home network (behind web_server's login)
  router_fallback:              # a network saved from the page that does not connect after a restart:
    timeout: 45s                # wait this long, then each step below in turn
    previous: true              # back to the network the device was on before
    firmware: true              # then to the network from the YAML (wifi: ssid / networks)
    # firmware: {ssid: Backup-Net, password: !secret backup}   # or a fallback network of its own
  on_router_fallback:           # to: "previous" / "firmware" / "none", ssid
  wifi_page:                    # built-in /wifi page, access point clients only (gate: after the button)
    enabled: true               # scan list, connect to a router network, the access point password
    path: /wifi                 # the landing page's second button leads here unless you set `secondary`
    ap_password: true           # the access point password block
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

### The Wi-Fi page

`/wifi` lists the networks around (a background scan while the router is connected), connects the
device to a router network (saved by ESPHome, kept across reboots) and changes the access point
password. Only clients of the access point get it — never the home network — and in gate mode only
after the button. Its endpoints live under `/ap_portal/`. Turn it off with `wifi_page: {enabled:
false}` when the device has its own Wi-Fi settings page.

### API for your own site

Your device's own site can draw Wi-Fi settings in its own style (a popup in a corner) and leave the
work to the library: include `/ap_portal/client.js` and call `window.apPortal`.

```html
<script src="/ap_portal/client.js"></script>
<script>
apPortal.networks().then(s => render(s.networks, s.connected && s.ssid));   // your popup
apPortal.scan();
apPortal.connect(ssid, psk).then(r => {
  showToast('Connecting…', r.addresses);        // http://<host>.local/ and the access point address
  apPortal.watch(ssid, {host: r.host})          // resolves once the device answers on the new network
    .then(x => showToast('Open', x.url));
});
apPortal.setApPassword('newpass99'); apPortal.resetApPassword();
</script>
```

| Endpoint | |
|---|---|
| `GET /ap_portal/networks` | `{scanning, connected, ssid, ip, rssi, host, ap_ip, ap_ssid, ap_active, networks:[{ssid, rssi, lock}]}` |
| `POST /ap_portal/scan` | start a scan |
| `POST /ap_portal/connect` | `ssid`, `psk` — saved by ESPHome, applied 2 s after the answer |
| `POST /ap_portal/ap_password` | `password` (empty or 8..63) |
| `POST /ap_portal/ap_password_reset` | back to the password from `wifi: ap:` |

A ready popup for any page: `<script src="/ap_portal/popup.js" data-accent="#3d7bff"></script>`
adds a Wi-Fi button in the corner (Shadow DOM, the site's styles do not touch it). ESPHome's own
page cannot load extra scripts with `local: true`; there `wifi_page: {ui_button: true}` serves `/`
to access point clients as a frame around ESPHome's page with that button on top.

Errors come back as `400` with `{"error": "<code>"}` and reject the promise with `Error.code`:
`ssid_empty`, `ssid_long` (over 32 bytes), `psk_length` (not empty and not 8..64),
`ap_password_length` (not empty and not 8..63 printable). `apPortal.check.ssid(s)`, `.psk(p)`,
`.apPassword(p)` apply the same rules before sending (`''` = fine), so your popup can show its own
text next to the field.

By default only access point clients reach the API. `api: {from_home: true}` opens it to the home
network too, behind `web_server`'s login if it has one. `wifi_page: {enabled: false}` keeps the API
and drops the built-in page.

### Forgot the access point password

With `password_persist: true` a password set from the page survives reboots and reflashing, so
there is a way back that needs no network:

- press **RESET** on the board 4 times, waiting 2-3 s after each press (each start must stay
  under 10 s) — the 4th start comes up with the password from `wifi: ap:`;
- or hold **BOOT** for 5 s while the device runs.

Every quick start also counts towards ESPHome's `safe_mode` (10 starts under 60 s), so between
two series let the device run for a minute. Only power-on and RESET-button starts count: deep-sleep wake-ups, OTA and software restarts,
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
