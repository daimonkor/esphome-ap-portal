#pragma once
// ap_portal core: the decisions the component makes, with no ESP-IDF or ESPHome dependencies,
// so they can be tested on a computer (tests/host). The component (ap_portal.cpp) feeds them
// real requests, packets and times.
//
//  * timers       — millis() arithmetic that survives the 49-day wrap
//  * routing      — which HTTP GET the portal answers and how (redirect / page / gate)
//  * passes       — the per-client "pass" of gate mode (MAC + IP, sliding window)
//  * dns          — the answer to a DNS query: every A record points at the access point
//  * quiet mode   — when ESPHome must stop scanning (client on the AP, no router) and
//                   when the radio may be locked to the AP channel
//  * strings      — SSID with a MAC suffix, AP password rules

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace ap_portal_core {

// ───────────────────────────── timers ─────────────────────────────
// Unsigned subtraction is correct across the millis() wrap; comparisons of absolute times are not.
inline bool elapsed(uint32_t now, uint32_t since, uint32_t ms) { return (uint32_t) (now - since) >= ms; }
// "now has reached until" for a deadline stored as an absolute time.
inline bool reached(uint32_t now, uint32_t until) { return (int32_t) (now - until) >= 0; }

// ───────────────────────────── routing ─────────────────────────────
enum class Mode : uint8_t { REDIRECT, PAGE, GATE };

enum class Route : uint8_t {
  PASS,      // not ours: web_server / the device's own pages answer
  LANDING,   // answer with the landing page (200)
  REDIRECT,  // 302 to the landing page
  ENTER,     // gate: grant this client a pass, then 302 to "/"
  WIFI,      // the built-in Wi-Fi page (router network, access point password)
  API,       // /ap_portal/... — JSON and form endpoints of the Wi-Fi page
  WRAP,      // "/" for an access point client: ESPHome's page in a frame + the Wi-Fi button
};

// The frame inside the wrapper asks for "/?" WRAP_RAW_ARG, which goes to web_server as usual.
constexpr const char *WRAP_RAW_ARG = "esp";

constexpr const char *API_PREFIX = "/ap_portal/";

struct Request {
  bool get{true};        // only GET is ever intercepted
  bool from_ap{false};   // the request arrived on the access point interface
  std::string host;      // Host header, may be empty
  std::string path;      // path without the query string
  bool has_pass{false};  // gate: this client already holds a valid pass
  bool raw{false};       // "/?esp": the wrapped page itself
};

struct RouteConfig {
  Mode mode{Mode::REDIRECT};
  std::string landing_path{"/welcome"};
  std::string enter_path{"/enter"};
  std::vector<std::string> pass_paths;  // gate: prefixes that are never intercepted
  std::string wifi_path;                // the built-in Wi-Fi page; empty = off
  bool api{true};                       // /ap_portal/ endpoints for access point clients
  bool wrap_root{false};                // ESPHome's own page gets the Wi-Fi button (ui_button)
};

// The connectivity checks of phones and computers. Android, iOS/macOS, Windows, Firefox, Kindle, ...
static const char *const PROBE_PATHS[] = {
    "/generate_204",  "/gen_204",         "/hotspot-detect.html", "/library/test/success.html",
    "/connecttest.txt", "/ncsi.txt",      "/canonical.html",      "/success.txt",
    "/check_network_status.txt",
};

inline bool is_probe_path(const std::string &path) {
  for (const char *p : PROBE_PATHS)
    if (path == p)
      return true;
  return false;
}

// A Host that is not this device: anything but an IP literal or a *.local name.
inline bool is_foreign_host(const std::string &host_header) {
  if (host_header.empty())
    return false;
  const std::string host = host_header.substr(0, host_header.find(':'));
  if (host.empty())
    return false;
  bool ip = true;
  for (char c : host) {
    if (!((c >= '0' && c <= '9') || c == '.')) {
      ip = false;
      break;
    }
  }
  if (ip)
    return false;
  static const char LOCAL[] = ".local";
  const size_t n = sizeof(LOCAL) - 1;
  if (host.size() > n && host.compare(host.size() - n, n, LOCAL) == 0)
    return false;
  // a trailing dot is a fully qualified name
  if (host.size() > n + 1 && host.compare(host.size() - n - 1, n + 1, ".local.") == 0)
    return false;
  return true;
}

inline bool has_prefix(const std::string &s, const std::string &prefix) {
  return !prefix.empty() && s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

// The single routing decision. Requests from the home network are never touched, so the
// device's own pages behave exactly as without the portal.
inline Route route_inner(const Request &r, const RouteConfig &c);

inline Route route(const Request &r, const RouteConfig &c) {
  const Route x = route_inner(r, c);
  if (x == Route::PASS && c.wrap_root && r.from_ap && r.get && r.path == "/" && !r.raw && c.api)
    return Route::WRAP;
  return x;
}

inline Route route_inner(const Request &r, const RouteConfig &c) {
  if (!r.from_ap)
    return Route::PASS;  // the home network never sees the portal, the Wi-Fi page included
  // The Wi-Fi page and its endpoints (GET and POST): access point clients only, in gate mode
  // only with a pass.
  const bool page = !c.wifi_path.empty() && r.path == c.wifi_path;
  const bool api = c.api && has_prefix(r.path, API_PREFIX);
  if (page || api) {
    if (c.mode == Mode::GATE && !r.has_pass)
      return r.get ? Route::LANDING : Route::PASS;
    return page ? (r.get ? Route::WIFI : Route::PASS) : Route::API;
  }
  if (!r.get)
    return Route::PASS;
  if (c.mode == Mode::GATE) {
    if (r.path == c.enter_path)
      return Route::ENTER;
    for (const auto &p : c.pass_paths)
      if (has_prefix(r.path, p))
        return Route::PASS;
    if (r.has_pass)
      return Route::PASS;
    return Route::LANDING;  // every GET, "/" included, until the client presses the button
  }
  if (r.path == c.landing_path)
    return Route::LANDING;
  if (is_probe_path(r.path) || is_foreign_host(r.host))
    return c.mode == Mode::REDIRECT ? Route::REDIRECT : Route::LANDING;
  return Route::PASS;
}

// ───────────────────────────── passes (gate) ─────────────────────────────
// A pass belongs to one client (MAC + IP), slides with every request and expires after `window`
// without requests. It is dropped when the client leaves the access point, so a new client that
// gets the same IP from DHCP does not inherit it.
struct Mac {
  uint8_t b[6]{};
  bool operator==(const Mac &o) const { return std::memcmp(b, o.b, 6) == 0; }
  bool zero() const {
    for (uint8_t x : b)
      if (x)
        return false;
    return true;
  }
};

class PassTable {
 public:
  static constexpr size_t CAPACITY = 8;

  explicit PassTable(uint32_t window_ms = 30u * 60u * 1000u) : window_(window_ms) {}
  void set_window(uint32_t ms) { this->window_ = ms; }
  uint32_t window() const { return this->window_; }

  // Grant (or refresh) the pass of this client. A full table drops the pass used longest ago.
  void grant(const Mac &mac, uint32_t ip, uint32_t now) {
    Slot *slot = nullptr;
    for (auto &s : this->slots_)
      if (s.used && ((!mac.zero() && s.mac == mac) || s.ip == ip)) {
        slot = &s;
        break;
      }
    if (slot == nullptr)
      for (auto &s : this->slots_)
        if (!s.used || !this->valid_(s, now)) {
          slot = &s;
          break;
        }
    if (slot == nullptr) {
      slot = &this->slots_[0];
      for (auto &s : this->slots_)
        if ((int32_t) (s.last - slot->last) < 0)
          slot = &s;
    }
    slot->used = true;
    slot->mac = mac;
    slot->ip = ip;
    slot->last = now;
  }

  // Does the client at this IP hold a valid pass? A valid pass slides.
  bool check(uint32_t ip, uint32_t now) {
    for (auto &s : this->slots_)
      if (s.used && s.ip == ip) {
        if (!this->valid_(s, now)) {
          s.used = false;
          return false;
        }
        s.last = now;
        return true;
      }
    return false;
  }

  // The client left the access point.
  void revoke(const Mac &mac) {
    for (auto &s : this->slots_)
      if (s.used && s.mac == mac)
        s.used = false;
  }

  // Passes granted by IP alone (MAC never learned): dropped whenever a client leaves, since the
  // address may go to the next client.
  void revoke_unknown() {
    for (auto &s : this->slots_)
      if (s.used && s.mac.zero())
        s.used = false;
  }

  void clear() {
    for (auto &s : this->slots_)
      s.used = false;
  }

  size_t active(uint32_t now) const {
    size_t n = 0;
    for (const auto &s : this->slots_)
      if (s.used && this->valid_(s, now))
        n++;
    return n;
  }

 protected:
  struct Slot {
    bool used{false};
    Mac mac;
    uint32_t ip{0};
    uint32_t last{0};
  };
  bool valid_(const Slot &s, uint32_t now) const { return !elapsed(now, s.last, this->window_); }

  Slot slots_[CAPACITY];
  uint32_t window_;
};

// ───────────────────────────── dns ─────────────────────────────
// Answer one DNS query in place-independent form: copy the question and, for an A query,
// add one answer pointing at `ip_be` (network byte order). AAAA and everything else get an
// empty NOERROR, so phones fall back to IPv4 at once. Returns the answer length, or 0 when the
// packet is not a single-question query (and must be ignored).
constexpr uint32_t DNS_TTL_S = 30;

inline size_t dns_answer(const uint8_t *q, size_t n, uint8_t *out, size_t cap, uint32_t ip_be) {
  if (n < 12 || n > cap)
    return 0;
  if (q[2] & 0x80)  // already a response
    return 0;
  if ((q[2] & 0x78) != 0)  // opcode must be QUERY
    return 0;
  if (q[4] != 0 || q[5] != 1)  // exactly one question
    return 0;
  size_t p = 12;
  while (p < n && q[p] != 0) {
    if ((q[p] & 0xC0) != 0)  // no compression in a question
      return 0;
    p += (size_t) q[p] + 1;
  }
  if (p + 5 > n)
    return 0;
  const uint16_t qtype = (uint16_t) (q[p + 1] << 8 | q[p + 2]);
  const size_t qend = p + 5;  // zero label + QTYPE + QCLASS
  const bool a = qtype == 1;
  const size_t len = qend + (a ? 16 : 0);
  if (len > cap)
    return 0;
  if (out != q)
    std::memcpy(out, q, qend);
  out[2] = (uint8_t) (0x80 | (q[2] & 0x01));  // response, keep RD
  out[3] = 0x80;                              // RA, NOERROR
  out[6] = 0;
  out[7] = a ? 1 : 0;  // ANCOUNT
  out[8] = out[9] = out[10] = out[11] = 0;
  if (a) {
    const uint8_t *ip = reinterpret_cast<const uint8_t *>(&ip_be);
    const uint8_t ans[16] = {0xC0, 0x0C,  // pointer to the question name
                             0, 1, 0, 1,  // A, IN
                             (uint8_t) (DNS_TTL_S >> 24), (uint8_t) (DNS_TTL_S >> 16), (uint8_t) (DNS_TTL_S >> 8),
                             (uint8_t) DNS_TTL_S, 0, 4, ip[0], ip[1], ip[2], ip[3]};
    std::memcpy(out + qend, ans, sizeof(ans));
  }
  return len;
}

// ───────────────────────────── quiet mode ─────────────────────────────
// Without a router ESPHome scans all channels and restarts the adapter; the radio leaves the
// access point's channel and clients lose packets or drop (measured: 45 % loss). The lever is
// an active captive_portal: ESPHome then retries blindly without scans and without adapter
// restarts. Its blind connect attempts still probe every channel, so on top the radio is locked
// to the access point's channel, with a full search window now and then in case the router came
// back on another channel (measured: 0.3 % loss). Stage 1 of the plan, 2026-11-18.
constexpr uint32_t LOCK_WIDEN_EVERY_MS = 180000;  // full search every 3 min ...
constexpr uint32_t LOCK_WIDEN_FOR_MS = 20000;     // ... lasting 20 s

constexpr uint32_t QUIET_BOOT_GRACE_MS = 30000;  // after boot ESPHome gets this long to find the router

struct QuietInput {
  bool router_connected{false};
  bool ap_up{false};
  bool settled{false};  // the router was connected once since boot, or the boot grace is over
};

// Quiet mode is wanted whenever the access point is up and there is no router — with or without
// clients. Waiting for a client does not work: without quiet mode the radio is mostly on other
// channels, the access point barely beacons and nobody can join (seen 2026-11-18 on the S3).
// Right after boot ESPHome's normal search runs first, so the router is found in seconds.
inline bool quiet_wanted(const QuietInput &in) { return in.ap_up && in.settled && !in.router_connected; }

// The radio is locked only to a known router channel. Before the router was ever connected the
// channel is unknown; a lock to a guess (channel 1) hid the router for minutes (2026-11-18), so
// then quiet mode runs without the lock and ESPHome's blind attempts probe every channel.
inline bool lock_wanted(bool quiet, uint8_t router_channel) { return quiet && router_channel != 0; }

class ChannelLock {
 public:
  enum class Action : uint8_t { NONE, LOCK, RELEASE };

  // Call every loop. `quiet` = quiet mode is active; `channel` = the radio's current channel.
  Action update(bool quiet, uint8_t channel, uint32_t now) {
    if (!quiet) {
      this->armed_ = false;
      if (this->locked_) {
        this->locked_ = false;
        return Action::RELEASE;
      }
      return Action::NONE;
    }
    if (!this->armed_) {  // quiet mode just started: lock to where the access point is now
      if (channel == 0)
        return Action::NONE;
      this->armed_ = true;
      this->channel_ = channel;
      this->since_ = now;
      this->locked_ = true;
      return Action::LOCK;
    }
    if (this->locked_ && elapsed(now, this->since_, LOCK_WIDEN_EVERY_MS)) {
      this->locked_ = false;
      this->since_ = now;
      this->widens_++;
      return Action::RELEASE;
    }
    if (!this->locked_ && elapsed(now, this->since_, LOCK_WIDEN_FOR_MS)) {
      this->locked_ = true;
      this->since_ = now;
      return Action::LOCK;
    }
    return Action::NONE;
  }

  bool locked() const { return this->locked_; }
  uint8_t channel() const { return this->channel_; }
  uint32_t widens() const { return this->widens_; }

 protected:
  bool armed_{false};
  bool locked_{false};
  uint8_t channel_{0};
  uint32_t since_{0};
  uint32_t widens_{0};
};

// ───────────────────────────── strings ─────────────────────────────
constexpr size_t SSID_MAX = 32;

// "MoveSensorBLE" + MAC 14:c1:9f:4f:d3:0c -> "MoveSensorBLE-4FD30C" (last 3 bytes, upper case).
// The base is cut so the whole SSID fits in 32 bytes.
inline std::string ssid_with_suffix(const std::string &base, const uint8_t mac[6]) {
  static const char HEX[] = "0123456789ABCDEF";
  char suffix[8];
  suffix[0] = '-';
  for (int i = 0; i < 3; i++) {
    suffix[1 + i * 2] = HEX[mac[3 + i] >> 4];
    suffix[2 + i * 2] = HEX[mac[3 + i] & 0x0F];
  }
  suffix[7] = '\0';
  const size_t keep = SSID_MAX - 7;
  return (base.size() > keep ? base.substr(0, keep) : base) + suffix;
}

// An access point password is empty (open network) or 8..63 printable ASCII characters.
inline bool valid_ap_password(const std::string &p) {
  if (p.empty())
    return true;
  if (p.size() < 8 || p.size() > 63)
    return false;
  for (char c : p)
    if (c < 0x20 || c > 0x7E)
      return false;
  return true;
}

// ───────────────────────────── templates ─────────────────────────────
// The landing page is a template, the built-in page and your own `html` alike:
//   {{name}}                 the variable, HTML-escaped
//   {{{name}}}               the variable as is (an SVG icon, a prepared fragment)
//   {{#if name}}…{{else}}…{{/if}}, {{#unless name}}…{{/unless}}   nested blocks
// A variable is true when it is not empty, "0" or "false". Unknown variables are empty.
// Unclosed tags are copied as text, so a stray "{{" never eats the page.
using Vars = std::vector<std::pair<std::string, std::string>>;

inline const std::string *find_var(const Vars &vars, const std::string &name) {
  for (const auto &kv : vars)
    if (kv.first == name)
      return &kv.second;
  return nullptr;
}

inline bool truthy(const std::string *v) { return v != nullptr && !v->empty() && *v != "0" && *v != "false"; }

inline void html_escape_into(std::string &out, const std::string &s) {
  for (char c : s) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      case '\'': out += "&#39;"; break;
      default: out += c;
    }
  }
}

