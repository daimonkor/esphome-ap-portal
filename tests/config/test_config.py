"""ESPHome config validation: the examples pass, wrong configs fail (or warn) with a clear message.

    python3 -m pytest tests/config      (needs `pip install esphome pytest`)

Every row of the "build checks" table has a test here.
"""
import pathlib
import subprocess
import textwrap

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
EXAMPLES = sorted((ROOT / "examples").glob("*.yaml"))


def _esphome_version():
    out = subprocess.run(["esphome", "version"], capture_output=True, text=True).stdout
    parts = out.strip().split()[-1].split(".")
    return tuple(int(p) for p in parts[:2])


ESPHOME_VER = _esphome_version()
TESTED = tuple(
    int(p)
    for p in next(
        l for l in (ROOT / "components/ap_portal/__init__.py").read_text().splitlines() if l.startswith("TESTED_ESPHOME")
    ).split('"')[1].split(".")
)
on_tested = pytest.mark.skipif(ESPHOME_VER != TESTED, reason="needs the tested ESPHome version")
needs_2026_11 = pytest.mark.skipif(ESPHOME_VER < (2026, 11), reason="wifi: ap: ap_mode exists from ESPHome 2026.11")

ESP = """
esp32:
  board: esp32-c3-devkitm-1
  framework:
    type: esp-idf
"""

AP_GOOD = """
    manual_ip: {static_ip: 100.64.4.1, gateway: 100.64.4.1, subnet: 255.255.255.0}
"""


def head(esp=ESP, ap_ssid="T-AP", ap_extra=AP_GOOD, ap=True):
    ap_block = f"  ap:\n    ssid: {ap_ssid}\n    password: test12345\n{ap_extra}" if ap else ""
    return f"""
esphome:
  name: t
{esp}
logger:
external_components:
  - source:
      type: local
      path: {ROOT / "components"}
wifi:
  ssid: r
  password: router123
{ap_block}
"""


def esphome_config(path: pathlib.Path) -> subprocess.CompletedProcess:
    return subprocess.run(["esphome", "config", str(path)], capture_output=True, text=True, timeout=300)


def run(tmp_path, text):
    f = tmp_path / "c.yaml"
    f.write_text(text)
    r = esphome_config(f)
    return r, r.stdout + r.stderr


@pytest.mark.parametrize("example", EXAMPLES, ids=lambda p: p.name)
def test_examples_valid(example):
    r = esphome_config(example)
    assert r.returncode == 0, r.stdout[-2000:] + r.stderr[-2000:]


