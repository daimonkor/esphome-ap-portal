// Host tests for ap_portal_core.h:  make -C tests/host
#include "../../components/ap_portal/ap_portal_core.h"

#include <cstdio>
#include <cstdlib>

using namespace ap_portal_core;

static int g_fail = 0, g_checks = 0;
#define CHECK(c)                                                 \
  do {                                                           \
    g_checks++;                                                  \
    if (!(c)) {                                                  \
      g_fail++;                                                  \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);   \
    }                                                            \
  } while (0)

// ------------------------------------------------------------------ timers
static void test_timers() {
  CHECK(elapsed(1000, 0, 1000));
  CHECK(!elapsed(999, 0, 1000));
  // across the millis() wrap
  CHECK(elapsed(500, 0xFFFFFE00u, 1000));
  CHECK(!elapsed(100, 0xFFFFFE00u, 1000));
  CHECK(reached(10, 10));
  CHECK(!reached(9, 10));
  CHECK(reached(5, 0xFFFFFFF0u));    // deadline before the wrap, now after it
  CHECK(!reached(0xFFFFFFF0u, 5));   // deadline after the wrap, now before it
}

// ------------------------------------------------------------------ routing
static Request req(bool from_ap, const char *host, const char *path, bool get = true, bool pass = false) {
  Request r;
  r.get = get;
  r.from_ap = from_ap;
  r.host = host;
  r.path = path;
  r.has_pass = pass;
  return r;
}

static void test_hosts() {
  CHECK(!is_foreign_host(""));
  CHECK(!is_foreign_host("100.64.4.1"));
  CHECK(!is_foreign_host("100.64.4.1:80"));
  CHECK(!is_foreign_host("192.168.1.132"));
  CHECK(!is_foreign_host("deye-battery.local"));
  CHECK(!is_foreign_host("deye-battery.local."));
  CHECK(!is_foreign_host("deye-battery.local:80"));
  CHECK(is_foreign_host("connectivitycheck.gstatic.com"));
  CHECK(is_foreign_host("captive.apple.com"));
  CHECK(is_foreign_host("local"));  // a bare word is not *.local
  CHECK(is_probe_path("/generate_204"));
  CHECK(is_probe_path("/hotspot-detect.html"));
  CHECK(!is_probe_path("/generate_204x"));
  CHECK(!is_probe_path("/"));
}

static void test_route_redirect_page() {
  RouteConfig c;
  c.mode = Mode::REDIRECT;
  // home network: never touched, not even probes
  CHECK(route(req(false, "connectivitycheck.gstatic.com", "/generate_204"), c) == Route::PASS);
  CHECK(route(req(false, "192.168.1.132", "/welcome"), c) == Route::PASS);
  // access point
  CHECK(route(req(true, "connectivitycheck.gstatic.com", "/generate_204"), c) == Route::REDIRECT);
  CHECK(route(req(true, "100.64.4.1", "/generate_204"), c) == Route::REDIRECT);
  CHECK(route(req(true, "example.com", "/anything"), c) == Route::REDIRECT);
  CHECK(route(req(true, "100.64.4.1", "/welcome"), c) == Route::LANDING);
  CHECK(route(req(true, "100.64.4.1", "/"), c) == Route::PASS);
  CHECK(route(req(true, "deye.local", "/events"), c) == Route::PASS);
  CHECK(route(req(true, "example.com", "/generate_204", false), c) == Route::PASS);  // POST
  c.mode = Mode::PAGE;
  CHECK(route(req(true, "100.64.4.1", "/generate_204"), c) == Route::LANDING);
  CHECK(route(req(true, "example.com", "/x"), c) == Route::LANDING);
  CHECK(route(req(true, "100.64.4.1", "/"), c) == Route::PASS);
}

static void test_route_gate() {
  RouteConfig c;
  c.mode = Mode::GATE;
  c.enter_path = "/dashboard";
  c.pass_paths = {"/history.bin"};
  CHECK(route(req(true, "100.64.4.1", "/"), c) == Route::LANDING);
  CHECK(route(req(true, "100.64.4.1", "/0.js"), c) == Route::LANDING);
  CHECK(route(req(true, "x.com", "/generate_204"), c) == Route::LANDING);
  CHECK(route(req(true, "100.64.4.1", "/dashboard"), c) == Route::ENTER);
  CHECK(route(req(true, "100.64.4.1", "/history.bin?from=0"), c) == Route::PASS);
  CHECK(route(req(true, "100.64.4.1", "/", true, true), c) == Route::PASS);
  CHECK(route(req(true, "100.64.4.1", "/dashboard", true, true), c) == Route::ENTER);  // re-grant is harmless
  CHECK(route(req(false, "192.168.1.132", "/"), c) == Route::PASS);  // home network
  RouteConfig empty;
  empty.mode = Mode::GATE;
  empty.pass_paths = {""};  // an empty prefix must not open everything
  CHECK(route(req(true, "100.64.4.1", "/"), empty) == Route::LANDING);
}