inline std::string trim(const std::string &s) {
  size_t a = 0, b = s.size();
  while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\n' || s[a] == '\r')) a++;
  while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\n' || s[b - 1] == '\r')) b--;
  return s.substr(a, b - a);
}

inline bool has_template_tags(const std::string &tpl) { return tpl.find("{{") != std::string::npos; }

inline std::string render(const std::string &tpl, const Vars &vars) {
  struct Block {
    bool parent;  // the enclosing block is shown
    bool cond;
    bool in_else;
    bool shown() const { return parent && (in_else ? !cond : cond); }
  };
  std::vector<Block> stack;
  auto shown = [&]() { return stack.empty() || stack.back().shown(); };
  std::string out;
  out.reserve(tpl.size() + 64);
  size_t i = 0;
  while (i < tpl.size()) {
    const size_t open = tpl.find("{{", i);
    if (open == std::string::npos) {
      if (shown()) out.append(tpl, i, std::string::npos);
      break;
    }
    if (shown()) out.append(tpl, i, open - i);
    const bool raw = tpl.compare(open, 3, "{{{") == 0;
    const char *close_tok = raw ? "}}}" : "}}";
    const size_t start = open + (raw ? 3 : 2);
    const size_t close = tpl.find(close_tok, start);
    if (close == std::string::npos) {  // unclosed: the rest is text
      if (shown()) out.append(tpl, open, std::string::npos);
      break;
    }
    const std::string tag = trim(tpl.substr(start, close - start));
    i = close + (raw ? 3 : 2);
    if (!raw && (tag.compare(0, 4, "#if ") == 0 || tag.compare(0, 8, "#unless ") == 0)) {
      const bool unless = tag[1] == 'u';
      const bool t = truthy(find_var(vars, trim(tag.substr(unless ? 8 : 4))));
      stack.push_back({shown(), unless ? !t : t, false});
    } else if (!raw && tag == "else") {
      if (!stack.empty()) stack.back().in_else = true;
    } else if (!raw && (tag == "/if" || tag == "/unless")) {
      if (!stack.empty()) stack.pop_back();
    } else if (shown()) {
      const std::string *v = find_var(vars, tag);
      if (v != nullptr) {
        if (raw) out += *v;
        else html_escape_into(out, *v);
      }
    }
  }
  return out;
}

