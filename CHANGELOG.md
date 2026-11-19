# Changelog

Versions stay 0.x until all three devices (motion sensor, HiveScale, Deye monitor) run on the library.

## Unreleased

### Changed
- `ap_mode` is renamed to `ap_raise: always | without_router` (was `always | fallback`): next to
  ESPHome's own `wifi: ap: ap_mode: always_on` the two `ap_mode` keys were easy to mix up. The old
  name still works and maps to the new one with a warning at build time; both at once are refused.
  The `fallback: {after, off_when_idle}` block keeps its name.

## 0.1.3 — 2026-11-20

### Added
- Diagnostics in the log (DEBUG) for 60 s after a client joins the access point: the address it got
  from DHCP, every DNS question it asks (name and type) and every connectivity check
  (`captive.apple.com/hotspot-detect.html`, `/generate_204`, …) with the answer it gets
  (`redirect`, `landing`, `pass`). Shows at once whether a phone looked for the portal at all.

### Notes
- iPhone (iOS 26) seen with this: with «Auto-Login» on for the network, iOS finds the portal but does
  not open the sign-in window by itself (turning Auto-Login off opens it at once); for some private
  Wi-Fi addresses it does not check for a portal at all, even after «Forget This Network». Neither
  depends on the answer (302 and 200 behave the same). Put the access point address
  (`http://<access point IP>`) on the device label as a second QR code.

## 0.1.2 — 2026-11-19

### Changed
- One Wi-Fi action at a time. While the device scans, switches routers or applies a new access point
  password, the popup and the Wi-Fi page lock the network list, «Connect now», «Search again», the
  saved-network buttons and the access point password (the fields too while a switch runs), with a
  spinner and a «wait…» line; «Close» stays. The device refuses such requests itself with
  `409 {"error":"busy"}` (another tab or phone can't cut in); cancelling a saved network always works,
  a scan asked for during a scan is the same scan.
- jsdom tests for the popup and the Wi-Fi page locks in CI.

### Security
- Reading the API from another web page: ESPHome answers everything with
  `Access-Control-Allow-Origin: *`, so any site open in a browser on the home network (or on an
  access point client with mobile data) could read `/ap_portal/networks`, the landing and the Wi-Fi
  page — the router SSID, the device's addresses, the access point SSID, nearby networks. ap_portal's
  own answers now allow only the device's own pages: `http://<access point IP>`, `http://<host>.local`,
  `http://<router IP>` and the router IP before the last change (the page that switched routers polls
  `<host>.local` from the address it was opened on). Any other origin gets the access point's own
  origin back and the browser refuses the read; `Vary: Origin` keeps caches apart. ESPHome's own pages
  (`/`, `/events`, entity URLs) are not ap_portal's and keep ESPHome's header.

## 0.1.1 — 2026-11-19

### Security
- CSRF: any web page the user had open could send a plain form POST to `/ap_portal/connect`
  (switch the device to another router), `/ap_portal/ap_password` or the reset — from the home
  network with `api: {from_home: true}` (the browser sends a cached `web_server` login along) and
  from an access point client's browser. POSTs now need the header `X-AP-Portal: 1` (a browser sends
  it cross-site only after a CORS preflight, which the device refuses) and, when the browser names
  the page's `Origin`, the device's own address; `Sec-Fetch-Site: cross-site` is refused too.
  Everything else gets `403 {"error":"forbidden"}`. `client.js` / `popup.js` send the header, so
  pages using them need no change; your own `fetch`/`curl` calls must add it.

## 0.1.0 — 2026-11-19

### Changed
- The router password field is cleared after a successful connect or save, when a network is picked from the list, and when the popup opens or closes.
- Network list: the network the device is on is marked «✓ connected» and cannot be picked.
- Gate mode: the Wi-Fi page, popup and `/ap_portal/` API no longer wait for Enter — anyone on the
  access point can press it anyway; the access point password is the protection.