// ------------------------------------------------------------------ passes
static Mac mac(uint8_t last) {
  Mac m;
  m.b[0] = 0x14;
  m.b[5] = last;
  return m;
}

static void test_passes() {
  PassTable t(1000);
  CHECK(!t.check(101, 0));
  t.grant(mac(1), 101, 0);
  CHECK(t.check(101, 500));
  CHECK(t.check(101, 1400));   // slid at 500, valid until 1500
  CHECK(!t.check(101, 2400));  // no request for 1000 ms
  CHECK(!t.check(101, 2401));  // and it stays gone
  // revoke on leave, IP reuse does not inherit the pass
  t.grant(mac(2), 102, 3000);
  t.revoke(mac(2));
  CHECK(!t.check(102, 3001));
  // same client re-granted with a new IP: one slot, the old IP is gone
  t.grant(mac(3), 103, 4000);
  t.grant(mac(3), 113, 4001);
  CHECK(t.check(113, 4002));
  CHECK(!t.check(103, 4002));
  // capacity: the 9th client replaces the one used longest ago
  PassTable f(100000);
  for (uint8_t i = 0; i < PassTable::CAPACITY; i++)
    f.grant(mac(10 + i), 200 + i, 10 + i);
  CHECK(f.active(20) == PassTable::CAPACITY);
  f.check(200, 50);  // client 0 is fresh again; client 1 is now the oldest
  f.grant(mac(99), 299, 60);
  CHECK(f.check(299, 61));
  CHECK(f.check(200, 61));
  CHECK(!f.check(201, 61));
  // across the wrap
  PassTable w(1000);
  w.grant(mac(5), 105, 0xFFFFFF00u);
  CHECK(w.check(105, 0x00000100u));  // 512 ms later
  CHECK(!w.check(105, 0x00000600u)); // 1280 ms after the last request
}

// ------------------------------------------------------------------ dns
static size_t make_query(uint8_t *b, const char *name, uint16_t qtype, uint8_t flags2 = 0x01) {
  size_t n = 0;
  const uint8_t head[12] = {0x12, 0x34, flags2, 0x00, 0, 1, 0, 0, 0, 0, 0, 0};
  std::memcpy(b, head, 12);
  n = 12;
  const char *p = name;
  while (*p) {
    const char *dot = std::strchr(p, '.');
    size_t l = dot ? (size_t) (dot - p) : std::strlen(p);
    b[n++] = (uint8_t) l;
    std::memcpy(b + n, p, l);
    n += l;
    p += l + (dot ? 1 : 0);
  }
  b[n++] = 0;
  b[n++] = (uint8_t) (qtype >> 8);
  b[n++] = (uint8_t) qtype;
  b[n++] = 0;
  b[n++] = 1;
  return n;
}