// ───────────────────────────── HTTP claim ─────────────────────────────
// Our handler stands first. It answers its own routes; everything else goes on to web_server.
// While captive_portal runs (quiet mode) its handler would take every GET nobody else wants —
// the Wi-Fi form, /wifisave — so then we take every GET and hand it to the next handler that is
// not captive_portal, or answer 404 ourselves.
enum class Claim : uint8_t { NONE, OURS, FORWARD };

inline Claim claim(Route r, bool get, bool captive_active) {
  if (r != Route::PASS) return Claim::OURS;
  if (get && captive_active) return Claim::FORWARD;
  return Claim::NONE;
}

// ───────────────────────────── clients (IP -> MAC) ─────────────────────────────
// From IP_EVENT_AP_STAIPASSIGNED: the gate needs the MAC of the client that pressed the button,
// HTTP only knows its IP.
class ClientTable {
 public:
  static constexpr size_t CAPACITY = 8;
  void set(uint32_t ip, const Mac &mac) {
    Slot *slot = nullptr;
    for (auto &s : this->slots_)
      if (s.used && (s.ip == ip || s.mac == mac)) { slot = &s; break; }
    if (slot == nullptr)
      for (auto &s : this->slots_)
        if (!s.used) { slot = &s; break; }
    if (slot == nullptr) slot = &this->slots_[this->next_++ % CAPACITY];
    slot->used = true;
    slot->ip = ip;
    slot->mac = mac;
  }
  bool find(uint32_t ip, Mac &mac) const {
    for (const auto &s : this->slots_)
      if (s.used && s.ip == ip) { mac = s.mac; return true; }
    return false;
  }
  void remove(const Mac &mac) {
    for (auto &s : this->slots_)
      if (s.used && s.mac == mac) s.used = false;
  }
  void clear() {
    for (auto &s : this->slots_) s.used = false;
  }