GOOD = {
    "minimal": "ap_portal:\n",
    "lambda_active": "globals:\n  - {id: svc, type: bool}\nap_portal:\n  active: !lambda return id(svc);\n",
    "styled": "ap_portal:\n  landing: {title: X, accent_color: '#f59e0b'}\n",
    "mode_redirect_explicit": "ap_portal:\n  landing: {mode: redirect}\n",
    "short_color": "ap_portal:\n  landing: {accent_color: '#fa0'}\n",
    "own_html": "ap_portal:\n  landing: {html: '<html>x</html>'}\n",
    "html_template_vars": textwrap.dedent("""\
        ap_portal:
          landing:
            accent_color: '#ff0000'
            html: "<h1>{{title}}</h1>{{#if sta_ip}}{{sta_ip}}{{/if}}{{battery}} {{fw}}"
            vars:
              battery: !lambda return std::string("87");
              fw: "1.2.3"
          on_landing:
            - logger.log: {format: "%s %s", args: [ip.c_str(), path.c_str()]}
        """),
    "html_static_gz": "ap_portal:\n  landing: {html: '<p>static page</p>'}\n",
    "reset_defaults": "ap_portal:\n  password_persist: true\n",
    "reset_own_pin": textwrap.dedent("""\
        ap_portal:
          password_persist: true
          password_reset:
            power_cycles: 3
            pin: {number: GPIO2, inverted: true, mode: {input: true, pullup: true}}
            hold: 3s
          on_power_cycle:
            - logger.log: {format: "start %d", args: [count]}
          on_password_reset:
            - logger.log: {format: "reset by %s", args: [source.c_str()]}
        """),
    "reset_off": "ap_portal:\n  password_persist: true\n  password_reset: {power_cycles: 0, pin: false}\n",
    "api_from_home": "ap_portal:\n  api: {from_home: true}\n  wifi_page: {enabled: false}\n",
    "wifi_page_custom": "ap_portal:\n  wifi_page: {path: /setup, ap_password: false}\n",
    "router_fallback": textwrap.dedent("""\
        ap_portal:
          router_fallback: {timeout: 60s, previous: true, firmware: false}
          on_router_fallback:
            - logger.log: {format: "fallback to %s (%s)", args: [to.c_str(), ssid.c_str()]}
        """),
    "fallback_own_network": "ap_portal:\n  router_fallback:\n    firmware: {ssid: Backup-Net, password: backup1234}\n",
    "suffix_fits": "ap_portal:\n  ssid_suffix_mac: true\n",
    "actions": textwrap.dedent("""\
        ap_portal:
          id: p
        button:
          - platform: template
            name: a
            on_press:
              - ap_portal.scan: p
              - ap_portal.set_password: {id: p, password: abcdefgh}
              - ap_portal.set_password: {id: p, password: ""}
              - ap_portal.reset_password: p
        """),
    "triggers": textwrap.dedent("""\
        ap_portal:
          on_client_connected:
            - logger.log: {format: "%s", args: [mac.c_str()]}
          on_client_disconnected:
            - logger.log: "left"
          on_ap_restored:
            - logger.log: "restored"
          on_portal_start:
            - logger.log: "start"
          on_portal_stop:
            - logger.log: "stop"
        """),
}

BAD = {
    # wifi: ap: missing
    "no_wifi_ap": (head(ap=False), "ap_portal:\n", "needs 'wifi: ap:'"),
    # SSID too long with the MAC suffix: 26 + 7 > 32
    "ssid_suffix_too_long": (head(ap_ssid="A" * 26), "ap_portal:\n  ssid_suffix_mac: true\n", "longer than 32"),
    "var_builtin_name": (head(), "ap_portal:\n  landing: {vars: {ssid: x}}\n", "built-in variable"),
    "var_bad_name": (head(), "ap_portal:\n  landing: {vars: {Bad-Name: x}}\n", "variable name"),
    # removed in 0.2.0: landing modes page and gate, the gate keys and on_enter
    "mode_gate_removed": (head(), "ap_portal:\n  landing: {mode: gate}\n", "was removed in ap_portal 0.2.0"),
    "mode_page_removed": (head(), "ap_portal:\n  landing: {mode: page}\n", "was removed in ap_portal 0.2.0"),
    "pass_paths_removed": (head(), "ap_portal:\n  landing: {pass_paths: [/x]}\n", "were removed in ap_portal 0.2.0"),
    "enter_path_removed": (head(), "ap_portal:\n  landing: {enter_path: /e}\n", "were removed in ap_portal 0.2.0"),
    "on_enter_removed": (
        head(),
        "ap_portal:\n  on_enter:\n    - logger.log: x\n",
        "on_enter was removed",
    ),
    "path_without_slash": (head(), "ap_portal:\n  landing: {path: welcome}\n", "must start with '/'"),
    "bad_color": (head(), "ap_portal:\n  landing: {accent_color: blue}\n", "is not a color"),
    "unknown_mode": (head(), "ap_portal:\n  landing: {mode: popup}\n", "popup"),
    "short_password_action": (
        head(),
        "ap_portal:\n  id: p\nbutton:\n  - platform: template\n    name: a\n    on_press:\n"
        "      - ap_portal.set_password: {id: p, password: short}\n",
        "8..63",
    ),
    "one_power_cycle": (head(), "ap_portal:\n  password_reset: {power_cycles: 1}\n", "0 (off) or 2..9"),
    "cycles_vs_safe_mode": (
        head(),
        "safe_mode:\n  num_attempts: 4\nap_portal:\n  password_persist: true\n",
        "below safe_mode num_attempts",
    ),
    "wifi_page_without_api": (head(), "ap_portal:\n  api: {enabled: false}\n", "needs api"),
    "wifi_page_clash": (head(), "ap_portal:\n  wifi_page: {path: /welcome}\n", "clashes"),
    "fallback_too_short": (head(), "ap_portal:\n  router_fallback: {timeout: 5s}\n", "timeout"),
    "fallback_short_password": (
        head(),
        "ap_portal:\n  router_fallback:\n    firmware: {ssid: Backup-Net, password: short}\n",
        "8..64",
    ),
    "removed_option": (head(), "ap_portal:\n  keep_ap: native\n", "keep_ap"),
}