static void test_dns() {
  uint8_t q[300], out[300];
  const uint8_t ipb[4] = {100, 64, 4, 1};
  uint32_t ip_be;
  std::memcpy(&ip_be, ipb, 4);

  size_t n = make_query(q, "connectivitycheck.gstatic.com", 1);
  size_t a = dns_answer(q, n, out, sizeof(out), ip_be);
  CHECK(a == n + 16);
  CHECK(out[0] == 0x12 && out[1] == 0x34);  // same id
  CHECK(out[2] == 0x81 && out[3] == 0x80);  // response, RD kept, RA, NOERROR
  CHECK(out[7] == 1);                       // one answer
  CHECK(std::memcmp(out + 12, q + 12, n - 12) == 0);
  CHECK(out[n] == 0xC0 && out[n + 1] == 0x0C);
  CHECK(out[n + 9] == DNS_TTL_S);
  CHECK(std::memcmp(out + n + 12, ipb, 4) == 0);

  n = make_query(q, "example.com", 28);  // AAAA: empty NOERROR
  a = dns_answer(q, n, out, sizeof(out), ip_be);
  CHECK(a == n);
  CHECK(out[7] == 0 && out[3] == 0x80);

  // answer in place, as the component does
  n = make_query(q, "a.b", 1);
  a = dns_answer(q, n, q, sizeof(q), ip_be);
  CHECK(a == n + 16 && std::memcmp(q + n + 12, ipb, 4) == 0);

  // ignored
  n = make_query(q, "x.com", 1);
  CHECK(dns_answer(q, 11, out, sizeof(out), ip_be) == 0);  // too short
  q[2] = 0x81;
  CHECK(dns_answer(q, n, out, sizeof(out), ip_be) == 0);  // a response
  n = make_query(q, "x.com", 1);
  q[5] = 2;
  CHECK(dns_answer(q, n, out, sizeof(out), ip_be) == 0);  // two questions
  n = make_query(q, "x.com", 1);
  CHECK(dns_answer(q, n - 2, out, sizeof(out), ip_be) == 0);  // truncated
  n = make_query(q, "x.com", 1);
  CHECK(dns_answer(q, n, out, n + 15, ip_be) == 0);  // no room for the answer
  n = make_query(q, "x.com", 1, 0x29);                // opcode 5 (UPDATE)
  CHECK(dns_answer(q, n, out, sizeof(out), ip_be) == 0);
  n = make_query(q, "x.com", 1);
  q[12] = 0xC0;  // compression pointer in the question
  CHECK(dns_answer(q, n, out, sizeof(out), ip_be) == 0);
}

// ------------------------------------------------------------------ quiet mode
static void test_quiet() {
  CHECK(quiet_wanted({false, true, true}));    // no router, access point up — clients do not matter
  CHECK(!quiet_wanted({true, true, true}));    // router connected
  CHECK(!quiet_wanted({false, false, true}));  // access point down
  CHECK(!quiet_wanted({false, true, false}));  // just booted, ESPHome still searching
  CHECK(lock_wanted(true, 9));
  CHECK(!lock_wanted(true, 0));   // router channel unknown — no lock to a guess
  CHECK(!lock_wanted(false, 9));

  ChannelLock l;
  CHECK(l.update(false, 7, 0) == ChannelLock::Action::NONE);
  CHECK(l.update(true, 0, 1) == ChannelLock::Action::NONE);  // channel unknown yet
  CHECK(l.update(true, 7, 2) == ChannelLock::Action::LOCK);
  CHECK(l.locked() && l.channel() == 7);
  CHECK(l.update(true, 7, 2 + LOCK_WIDEN_EVERY_MS - 1) == ChannelLock::Action::NONE);
  CHECK(l.update(true, 7, 2 + LOCK_WIDEN_EVERY_MS) == ChannelLock::Action::RELEASE);
  CHECK(!l.locked() && l.widens() == 1);
  const uint32_t w = 2 + LOCK_WIDEN_EVERY_MS;
  CHECK(l.update(true, 3, w + LOCK_WIDEN_FOR_MS - 1) == ChannelLock::Action::NONE);
  CHECK(l.update(true, 3, w + LOCK_WIDEN_FOR_MS) == ChannelLock::Action::LOCK);
  CHECK(l.channel() == 7);  // still the access point's channel from the start
  CHECK(l.update(false, 7, w + LOCK_WIDEN_FOR_MS + 1) == ChannelLock::Action::RELEASE);
  CHECK(l.update(false, 7, w + LOCK_WIDEN_FOR_MS + 2) == ChannelLock::Action::NONE);
  // next outage: lock to the new channel
  CHECK(l.update(true, 1, 0x10000000u) == ChannelLock::Action::LOCK);
  CHECK(l.channel() == 1);
  // quiet mode ends during a widen window: nothing to release
  ChannelLock m;
  m.update(true, 6, 0);
  m.update(true, 6, LOCK_WIDEN_EVERY_MS);
  CHECK(m.update(false, 6, LOCK_WIDEN_EVERY_MS + 1) == ChannelLock::Action::NONE);
}

// ------------------------------------------------------------------ strings
static void test_strings() {
  const uint8_t m[6] = {0x14, 0xc1, 0x9f, 0x4f, 0xd3, 0x0c};
  CHECK(ssid_with_suffix("MoveSensorBLE", m) == "MoveSensorBLE-4FD30C");
  const std::string longbase(40, 'A');
  const std::string s = ssid_with_suffix(longbase, m);
  CHECK(s.size() == SSID_MAX);
  CHECK(s.substr(25) == "-4FD30C");
  CHECK(valid_ap_password(""));
  CHECK(!valid_ap_password("1234567"));
  CHECK(valid_ap_password("12345678"));
  CHECK(valid_ap_password(std::string(63, 'x')));
  CHECK(!valid_ap_password(std::string(64, 'x')));
  CHECK(!valid_ap_password("pass\nword"));
}