 protected:
  struct Slot {
    bool used{false};
    uint32_t ip{0};
    Mac mac;
  };
  Slot slots_[CAPACITY];
  size_t next_{0};
};

// ───────────────────────────── password reset on the spot ─────────────────────────────
// Two ways back to the firmware password when the device is out of reach on the network:
//  * power cycles — N boots in a row that each ended within POWER_CYCLE_WINDOW_MS of uptime
//    (the RESET button or the power plug). Only power-on / external resets count: deep-sleep
//    wake-ups, software restarts (OTA, Restart), crashes and brown-outs never do.
//  * a pin — a button held for `hold` while the device runs.
constexpr uint32_t POWER_CYCLE_WINDOW_MS = 10000;

struct PowerCycleStep {
  uint8_t count;  // to store
  bool reset;     // reset the password now
};

// One boot. `stored` is the count kept in flash, `counted` whether this boot's reset reason
// counts, `needed` the number of cycles (0 = off).
inline PowerCycleStep power_cycle_step(uint8_t stored, bool counted, uint8_t needed) {
  if (needed == 0 || !counted)
    return {0, false};
  const uint8_t c = (uint8_t) (stored >= needed ? 1 : stored + 1);
  if (c >= needed)
    return {0, true};
  return {c, false};
}

// A button held down long enough; fires once per press.
class HoldDetector {
 public:
  explicit HoldDetector(uint32_t hold_ms = 5000) : hold_(hold_ms) {}
  void set_hold(uint32_t ms) { this->hold_ = ms; }
  // Returns true once, when the button has been down for `hold`.
  bool update(bool pressed, uint32_t now) {
    if (!pressed) {
      this->down_ = false;
      this->fired_ = false;
      return false;
    }
    if (!this->down_) {
      this->down_ = true;
      this->since_ = now;
    }
    if (!this->fired_ && elapsed(now, this->since_, this->hold_)) {
      this->fired_ = true;
      return true;
    }
    return false;
  }