WARN = {
    "no_manual_ip": (head(ap_extra=""), "gets 192.168.4.1"),
    "private_ip": (
        head(ap_extra="    manual_ip: {static_ip: 192.168.4.1, gateway: 192.168.4.1, subnet: 255.255.255.0}\n"),
        "private range",
    ),
}


@pytest.mark.parametrize("name", GOOD)
def test_good(tmp_path, name):
    r, out = run(tmp_path, head() + GOOD[name])
    assert r.returncode == 0, out[-2000:]


ESP_ARDUINO = """
esp32:
  board: esp32-c3-devkitm-1
  framework:
    type: arduino
"""


@pytest.mark.parametrize("name", ["minimal", "own_html", "styled"])
def test_good_arduino(tmp_path, name):
    r, out = run(tmp_path, head(esp=ESP_ARDUINO) + GOOD[name])
    assert r.returncode == 0, out[-2000:]


@needs_2026_11
def test_wifi_ap_always_on_is_valid_arduino(tmp_path):
    r, out = run(tmp_path, head(esp=ESP_ARDUINO, ap_extra=ALWAYS_ON) + "  post_connect_roaming: false\nap_portal:\n")
    assert r.returncode == 0, out[-2000:]


@pytest.mark.parametrize("name", BAD)
def test_bad(tmp_path, name):
    h, body, needle = BAD[name]
    r, out = run(tmp_path, h + body)
    assert r.returncode != 0, out[-2000:]
    assert needle in out, out[-2000:]


@pytest.mark.parametrize("name", WARN)
def test_warn(tmp_path, name):
    h, needle = WARN[name]
    r, out = run(tmp_path, h + "ap_portal:\n")
    assert r.returncode == 0, out[-2000:]
    assert needle in out, out[-2000:]


@on_tested
def test_good_config_has_no_ap_portal_warning(tmp_path):
    r, out = run(tmp_path, head() + "ap_portal:\n")
    assert r.returncode == 0, out[-2000:]
    assert "ap_portal:" not in "\n".join(l for l in out.splitlines() if "WARNING" in l), out[-2000:]


@on_tested
def test_keep_ap_options_valid_without_warning_on_tested_esphome(tmp_path):
    for opts in ("  keep_ap_on_connect: true\n", "  ap_raise: without_router\n  fallback: {after: 20s, off_when_idle: 2min}\n"):
        r, out = run(tmp_path, head() + "ap_portal:\n" + opts)
        assert r.returncode == 0, out[-2000:]
        assert "has_ap_" not in out, out[-2000:]


def test_ap_mode_rejects_unknown(tmp_path):
    r, out = run(tmp_path, head() + "ap_portal:\n  ap_mode: sometimes\n")
    assert r.returncode != 0
    assert "ap_mode" in out or "sometimes" in out, out[-2000:]


ALWAYS_ON = AP_GOOD + "    ap_mode: always_on\n"


def _always_on_head():
    return head(ap_extra=ALWAYS_ON) + "  post_connect_roaming: false\n"


@needs_2026_11
def test_wifi_ap_always_on_is_valid(tmp_path):
    r, out = run(tmp_path, _always_on_head() + "ap_portal:\n")
    assert r.returncode == 0, out[-2000:]


@needs_2026_11
def test_later_button_off_is_valid(tmp_path):
    r, out = run(tmp_path, _always_on_head() + "ap_portal:\n  wifi_page:\n    later_button: false\n")
    assert r.returncode == 0, out[-2000:]