// ------------------------------------------------------------------ templates
static void test_templates() {
  const Vars v = {{"title", "Deye <Battery>"}, {"sta_ip", "10.1.30.117"}, {"empty", ""}, {"zero", "0"},
                  {"icon", "<svg/>"}};
  CHECK(render("<h1>{{title}}</h1>", v) == "<h1>Deye &lt;Battery&gt;</h1>");
  CHECK(render("{{{icon}}}", v) == "<svg/>");
  CHECK(render("{{ title }}", v) == "Deye &lt;Battery&gt;");          // spaces inside the tag
  CHECK(render("[{{nope}}]", v) == "[]");                             // unknown variable
  CHECK(render("{{#if sta_ip}}home {{sta_ip}}{{/if}}", v) == "home 10.1.30.117");
  CHECK(render("{{#if empty}}A{{else}}B{{/if}}", v) == "B");
  CHECK(render("{{#if zero}}A{{else}}B{{/if}}", v) == "B");
  CHECK(render("{{#unless empty}}A{{/unless}}", v) == "A");
  CHECK(render("{{#if sta_ip}}1{{#if empty}}2{{else}}3{{/if}}4{{/if}}", v) == "134");  // nested
  CHECK(render("{{#if empty}}1{{#if sta_ip}}2{{/if}}3{{else}}4{{/if}}", v) == "4");     // hidden parent
  CHECK(render("width:100%;a{{b", v) == "width:100%;a{{b");          // unclosed tag is text
  CHECK(render("{{title", v) == "{{title");
  CHECK(!has_template_tags("<p>static</p>"));
  CHECK(has_template_tags("<p>{{x}}</p>"));
}

// ------------------------------------------------------------------ claim / clients
static void test_claim_clients() {
  CHECK(claim(Route::LANDING, true, false) == Claim::OURS);
  CHECK(claim(Route::REDIRECT, true, true) == Claim::OURS);
  CHECK(claim(Route::PASS, true, false) == Claim::NONE);    // web_server answers as usual
  CHECK(claim(Route::PASS, true, true) == Claim::FORWARD);  // captive_portal must not see it
  CHECK(claim(Route::PASS, false, true) == Claim::NONE);    // POST is never captive_portal's

  ClientTable t;
  Mac a, b, m;
  a.b[5] = 1;
  b.b[5] = 2;
  t.set(0x6401a8c0, a);
  CHECK(t.find(0x6401a8c0, m) && m == a);
  t.set(0x6401a8c0, b);  // DHCP gave the IP to another client
  CHECK(t.find(0x6401a8c0, m) && m == b);
  t.remove(b);
  CHECK(!t.find(0x6401a8c0, m));
  for (uint32_t k = 0; k < 20; k++) {
    Mac x;
    x.b[4] = (uint8_t) k;
    t.set(k, x);
  }
  CHECK(t.find(19, m) && m.b[4] == 19);  // full table keeps the newest
}

// ------------------------------------------------------------------ password reset
static void test_password_reset() {
  // four power cycles
  PowerCycleStep st = power_cycle_step(0, true, 4);
  CHECK(st.count == 1 && !st.reset);
  st = power_cycle_step(st.count, true, 4);
  st = power_cycle_step(st.count, true, 4);
  CHECK(st.count == 3 && !st.reset);
  st = power_cycle_step(st.count, true, 4);
  CHECK(st.count == 0 && st.reset);
  CHECK(!power_cycle_step(3, false, 4).reset);         // deep-sleep wake / OTA restart: not counted
  CHECK(power_cycle_step(3, false, 4).count == 0);     // ... and the series starts over
  CHECK(!power_cycle_step(9, true, 0).reset);          // off
  CHECK(power_cycle_step(200, true, 4).count == 1);    // junk in flash: a fresh series

  HoldDetector h(5000);
  CHECK(!h.update(true, 0));
  CHECK(!h.update(true, 4999));
  CHECK(h.update(true, 5000));
  CHECK(!h.update(true, 9000));   // fires once per press
  CHECK(!h.update(false, 9100));
  CHECK(!h.update(true, 10000));
  CHECK(!h.update(false, 12000));  // released too early
  CHECK(!h.update(true, 20000));
  CHECK(h.update(true, 25000));
}