 protected:
  uint32_t hold_;
  bool down_{false};
  bool fired_{false};
  uint32_t since_{0};
};

// ───────────────────────────── router connection errors ─────────────────────────────
// The reason of the last WIFI_EVENT_STA_DISCONNECTED, for the Wi-Fi page: why the new router
// network does not take. Our own leave (8, when the device switches networks) is no error.
inline const char *sta_error_kind(uint8_t reason) {
  switch (reason) {
    case 0:
    case 8:  // ASSOC_LEAVE: we left
      return "";
    case 2:    // AUTH_EXPIRE
    case 14:   // MIC_FAILURE
    case 15:   // 4WAY_HANDSHAKE_TIMEOUT
    case 202:  // AUTH_FAIL
    case 204:  // HANDSHAKE_TIMEOUT
      return "wrong_password";
    case 201:  // NO_AP_FOUND
    case 210:  // NO_AP_FOUND_W_COMPATIBLE_SECURITY
    case 211:  // NO_AP_FOUND_IN_AUTHMODE_THRESHOLD
    case 212:  // NO_AP_FOUND_IN_RSSI_THRESHOLD
      return "not_found";
    default:
      return "failed";
  }
}

// ───────────────────────────── access point only without a router (ap_mode: fallback) ─────────────────────────────
// Up once there has been no router for `after`. Router back: down only after nobody has been on the
// access point for `idle` — a phone in the middle of something is never thrown off.
// keep_ap_on_connect hides ESPHome's access point flag, which also switches off ESPHome's own
// "reboot when no router for reboot_timeout" (it only runs without an access point). This brings
// it back: reboot after `timeout_ms` without a router, but never while someone is on the access
// point — the timer starts over when the last client leaves. 0 = never.
class RebootTimer {
 public:
  explicit RebootTimer(uint32_t timeout_ms = 0) : timeout_(timeout_ms) {}
  void set(uint32_t timeout_ms) { this->timeout_ = timeout_ms; }
  uint32_t timeout() const { return this->timeout_; }
  bool update(bool router, uint32_t clients, uint32_t now) {
    if (this->timeout_ == 0 || router || clients > 0) {
      this->since_ = 0;
      return false;
    }
    if (this->since_ == 0)
      this->since_ = now ? now : 1;
    return elapsed(now, this->since_, this->timeout_);
  }
  bool running() const { return this->since_ != 0; }
 protected:
  uint32_t timeout_;
  uint32_t since_{0};
};