- Landing page in Android's sign-in window: "Open in the browser ↗" under the main button
  (`intent://`) with a note that the sign-in prompt won't come back and how to return (the access
  point address, or any `http://` address while on its Wi-Fi). iPhone/iPad/Mac sign-in window: a
  hint to open the address in Safari. The main button stays a plain link (opens the page in place).
- The landing page's Wi-Fi button opens the popup in place (no page change for Back to return to).
  `popup.js` takes `data-button="none"` (no corner button, `apPortalPopup.open()` only).
- Wi-Fi page and popup: "Device is on …"; polled every 5 s while open (4 s timeout) and on return
  to the tab, so a lost connection shows instead of a stale status.

### Security
- Addresses and error codes from `/ap_portal/networks` (also the one at `<host>.local`, which any
  device on the network can answer) and from the saved pending switch are escaped and checked
  (IPv4 only) before they reach the page; `esc()` escapes quotes for attributes.
- An access point client is told by its own address (inside the access point subnet), no longer by
  the local address it reached: a home host routing to the access point address no longer gets the
  access point API, an access point client reaching the router-side address no longer skips the gate.
- Gate passes of clients whose MAC was never learned: looked up in the DHCP leases; still unknown
  ones are dropped whenever a client leaves (the address may go to the next one) and no longer share
  one slot.

### Fixed
- Review 2026-11-19 (2 reviewers, 6 counter-checks):
  - ESPHome stores a scan list it takes while connected unsorted, and its first reconnect goes to entry 0 — any network. ap_portal now listens for every stored list (scan results listener) and puts the configured network first; a router switch waiting for its scan goes on from there (it acted one loop too early before, on the old list).
  - A scan list older than 60 s is not trusted for a switch (the BSSID / channel may have moved): a background scan runs first.
  - A newer switch request cancels an earlier one still waiting for its scan.
  - Early give-up needs the same clear answer (wrong password / not found) 3 times in a row, not 3 failures of any kind.
  - The page shows why a reverted switch failed (the reason was cleared by the reconnect to the old network).
  - A switch is a success only after a new association: a same-SSID switch (new router password) no longer reports success at once, and falls back to the old password if the new one fails.
  - With Wi-Fi disabled (a battery device outside service mode) the router-check and switch timers stop; nothing is saved over the chosen network.
  - A pending AP password change is applied on shutdown (a reboot right after `reset_password()` lost it).
  - «Apply saved network»: no stale «reverted», the switch counts as pending at once.
  - Log while away: recording stops when the device is back (later lines no longer push the switch out), replayed at WARN, no stack copy.
  - No logging while holding ESPHome's scan-results lock.