// ------------------------------------------------------------------ wifi page routes
static void test_wifi_routes() {
  RouteConfig c;
  c.wifi_path = "/wifi";
  Request r;
  r.from_ap = true;
  r.path = "/wifi";
  CHECK(route(r, c) == Route::WIFI);
  r.path = "/ap_portal/networks";
  CHECK(route(r, c) == Route::API);
  r.get = false;  // POST /ap_portal/connect
  r.path = "/ap_portal/connect";
  CHECK(route(r, c) == Route::API);
  r.from_ap = false;  // home network: never
  CHECK(route(r, c) == Route::PASS);
  r.get = true;
  r.path = "/wifi";
  CHECK(route(r, c) == Route::PASS);
  r.from_ap = true;
  c.mode = Mode::GATE;  // gate: the Wi-Fi page and API need no pass (the AP password protects them)
  CHECK(route(r, c) == Route::WIFI);
  r.path = "/";  // the rest still waits for Enter
  CHECK(route(r, c) == Route::LANDING);
  r.get = false;
  r.path = "/ap_portal/connect";
  CHECK(route(r, c) == Route::API);
  r.has_pass = true;
  CHECK(route(r, c) == Route::API);
  c.wifi_path.clear();  // page off, API on (a device with its own site)
  c.mode = Mode::REDIRECT;
  r.get = true;
  r.path = "/ap_portal/networks";
  CHECK(route(r, c) == Route::API);
  r.path = "/wifi";
  CHECK(route(r, c) == Route::PASS);
  c.api = false;  // both off: /ap_portal/ is just another path
  r.path = "/ap_portal/networks";
  CHECK(route(r, c) == Route::PASS);
}

static void test_wrap() {
  RouteConfig c;
  c.wrap_root = true;
  Request r;
  r.from_ap = true;
  r.path = "/";
  CHECK(route(r, c) == Route::WRAP);
  r.raw = true;  // the frame itself
  CHECK(route(r, c) == Route::PASS);
  r.raw = false;
  r.from_ap = false;  // home network: ESPHome's page untouched
  CHECK(route(r, c) == Route::PASS);
  r.from_ap = true;
  c.mode = Mode::GATE;  // gate without a pass: the landing first
  CHECK(route(r, c) == Route::LANDING);
  r.has_pass = true;
  CHECK(route(r, c) == Route::WRAP);
  c.wrap_root = false;
  CHECK(route(r, c) == Route::PASS);
}

static void test_sta_errors() {
  CHECK(std::string(sta_error_kind(0)).empty());
  CHECK(std::string(sta_error_kind(8)).empty());
  CHECK(std::string(sta_error_kind(15)) == "wrong_password");
  CHECK(std::string(sta_error_kind(204)) == "wrong_password");
  CHECK(std::string(sta_error_kind(201)) == "not_found");
  CHECK(std::string(sta_error_kind(205)) == "failed");
}

static void test_reboot_timer() {
  using ap_portal_core::RebootTimer;
  RebootTimer off(0);
  CHECK(!off.update(false, 0, 1000));
  CHECK(!off.update(false, 0, 10000000));
  RebootTimer t(120000);
  CHECK(!t.update(true, 0, 1000));        // router: nothing
  CHECK(!t.update(false, 0, 2000));       // no router: timer starts
  CHECK(!t.update(false, 0, 121000));     // 119 s
  CHECK(t.update(false, 0, 122001));      // > 120 s: reboot
  RebootTimer c(120000);
  CHECK(!c.update(false, 0, 1000));
  CHECK(!c.update(false, 1, 100000));     // client joined: timer reset
  CHECK(!c.update(false, 1, 500000));     // still a client: never
  CHECK(!c.update(false, 0, 500001));     // client left: starts over
  CHECK(!c.update(false, 0, 620000));
  CHECK(c.update(false, 0, 620002));
  RebootTimer r(120000);
  CHECK(!r.update(false, 0, 1000));
  CHECK(!r.update(true, 0, 100000));      // router back: reset
  CHECK(!r.update(false, 0, 110000));
  CHECK(!r.update(false, 0, 229000));
  CHECK(r.update(false, 0, 230001));
  RebootTimer w(120000);                  // millis() wrap
  CHECK(!w.update(false, 0, 0xFFFFF000u));
  CHECK(w.update(false, 0, 120000));
}