class FallbackAp {
 public:
  FallbackAp(uint32_t after_ms = 30000, uint32_t idle_ms = 60000) : after_(after_ms), idle_(idle_ms) {}
  void set(uint32_t after_ms, uint32_t idle_ms) {
    this->after_ = after_ms;
    this->idle_ = idle_ms;
  }
  // Returns whether the access point should be up now.
  bool update(bool router, uint32_t clients, uint32_t now) {
    if (!router) {
      this->idle_since_ = 0;
      if (this->no_router_since_ == 0)
        this->no_router_since_ = now ? now : 1;
      if (!this->on_ && elapsed(now, this->no_router_since_, this->after_))
        this->on_ = true;
      return this->on_;
    }
    this->no_router_since_ = 0;
    if (!this->on_)
      return false;
    if (clients > 0) {
      this->idle_since_ = 0;
      return true;
    }
    if (this->idle_since_ == 0)
      this->idle_since_ = now ? now : 1;
    if (elapsed(now, this->idle_since_, this->idle_)) {
      this->on_ = false;
      this->idle_since_ = 0;
    }
    return this->on_;
  }
  bool on() const { return this->on_; }

 protected:
  uint32_t after_, idle_;
  uint32_t no_router_since_{0}, idle_since_{0};
  bool on_{false};
};

}  // namespace ap_portal_core
