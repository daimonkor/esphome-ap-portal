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
  CHECK(quiet_wanted({false, 1, true}));
  CHECK(!quiet_wanted({true, 1, true}));    // router connected
  CHECK(!quiet_wanted({false, 0, true}));   // nobody on the access point
  CHECK(!quiet_wanted({false, 1, false}));  // access point down

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

int main() {
  test_timers();
  test_hosts();
  test_route_redirect_page();
  test_route_gate();
  test_passes();
  test_dns();
  test_quiet();
  test_strings();
  std::printf("%d checks, %d failed\n", g_checks, g_fail);
  return g_fail ? EXIT_FAILURE : EXIT_SUCCESS;
}