static void test_switch_hopeless() {
  using namespace ap_portal_core;
  CHECK(!switch_hopeless(2, 15));   // two wrong-password answers in a row: not yet
  CHECK(switch_hopeless(3, 15));    // three: give up, back to the old network
  CHECK(switch_hopeless(3, 202));
  CHECK(switch_hopeless(4, 201));   // network not there
  CHECK(!switch_hopeless(5, 205));  // a plain failure waits for the deadline
  CHECK(!switch_hopeless(5, 8));    // our own leave is no answer
  CHECK(!switch_hopeless(0, 0));
  // streak: only the same kind in a row counts
  uint8_t r = 0, k = 0;
  k = switch_streak(r, k, 201); r = 201;  CHECK(k == 1);
  k = switch_streak(r, k, 201); r = 201;  CHECK(k == 2);
  k = switch_streak(r, k, 15); r = 15;    CHECK(k == 1);   // not_found, not_found, wrong_password
  CHECK(!switch_hopeless(k, r));
  k = switch_streak(r, k, 202); r = 202;  CHECK(k == 2);   // 15 and 202 are both wrong_password
  k = switch_streak(r, k, 204); r = 204;  CHECK(k == 3);
  CHECK(switch_hopeless(k, r));
}

static void test_passes_unknown_mac() {
  using ap_portal_core::Mac;
  using ap_portal_core::PassTable;
  PassTable t(60000);
  Mac zero{};
  Mac a{};
  a.b[5] = 1;
  t.grant(zero, 0x0A00000A, 1000);  // static IP .10, MAC unknown
  t.grant(zero, 0x0B00000A, 1000);  // another one, .11: its own pass, not .10's
  CHECK(t.check(0x0A00000A, 2000));
  CHECK(t.check(0x0B00000A, 2000));
  t.grant(a, 0x0C00000A, 1000);
  t.revoke(a);                      // a known client leaves
  t.revoke_unknown();               // IP-only passes go with any leave
  CHECK(!t.check(0x0A00000A, 3000));
  CHECK(!t.check(0x0B00000A, 3000));
  CHECK(!t.check(0x0C00000A, 3000));
}

static void test_fallback_ap() {
  FallbackAp f(30000, 60000);
  CHECK(!f.update(true, 0, 1000));      // router: no access point
  CHECK(!f.update(false, 0, 2000));     // router gone, not long enough
  CHECK(!f.update(false, 0, 31999));
  CHECK(f.update(false, 0, 32000));     // 30 s without a router: up
  CHECK(f.update(true, 1, 40000));      // router back, a phone on it: stays
  CHECK(f.update(true, 1, 200000));
  CHECK(f.update(true, 0, 200001));     // the phone left: idle starts
  CHECK(f.update(true, 0, 260000));
  CHECK(!f.update(true, 0, 260001));    // 60 s idle: down
  CHECK(!f.update(false, 0, 270000));   // gone again: waits `after` again
  CHECK(f.update(false, 0, 300000));
  CHECK(f.update(true, 0, 310000));     // back, idle...
  CHECK(f.update(false, 0, 320000));    // ...gone again before idle ran out: stays up
  CHECK(f.update(true, 0, 330000));
  CHECK(!f.update(true, 0, 390000));    // idle counted from the return
}


static void test_csrf() {
  using ap_portal_core::post_allowed;
  using ap_portal_core::origin_host;
  CHECK(origin_host("http://10.1.30.117") == "10.1.30.117");
  CHECK(origin_host("http://Sensor.local:8080/x") == "sensor.local:8080");
  CHECK(origin_host("null") == "");
  // our page: header, same origin
  CHECK(post_allowed(true, "http://10.1.30.117", "10.1.30.117", "same-origin"));
  CHECK(post_allowed(true, "http://move-sensor.local", "Move-Sensor.local", ""));
  // no Origin / Sec-Fetch-Site (older browsers, curl with the header)
  CHECK(post_allowed(true, "", "192.168.1.118", ""));
  // a form or fetch from another site: no header
  CHECK(!post_allowed(false, "https://evil.example", "192.168.1.118", "cross-site"));
  CHECK(!post_allowed(false, "", "192.168.1.118", ""));
  // header but a foreign origin (some other page on the LAN, a rebinding name)
  CHECK(!post_allowed(true, "http://192.168.1.50", "192.168.1.118", "same-site"));
  CHECK(!post_allowed(true, "http://192.168.1.118", "attacker.example", ""));
  CHECK(!post_allowed(true, "null", "192.168.1.118", ""));
  CHECK(!post_allowed(true, "http://192.168.1.118", "192.168.1.118", "cross-site"));
  // the access point page and the sensor's own site posting to itself
  CHECK(post_allowed(true, "http://100.64.4.1", "100.64.4.1", "same-origin"));
}