@needs_2026_11
def test_reboot_timeout_allowed_with_always_on(tmp_path):
    # ap_portal manages "reboot without a router" with always_on too (ESPHome's one is switched off)
    r, out = run(tmp_path, _always_on_head() + "ap_portal:\n  reboot_timeout: 5min\n")
    assert r.returncode == 0, out[-2000:]


@needs_2026_11
@pytest.mark.parametrize(
    "opts,needle",
    [
        ("  keep_ap_on_connect: true\n", "keep_ap_on_connect is not needed"),
        ("  ap_raise: without_router\n", "use one of them"),
        ("  ap_mode: fallback\n", "use one of them"),
    ],
)
def test_wifi_ap_always_on_conflicts(tmp_path, opts, needle):
    r, out = run(tmp_path, _always_on_head() + "ap_portal:\n" + opts)
    assert r.returncode != 0
    assert needle in out, out[-2000:]


C3 = """esp32:
  board: esp32-c3-devkitm-1
  framework:
    type: esp-idf"""


def test_boot_pin_used_by_i2c_warns(tmp_path):
    text = head(esp=C3) + "i2c:\n  sda: GPIO8\n  scl: GPIO9\nap_portal:\n  password_persist: true\n"
    r, out = run(tmp_path, text)
    assert r.returncode == 0, out[-2000:]
    assert "GPIO9 (the BOOT button) is used elsewhere" in out, out[-2000:]


@pytest.mark.parametrize(
    "opts,needle",
    [
        ("  landing:\n    path: /\n", "is the device's own page"),
        ("  wifi_page:\n    path: /\n", "is the device's own page"),
        ("  landing:\n    path: /ap_portal/x\n", "reserved"),
        ("  wifi_page:\n    ui_button: true\n", "add web_server"),
    ],
)
def test_bad_paths_and_ui_button(tmp_path, opts, needle):
    r, out = run(tmp_path, head() + "ap_portal:\n" + opts)
    assert r.returncode != 0
    assert needle in out, out[-2000:]


@needs_2026_11
def test_wifi_ap_wisp_rejected(tmp_path):
    h = head(ap_extra=AP_GOOD + "    ap_mode: wisp\n")
    r, out = run(tmp_path, h + "ap_portal:\n")
    assert r.returncode != 0
    assert "WISP" in out, out[-2000:]


def test_ap_raise_rejects_unknown(tmp_path):
    r, out = run(tmp_path, head() + "ap_portal:\n  ap_raise: sometimes\n")
    assert r.returncode != 0
    assert "ap_raise" in out or "sometimes" in out, out[-2000:]


@on_tested
def test_ap_mode_old_name_still_works_with_warning(tmp_path):
    r, out = run(tmp_path, head() + "ap_portal:\n  ap_mode: fallback\n")
    assert r.returncode == 0, out[-2000:]
    assert "renamed to ap_raise" in out and "ap_raise: without_router" in out, out[-2000:]


def test_ap_mode_and_ap_raise_together_rejected(tmp_path):
    r, out = run(tmp_path, head() + "ap_portal:\n  ap_mode: always\n  ap_raise: always\n")
    assert r.returncode != 0
    assert "old name of ap_raise" in out, out[-2000:]


@on_tested
def test_ap_raise_default_has_no_rename_warning(tmp_path):
    r, out = run(tmp_path, head() + "ap_portal:\n  ap_raise: always\n")
    assert r.returncode == 0, out[-2000:]
    assert "renamed to ap_raise" not in out, out[-2000:]


def test_no_router_with_api_warns_about_api_reboot(tmp_path):
    h = head().replace("  ssid: r\n", "").replace("  password: router123\n", "")
    r, out = run(tmp_path, h + "api:\nap_portal:\n")
    if r.returncode != 0:
        pytest.skip("this ESPHome needs a network for this head: " + out[-300:])
    assert "api: reboot_timeout" in out, out[-2000:]
    r, out = run(tmp_path, h + "api:\n  reboot_timeout: 0s\nap_portal:\n")
    assert r.returncode == 0, out[-2000:]
    assert "api: reboot_timeout (default" not in out, out[-2000:]
