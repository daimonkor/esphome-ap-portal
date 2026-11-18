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
    "page_mode": "ap_portal:\n  landing: {mode: page, title: X, accent_color: '#f59e0b'}\n",
    "short_color": "ap_portal:\n  landing: {accent_color: '#fa0'}\n",
    "gate_defaults": "ap_portal:\n  landing: {mode: gate}\n",
    "gate_full": textwrap.dedent("""\
        ap_portal:
          landing:
            mode: gate
            html: "<html>x</html>"
            enter_path: /dashboard
            enter_window: 10min
            pass_paths: [/history.bin, /events]
        """),
    "html_template_vars": textwrap.dedent("""\
        ap_portal:
          landing:
            mode: page
            accent_color: '#ff0000'
            html: "<h1>{{title}}</h1>{{#if sta_ip}}{{sta_ip}}{{/if}}{{battery}} {{fw}}"
            vars:
              battery: !lambda return std::string("87");
              fw: "1.2.3"
          on_landing:
            - logger.log: {format: "%s %s", args: [ip.c_str(), path.c_str()]}
        """),
    "html_static_gz": "ap_portal:\n  landing: {mode: page, html: '<p>static page</p>'}\n",
    "gate_enter_hook": textwrap.dedent("""\
        ap_portal:
          landing:
            mode: gate
          on_enter:
            - logger.log: {format: "%s %s", args: [mac.c_str(), ip.c_str()]}
        """),
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
    # framework
    "arduino": (
        head(esp="\nesp32:\n  board: esp32dev\n  framework:\n    type: arduino\n"),
        "ap_portal:\n",
        "esp-idf",
    ),
    # SSID too long with the MAC suffix: 26 + 7 > 32
    "ssid_suffix_too_long": (head(ap_ssid="A" * 26), "ap_portal:\n  ssid_suffix_mac: true\n", "longer than 32"),
    "var_builtin_name": (head(), "ap_portal:\n  landing: {vars: {ssid: x}}\n", "built-in variable"),
    "var_bad_name": (head(), "ap_portal:\n  landing: {vars: {Bad-Name: x}}\n", "variable name"),
    # gate-only keys outside gate mode
    "pass_paths_not_gate": (head(), "ap_portal:\n  landing: {pass_paths: [/x]}\n", "only work with mode: gate"),
    "enter_path_not_gate": (head(), "ap_portal:\n  landing: {mode: page, enter_path: /e}\n", "only work with mode: gate"),
    "enter_equals_path": (
        head(),
        "ap_portal:\n  landing: {mode: gate, path: /w, enter_path: /w}\n",
        "must differ",
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
    "window_too_short": (
        head(),
        "ap_portal:\n  landing: {mode: gate, enter_window: 10s}\n",
        "enter_window",
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


def test_good_config_has_no_ap_portal_warning(tmp_path):
    r, out = run(tmp_path, head() + "ap_portal:\n")
    assert r.returncode == 0, out[-2000:]
    assert "ap_portal:" not in "\n".join(l for l in out.splitlines() if "WARNING" in l), out[-2000:]