static uint32_t ipbe(unsigned a, unsigned b, unsigned c, unsigned d) { return a | (b << 8) | (c << 16) | (d << 24); }

static void test_cors() {
  using ap_portal_core::cors_allowed;
  using ap_portal_core::own_origin_ip;
  const uint32_t ap = ipbe(100, 64, 4, 1), sta = ipbe(192, 168, 1, 89), prev = ipbe(10, 1, 30, 117);
  const std::string host = "move-sensor-ble-148f98";
  auto ok = [&](const char *o) { return cors_allowed(o, ap, host, sta, prev); };
  CHECK(own_origin_ip(ap) == "http://100.64.4.1");
  CHECK(own_origin_ip(0) == "");
  // the device's own pages
  CHECK(ok("http://100.64.4.1"));
  CHECK(ok("http://move-sensor-ble-148f98.local"));
  CHECK(ok("HTTP://Move-Sensor-BLE-148F98.LOCAL"));   // case does not matter
  CHECK(ok("http://192.168.1.89"));
  CHECK(ok("http://10.1.30.117"));                    // the page that switched routers, on the old address
  // attacks: other sites
  CHECK(!ok(""));
  CHECK(!ok("null"));
  CHECK(!ok("https://evil.example"));
  CHECK(!ok("http://evil.example"));
  CHECK(!ok("http://localhost"));
  CHECK(!ok("http://127.0.0.1"));
  CHECK(!ok("file://"));
  CHECK(!ok("chrome-extension://abcdef"));
  // look-alike names and addresses
  CHECK(!ok("http://move-sensor-ble-148f98.local.evil.com"));
  CHECK(!ok("http://evil-move-sensor-ble-148f98.local"));
  CHECK(!ok("http://move-sensor-ble-148f98.local."));
  CHECK(!ok("http://move-sensor-ble-148f98"));
  CHECK(!ok("http://other-device.local"));
  CHECK(!ok("http://move-sensor-ble-148f98.local@evil.com"));
  CHECK(!ok("http://evil.com#http://move-sensor-ble-148f98.local"));
  CHECK(!ok("http://192.168.1.8"));                    // prefix of ours
  CHECK(!ok("http://192.168.1.890"));
  CHECK(!ok("http://192.168.1.89.evil.com"));
  CHECK(!ok("http://192.168.1.50"));                   // another host on the LAN
  CHECK(!ok("http://100.64.4.2"));                     // another access point client
  CHECK(!ok("http://3232235865"));                     // 192.168.1.89 as a number
  CHECK(!ok("http://0xc0a80159"));
  CHECK(!ok("http://[::ffff:192.168.1.89]"));
  // scheme and port
  CHECK(!ok("https://192.168.1.89"));
  CHECK(!ok("https://move-sensor-ble-148f98.local"));
  CHECK(!ok("http://192.168.1.89:8080"));
  CHECK(!ok("http://192.168.1.89:80"));                // browsers never send :80; exact match only
  CHECK(!ok("ws://192.168.1.89"));
  CHECK(!ok(" http://192.168.1.89"));
  CHECK(!ok("http://192.168.1.89 "));
  CHECK(!ok("http://192.168.1.89/"));
  CHECK(!cors_allowed(std::string("http://192.168.1.89\0.evil", 25), ap, host, sta, prev));
  CHECK(!ok(("http://" + std::string(100, 'a') + ".local").c_str()));
  // unknown addresses never match: no router, no previous one, no access point
  CHECK(!cors_allowed("http://0.0.0.0", ap, host, 0, 0));
  CHECK(!cors_allowed("http://0.0.0.0", 0, host, 0, 0));
  CHECK(!cors_allowed("http://.local", ap, "", sta, prev));
  CHECK(!cors_allowed("http://192.168.1.89", ap, host, 0, 0));   // not the router address any more
  CHECK(cors_allowed("http://192.168.1.89", 0, host, sta, 0));
}