- A router switch with a wrong password (or a network that is not there) went back to the old network only after the whole fallback time; it now gives up after 3 such answers in a row, and the page reports the error when the device has gone back, not before.
- Router switch from the page failed every other time: ESPHome takes its first attempt from the first entry of its last scan list (unsorted after a background scan) and went to another access point. The new network's strongest entry is now put first in that list, so the device goes straight to it without a scan of its own; if the list lacks it, a background scan runs first (the access point keeps serving). An intermediate version dropped the list instead, and ESPHome's own scan made phones lose the access point.
- Builds without any `manual_ip` failed (`get_manual_ip` exists only with `USE_WIFI_MANUAL_IP`).
- The log names the failed attempts of a switch («connected 'X' in N ms after K failed attempt(s), reasons …»); the error on the page shows the reason code.
- A router switch from the page failed at once if an earlier switch had fallen back: the stale `reverted` flag is cleared when a new switch is requested; `pending` is also true while the request waits to start.
- Status while switching shows «Connecting to «X»…» (or «back to «Y»…») with a spinner instead of «Not connected to the router».
- Switching routers from the page no longer reports «connection failed» on a first failed attempt: the networks JSON says `pending` while the device still tries, and the page waits for the result (or the fallback); only a wrong password repeated for ~15 s is reported early.
- client.js / popup.js are sent with `Cache-Control: no-cache` and an ETag (build hash): after a reflash the phone takes the new script at once instead of keeping the old one for 5 minutes; unchanged scripts answer 304.
- No rectangular tap highlight on phones (landing, /wifi page, popup); a pressed button dims instead, focus rings only for the keyboard.
- The network list showed only the configured network: ap_portal now asks ESPHome to keep every scan record (and reads them under ESPHome's scan-results lock).
- "Search again" shows a spinner and is disabled while the device scans (popup and /wifi page).
- Crash (out of memory, `std::bad_alloc`) on an ESP32-C3 with BLE when a page loaded `popup.js`:
  it was built as one ~22 KB string and copied again by the response. `client.js` / `popup.js` are
  now gzip-compressed at build time and sent from flash (~8 KB, no heap); rendered pages and JSON
  are sent without the response's second copy, and templates are no longer copied before rendering.
- Changing the access point password (page, action, BOOT) made ESPHome switch the access point off
  again on the next router connection with `keep_ap_on_connect` (`set_ap()` restores ESPHome's flag).
- Reboot without a router: with an access point raised by ap_portal, ESPHome 2026.11 rebooted even
  with clients on it and 2026.10 never rebooted. ap_portal now owns it (same `wifi: reboot_timeout`,
  paused while anyone is on the access point), also while the portal is inactive; with
  `wifi: ap: ap_mode: always_on` ESPHome keeps it. **Devices without a router: `reboot_timeout: 0s`.**
- Without `web_server:` the HTTP listener ran only in quiet mode; ap_portal now keeps it running.
- `always_on`: ap_portal no longer raises the access point behind ESPHome or switches it off when
  `active` goes false (only the portal stops); the channel lock also applies when ESPHome started
  the captive portal itself.
- A switch from the Wi-Fi page during the after-boot router check could be overridden by that check.
- `ssid_suffix_mac` without `wifi: ap: ssid` gave an SSID of the suffix alone.
- BOOT pin detection missed bare pin numbers (I2C `sda`/`scl`, camera clocks, `*_pin`); the warning
  now comes at validation, not only at compile. ESP32-C5: GPIO28.
- The page's PSK check counted characters, the device counts bytes.
- JS: no lost errors on scan/apply/cancel, one polling chain at a time, no `AbortController`,
  `Promise.finally` or CSS `inset` needed (older captive WebViews), popup.js may sit in `<head>`.

### Added
- «Log while away»: during a router switch warnings and Wi-Fi / ap_portal lines are kept (20 lines) and written to the log again once the device is back and an API client listens — a failed switch can be read afterwards. The access point stopping under its clients is logged as a warning.
- «Save, connect later» shows only where the access point may drop during a router switch: hidden by default with `wifi: ap: ap_mode: always_on` or `keep_ap_on_connect`; `wifi_page: later_button` overrides.
- `wifi: ap: ap_mode: always_on` (ESPHome 2026.11) supported; WISP and conflicting options rejected.
- Rejected: `/` or `/ap_portal/…` as landing/enter/Wi-Fi page path, `/` in `pass_paths`,
  `ui_button` without `web_server`. Warned: `on_power_cycle`/`on_password_reset` without
  `password_persist`, keep_ap with `post_connect_roaming: true`.
- No version warning on ESPHome 2026.10.x and 2026.11.x (both checked on hardware).

### Changed
- Tested with ESPHome 2026.11.x (was 2026.10.x); CI on 2026.11.0. `keep_ap_on_connect` and its
  `reboot_timeout` checked on hardware (XIAO ESP32-S3) on both 2026.10.2 and 2026.11.0.

### Fixed
- ESPHome 2026.11 returns the access point's `manual_ip` by value; the access point address was
  read through a dangling reference. Now copied.
- After boot, quiet mode starts at the first failed router attempt instead of waiting out the 30 s
  grace: ESPHome restarted the adapter three times meanwhile and the access point blinked.
- Connecting to another router network from the Wi-Fi page did nothing while the device was
  connected: ESPHome's `save_wifi_sta()` only saves then. Now only the router connection is dropped
  after it (`esp_wifi_disconnect`) — the access point stays up, unlike `wifi.configure`'s
  disable/enable which sent phones off to another network. While switching, quiet mode stays but
  the channel is not locked (the new router may be elsewhere). If the new network does not connect
  within 30 s the previous one comes back (`previous`, `reverted` in `networks`; the toast says so).

### Added
- **Experimental** `keep_ap_on_connect:` — ESPHome no longer switches the access point off when the
  router connects (clears ESPHome's private `has_ap_` after Wi-Fi start; build warns outside
  ESPHome 2026.11.x). Own `reboot_timeout:` (default `wifi: reboot_timeout`) that waits while
  someone is on the access point. `ap_mode: always | fallback` with `fallback: {after, off_when_idle}`
  (fallback not yet checked on hardware).
- Passwords in `router_fallback: firmware:` and `ap_portal.set_password` marked `cv.sensitive`
  (ESPHome 2026.12 drops the name-based redaction).
- API errors as JSON codes (`{"error": "ssid_long"}` …), `Error.code` in client.js and
  `apPortal.check.*` with the device's rules; the popup and the Wi-Fi page check fields before
  sending and show the reason next to the field (en / uk / ru).
- `router_fallback:` — a network saved from the page that gives no router within `timeout` (45 s)
  after a restart: back to the previous network, then to the YAML one (each optional, each step
  `timeout` long), else "none" (the access point stays). Hook `on_router_fallback` (to, ssid). The
  in-session switch from the page uses the same timeout and fires the hook when it goes back.
  `firmware:` also takes `{ssid, password}` for a fallback network of its own.
- "Save, connect later" next to "Connect now": the network is saved (a restart uses it) but the
  device stays on its current network while someone is on the access point; it switches 10 s after
  the last client leaves, or on Connect now / `apPortal.apply()`; Cancel (`apPortal.cancel()`)
  puts the previous network back. `networks().later` shows what is waiting.
- On the device's own Wi-Fi the result card does not offer to open the new router address (it
  cannot be reached from there): it says to carry on and lets you copy the address
  (`watch()` resolves with `onAp`).
- Addresses on the landing page and the switching card are copied on tap ("Copied"), with a small
  ↗ to open: the phone's "Sign in to network" window cannot open addresses of another network.
- Show / hide toggle (eye) in the password fields of the Wi-Fi popup and page.
- Loading state while switching the router network: a full-screen layer over the popup / page
  (nothing underneath can be pressed) with a spinner, seconds counter, where to find the device and
  a hint that the phone may change networks; the result gets Open and Close.
- The Wi-Fi page and popup say why a router network does not take (wrong password, not found,
  failed — from the last disconnect reason, `target` / `sta_error` in `networks`), keep waiting
  for the device after the page was closed and opened again, scroll in the bottom sheet; the lock
  sits next to the network name; signal in dBm.
- `/ap_portal/popup.js`: a Wi-Fi button + popup for any page (Shadow DOM, `data-accent`), and
  `wifi_page: {ui_button: true}` to put it on ESPHome's own page for access point clients.
- API for the device's own site: `/ap_portal/client.js` (`window.apPortal`: networks, scan,
  connect, watch, setApPassword, resetApPassword) and `api: {from_home}` (behind web_server's
  login). After a router change the Wi-Fi page shows a toast with where to find the device
  (`<host>.local`, the access point) and, once it answers from the new network, an Open button.
- Built-in Wi-Fi page (`wifi_page:`, `/wifi`): scan list, connect to a router network
  (`save_wifi_sta`, applied 2 s after the answer), the access point password; access point clients
  only, in gate mode after the button. The landing page's second button leads there by default.
- Password reset on the spot (`password_reset:`): the RESET button 4 times in a row (each start
  under 10 s) or BOOT held 5 s while running brings back the password from `wifi: ap:`. Only
  power-on / RESET starts count. Hooks `on_power_cycle` (count) and `on_password_reset` (source).
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