static void test_api_busy() {
  using ap_portal_core::api_busy;
  using A = ap_portal_core::ApiAction;
  // idle: everything goes
  for (A a : {A::SCAN, A::CONNECT, A::APPLY, A::AP_PASSWORD, A::CANCEL})
    CHECK(!api_busy(a, false, false, false));
  // scanning: no connect / apply / password; a second scan is the same scan
  CHECK(!api_busy(A::SCAN, true, false, false));
  CHECK(api_busy(A::CONNECT, true, false, false));
  CHECK(api_busy(A::APPLY, true, false, false));
  CHECK(api_busy(A::AP_PASSWORD, true, false, false));
  // switching: nothing but cancel
  CHECK(api_busy(A::SCAN, false, true, false));
  CHECK(api_busy(A::CONNECT, false, true, false));
  CHECK(api_busy(A::APPLY, false, true, false));
  CHECK(api_busy(A::AP_PASSWORD, false, true, false));
  CHECK(!api_busy(A::CANCEL, true, true, true));
  // access point password being applied: the access point restarts, wait
  CHECK(api_busy(A::SCAN, false, false, true));
  CHECK(api_busy(A::CONNECT, false, false, true));
  CHECK(api_busy(A::AP_PASSWORD, false, false, true));
}


static void test_dns_qname() {
  using ap_portal_core::dns_qname;
  // captive.apple.com, type A
  const uint8_t q[] = {0x12,0x34, 0x01,0x00, 0,1, 0,0, 0,0, 0,0,
                       7,'c','a','p','t','i','v','e', 5,'a','p','p','l','e', 3,'c','o','m', 0, 0,1, 0,1};
  char name[64]; uint16_t t = 0;
  CHECK(dns_qname(q, sizeof(q), name, sizeof(name), &t));
  CHECK(std::string(name) == "captive.apple.com" && t == 1);
  // HTTPS (65)
  uint8_t h[sizeof(q)]; std::memcpy(h, q, sizeof(q)); h[sizeof(q) - 3] = 65;
  CHECK(dns_qname(h, sizeof(h), name, sizeof(name), &t) && t == 65);
  // truncated / compressed / tiny buffer stay safe
  CHECK(!dns_qname(q, 20, name, sizeof(name), &t));
  uint8_t c[sizeof(q)]; std::memcpy(c, q, sizeof(q)); c[12] = 0xC0;
  CHECK(!dns_qname(c, sizeof(c), name, sizeof(name), &t));
  char small[6];
  CHECK(dns_qname(q, sizeof(q), small, sizeof(small), &t) && std::strlen(small) == 5);
  CHECK(std::string(ap_portal_core::route_name(ap_portal_core::Route::REDIRECT)) == "redirect");
}


static void test_reboot_timer_armed() {
  ap_portal_core::RebootTimer t(15u * 60u * 1000u);
  // no router network set / Wi-Fi off: never fires, never even counts
  CHECK(!t.update(false, 0, 1000, false));
  CHECK(!t.running());
  CHECK(!t.update(false, 0, 1000 + 20u * 60u * 1000u, false));
  CHECK(!t.running());
  // armed: counts and fires after 15 min, a client stops it, the router stops it
  CHECK(!t.update(false, 0, 2000, true));
  CHECK(t.running());
  CHECK(!t.update(false, 1, 2000 + 10u * 60u * 1000u, true));
  CHECK(!t.running());
  CHECK(!t.update(false, 0, 3000, true));
  CHECK(t.update(false, 0, 3000 + 15u * 60u * 1000u, true));
  // disarmed in the middle (Wi-Fi switched off): the count starts over
  ap_portal_core::RebootTimer u(60000);
  CHECK(!u.update(false, 0, 100, true));
  CHECK(!u.update(false, 0, 50000, false));
  CHECK(!u.update(false, 0, 60000, true));
  CHECK(!u.update(false, 0, 110000, true));
  CHECK(u.update(false, 0, 120001, true));
}

int main() {
  test_reboot_timer_armed();
  test_dns_qname();
  test_api_busy();
  test_cors();
  test_csrf();
  test_timers();
  test_hosts();
  test_route_redirect_page();
  test_route_gate();
  test_passes();
  test_dns();
  test_quiet();
  test_strings();
  test_templates();
  test_claim_clients();
  test_password_reset();
  test_wifi_routes();
  test_wrap();
  test_sta_errors();
  test_fallback_ap();
  test_passes_unknown_mac();
  test_switch_hopeless();
  test_reboot_timer();
  std::printf("%d checks, %d failed\n", g_checks, g_fail);
  return g_fail ? EXIT_FAILURE : EXIT_SUCCESS;
}
