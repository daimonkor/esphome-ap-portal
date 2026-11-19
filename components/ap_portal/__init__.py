"""ap_portal — the device's own Wi-Fi access point next to the router connection.

DNS hijack for the phone's "Sign in to network" window, an own landing page (redirect / page /
gate), and a quiet mode that keeps phones on the access point when there is no router.
https://github.com/daimonkor/esphome-ap-portal
"""

import gzip
import hashlib
import ipaddress
import logging
import re

from esphome import automation, pins
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    CONF_MANUAL_IP,
    CONF_PASSWORD,
    CONF_PATH,
    CONF_PIN,
    CONF_SSID,
    CONF_STATIC_IP,
    CONF_TRIGGER_ID,
    __version__ as ESPHOME_VERSION,
)
from esphome.core import CORE, ID
import esphome.final_validate as fv

_LOGGER = logging.getLogger(__name__)

CODEOWNERS = ["@daimonkor"]
DEPENDENCIES = ["wifi", "esp32"]
AUTO_LOAD = ["web_server_base", "captive_portal", "network", "socket"]

# The library leans on ESPHome internals (fallback AP, captive_portal quiet mode, handler order),
# so it is tested against one ESPHome version. Others may work; the build says so.
TESTED_ESPHOME = "2026.11"
# Every release line the library was checked on (hardware): no warning on these.
CHECKED_ESPHOME = ("2026.10", "2026.11")

CONF_ACTIVE = "active"
CONF_SSID_SUFFIX_MAC = "ssid_suffix_mac"
CONF_PASSWORD_PERSIST = "password_persist"
CONF_LOG_EXCLUDE_PREFIXES = "log_exclude_prefixes"
CONF_DNS = "dns"
CONF_ENABLED = "enabled"
CONF_LANDING = "landing"
CONF_MODE = "mode"
CONF_HTML = "html"
CONF_FILE = "file"
CONF_TITLE = "title"
CONF_ACCENT_COLOR = "accent_color"
CONF_ICON_SVG = "icon_svg"
CONF_PRIMARY_LABEL = "primary_label"
CONF_SECONDARY = "secondary"
CONF_LABEL = "label"
CONF_HREF = "href"
CONF_ENTER_PATH = "enter_path"
CONF_ENTER_WINDOW = "enter_window"
CONF_PASS_PATHS = "pass_paths"
CONF_VARS = "vars"
CONF_ON_LANDING = "on_landing"
CONF_ON_ENTER = "on_enter"
CONF_WIFI_PAGE = "wifi_page"
CONF_API = "api"
CONF_KEEP_AP_ON_CONNECT = "keep_ap_on_connect"
CONF_REBOOT_TIMEOUT = "reboot_timeout"
CONF_AP_MODE = "ap_mode"  # old name of ap_raise (0.1.x), still read
CONF_AP_RAISE = "ap_raise"
CONF_FALLBACK = "fallback"
CONF_AFTER = "after"
CONF_OFF_WHEN_IDLE = "off_when_idle"
CONF_ROUTER_FALLBACK = "router_fallback"
CONF_TIMEOUT = "timeout"
CONF_PREVIOUS = "previous"
CONF_FIRMWARE = "firmware"
CONF_ON_ROUTER_FALLBACK = "on_router_fallback"
CONF_FROM_HOME = "from_home"
CONF_AP_PASSWORD = "ap_password"
CONF_PASSWORD_RESET = "password_reset"
CONF_POWER_CYCLES = "power_cycles"
CONF_HOLD = "hold"
CONF_ON_POWER_CYCLE = "on_power_cycle"
CONF_ON_PASSWORD_RESET = "on_password_reset"

# The board's BOOT button: the strapping pin every ESP32 board wires to a button.
BOOT_PINS = {
    "ESP32": 0, "ESP32S2": 0, "ESP32S3": 0,
    "ESP32C2": 9, "ESP32C3": 9, "ESP32C5": 28, "ESP32C6": 9, "ESP32C61": 9,
}
CONF_ON_CLIENT_CONNECTED = "on_client_connected"
CONF_ON_CLIENT_DISCONNECTED = "on_client_disconnected"
CONF_ON_AP_RESTORED = "on_ap_restored"
CONF_ON_PORTAL_START = "on_portal_start"
CONF_ON_PORTAL_STOP = "on_portal_stop"

# Filled in by the component on every request; `vars:` may not reuse these names.
BUILTIN_VARS = (
    "title", "ap_ip", "sta_ip", "host", "ssid", "enter", "landing", "mode", "clients",
    "accent_color", "accent_text", "icon_svg", "primary_label", "primary_default",
    "secondary_label", "secondary_href", "secondary_default",
    "wifi_path", "ap_password_block", "custom_password",
)
GATE_KEYS = (CONF_ENTER_PATH, CONF_ENTER_WINDOW, CONF_PASS_PATHS)

ap_portal_ns = cg.esphome_ns.namespace("ap_portal")
ApPortal = ap_portal_ns.class_("ApPortal", cg.Component)
ClientConnectedTrigger = ap_portal_ns.class_(
    "ClientConnectedTrigger", automation.Trigger.template(cg.std_string)
)
ClientDisconnectedTrigger = ap_portal_ns.class_(
    "ClientDisconnectedTrigger", automation.Trigger.template(cg.std_string)
)
ApRestoredTrigger = ap_portal_ns.class_("ApRestoredTrigger", automation.Trigger.template())
PortalStartTrigger = ap_portal_ns.class_("PortalStartTrigger", automation.Trigger.template())
PortalStopTrigger = ap_portal_ns.class_("PortalStopTrigger", automation.Trigger.template())
LandingTrigger = ap_portal_ns.class_(
    "LandingTrigger", automation.Trigger.template(cg.std_string, cg.std_string)
)
EnterTrigger = ap_portal_ns.class_(
    "EnterTrigger", automation.Trigger.template(cg.std_string, cg.std_string)
)
PowerCycleTrigger = ap_portal_ns.class_("PowerCycleTrigger", automation.Trigger.template(cg.int_))
RouterFallbackTrigger = ap_portal_ns.class_(
    "RouterFallbackTrigger", automation.Trigger.template(cg.std_string, cg.std_string)
)
PasswordResetTrigger = ap_portal_ns.class_(
    "PasswordResetTrigger", automation.Trigger.template(cg.std_string)
)
ScanAction = ap_portal_ns.class_("ScanAction", automation.Action)
SetPasswordAction = ap_portal_ns.class_("SetPasswordAction", automation.Action)
ResetPasswordAction = ap_portal_ns.class_("ResetPasswordAction", automation.Action)

MODES = {"redirect": 0, "page": 1, "gate": 2}


def _url_path(value):
    value = cv.string_strict(value)
    if not value.startswith("/"):
        raise cv.Invalid(f"'{value}' must start with '/'")
    if any(c in value for c in " ?#"):
        raise cv.Invalid(f"'{value}' must be a plain path (no spaces, '?' or '#')")
    return value


def _own_path(value):
    """A page of ap_portal's own: not the device's root page, not under its API prefix."""
    value = _url_path(value)
    if value == "/":
        raise cv.Invalid("'/' is the device's own page; use a path of its own, like /welcome")
    if value.startswith("/ap_portal/"):
        raise cv.Invalid("/ap_portal/ is reserved for ap_portal's endpoints")
    return value


def _pass_path(value):
    value = _url_path(value)
    if value == "/":
        raise cv.Invalid("pass_paths: '/' would let every page through and switch the gate off")
    return value


def _builtin_js():
    """CLIENT_JS and POPUP_JS as landing_page.h has them (the C++ copies stay as a fallback)."""
    import re
    from pathlib import Path

    src = (Path(__file__).parent / "landing_page.h").read_text(encoding="utf-8")
    out = {}
    for name in ("CLIENT_JS", "POPUP_JS"):
        m = re.search(r"static const char " + name + r'\[\] = R"JS\((.*?)\)JS";', src, re.S)
        if m is None:
            raise cv.Invalid(f"ap_portal: {name} not found in landing_page.h")
        out[name] = m.group(1)
    return out["CLIENT_JS"], out["POPUP_JS"]


def _hex_color(value):
    value = cv.string_strict(value).strip()
    h = value[1:] if value.startswith("#") else value
    if len(h) == 3:
        h = "".join(c * 2 for c in h)
    if len(h) != 6 or any(c not in "0123456789abcdefABCDEF" for c in h):
        raise cv.Invalid(f"'{value}' is not a color like #3d7bff")
    return "#" + h.lower()


def _sta_password(value):
    if value and not 8 <= len(value) <= 64:
        raise cv.Invalid("a router password is empty (open network) or 8..64 characters")
    return value


def _reset_pin(value):
    if value is False or str(value).lower() in ("false", "off", "none"):
        return False
    return pins.gpio_input_pin_schema(value)


def _power_cycles(value):
    value = cv.int_range(min=0, max=9)(value)
    if value == 1:
        raise cv.Invalid("power_cycles: 0 (off) or 2..9 — one power-on is every normal start")
    return value


WIFI_PAGE_SCHEMA = cv.Schema(
    {
        # the built-in Wi-Fi page: scan list, router network, access point password;
        # access point clients only. false = the device has its own Wi-Fi page.
        cv.Optional(CONF_ENABLED, default=True): cv.boolean,
        cv.Optional(CONF_PATH, default="/wifi"): _own_path,
        cv.Optional(CONF_AP_PASSWORD, default=True): cv.boolean,  # on/off, not a secret
        # ESPHome's own page (web_server) gets a Wi-Fi button in the corner for access point
        # clients: "/" is served as a frame around it. For a device without its own site.
        cv.Optional("ui_button", default=False): cv.boolean,
        # "Save, connect later" on the Wi-Fi page and in the popup. By default only where the access
        # point may drop during a router switch: not with wifi: ap: ap_mode: always_on/WISP or
        # keep_ap_on_connect — there "Connect now" keeps the phone on and follows the switch.
        cv.Optional("later_button"): cv.boolean,
    }
)

PASSWORD_RESET_SCHEMA = cv.Schema(
    {
        # RESET button or power plug, this many times in a row, each start under 10 s; 0 = off
        cv.Optional(CONF_POWER_CYCLES, default=4): _power_cycles,
        # a button held while running; default: the board's BOOT button; false = off
        cv.Optional(CONF_PIN): _reset_pin,
        cv.Optional(CONF_HOLD, default="5s"): cv.All(
            cv.positive_time_period_milliseconds,
            cv.Range(min=cv.TimePeriod(seconds=2), max=cv.TimePeriod(seconds=30)),
        ),
    }
)


def _var_name(value):
    value = cv.string_strict(value)
    if not re.fullmatch(r"[a-z_][a-z0-9_]*", value):
        raise cv.Invalid(f"'{value}': a variable name is lower case letters, digits and '_'")
    if value in BUILTIN_VARS:
        raise cv.Invalid(f"'{value}' is a built-in variable; pick another name")
    return value


# Inline text, or `file: path` read at build time.
TEXT_OR_FILE = cv.Any(
    cv.Schema({cv.Required(CONF_FILE): cv.file_}),
    cv.string_strict,
)

LANDING_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_MODE, default="redirect"): cv.one_of(*MODES, lower=True),
        cv.Optional(CONF_PATH, default="/welcome"): _own_path,
        cv.Optional(CONF_HTML): TEXT_OR_FILE,
        cv.Optional(CONF_TITLE): cv.string_strict,
        cv.Optional(CONF_ACCENT_COLOR): _hex_color,
        cv.Optional(CONF_ICON_SVG): TEXT_OR_FILE,
        cv.Optional(CONF_PRIMARY_LABEL): cv.string_strict,
        cv.Optional(CONF_SECONDARY): cv.Schema(
            {
                cv.Required(CONF_LABEL): cv.string_strict,
                cv.Required(CONF_HREF): cv.string_strict,
            }
        ),
        cv.Optional(CONF_ENTER_PATH): _own_path,
        cv.Optional(CONF_ENTER_WINDOW): cv.All(
            cv.positive_time_period_milliseconds,
            cv.Range(min=cv.TimePeriod(minutes=1), max=cv.TimePeriod(hours=24)),
        ),
        cv.Optional(CONF_PASS_PATHS): cv.ensure_list(_pass_path),
        # Own template variables: a string, or a lambda returning std::string (read in loop()
        # once a second, the page gets the latest value).
        cv.Optional(CONF_VARS, default={}): cv.Schema(
            {cv.string_strict: cv.Any(cv.returning_lambda, cv.string)}
        ),
    }
)


def _validate_landing(config):
    for name in config[CONF_VARS]:
        _var_name(name)
    if config[CONF_MODE] != "gate":
        extra = [k for k in GATE_KEYS if k in config]
        if extra:
            raise cv.Invalid(
                f"{', '.join(repr(k) for k in extra)} only work with mode: gate "
                f"(mode is '{config[CONF_MODE]}')."
            )
    else:
        config.setdefault(CONF_ENTER_PATH, "/enter")
        config.setdefault(CONF_ENTER_WINDOW, cv.positive_time_period_milliseconds("30min"))
        config.setdefault(CONF_PASS_PATHS, [])
        if config[CONF_ENTER_PATH] == config[CONF_PATH]:
            raise cv.Invalid("'enter_path' and 'path' must differ")
    return config


def _ap_raise(config):
    """ap_raise (new) / ap_mode (old name, 0.1.x): one value in config[CONF_AP_RAISE]."""
    old = config.pop(CONF_AP_MODE, None)
    if old is not None:
        if CONF_AP_RAISE in config:
            raise cv.Invalid("ap_portal: ap_mode is the old name of ap_raise — use one of them (ap_raise)")
        new = "without_router" if old == "fallback" else "always"
        _LOGGER.warning(
            "ap_portal: ap_mode is renamed to ap_raise (it was easy to mix up with ESPHome's wifi: ap: ap_mode); "
            "write ap_raise: %s. The old name still works.",
            new,
        )
        config[CONF_AP_RAISE] = new
    config.setdefault(CONF_AP_RAISE, "always")
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(ApPortal),
            cv.Optional(CONF_ACTIVE, default=True): cv.Any(cv.boolean, cv.returning_lambda),
            cv.Optional(CONF_SSID_SUFFIX_MAC, default=False): cv.boolean,
            cv.Optional(CONF_PASSWORD_PERSIST, default=False): cv.boolean,
            cv.Optional(CONF_LOG_EXCLUDE_PREFIXES, default=[]): cv.ensure_list(_url_path),
            cv.Optional(CONF_DNS, default={}): cv.Schema(
                {cv.Optional(CONF_ENABLED, default=True): cv.boolean}
            ),
            cv.Optional(CONF_LANDING, default={}): cv.All(LANDING_SCHEMA, _validate_landing),
            # EXPERIMENTAL: ESPHome switches the access point off on every router connection;
            # with this it does not (ap_portal manages it). Turns ESPHome's reboot_timeout off.
            cv.Optional(CONF_KEEP_AP_ON_CONNECT, default=False): cv.boolean,
            # with keep_ap_on_connect: reboot after this long without a router and with nobody on
            # the access point. Default: wifi: reboot_timeout (15 min). 0s = never.
            cv.Optional(CONF_REBOOT_TIMEOUT): cv.positive_time_period_milliseconds,
            # always: the access point runs whenever `active`; without_router: only without a router.
            # (Was ap_mode: always / fallback — easy to mix up with ESPHome's wifi: ap: ap_mode.)
            cv.Optional(CONF_AP_RAISE): cv.one_of("always", "without_router", lower=True),
            cv.Optional(CONF_AP_MODE): cv.one_of("always", "fallback", lower=True),
            cv.Optional(CONF_FALLBACK, default={}): cv.Schema(
                {
                    cv.Optional(CONF_AFTER, default="30s"): cv.positive_time_period_milliseconds,
                    cv.Optional(CONF_OFF_WHEN_IDLE, default="60s"): cv.positive_time_period_milliseconds,
                }
            ),
            cv.Optional(CONF_WIFI_PAGE, default={}): WIFI_PAGE_SCHEMA,
            # /ap_portal/ endpoints and client.js for the device's own site
            cv.Optional(CONF_API, default={}): cv.Schema(
                {
                    cv.Optional(CONF_ENABLED, default=True): cv.boolean,
                    # also from the home network (behind web_server's login, if it has one)
                    cv.Optional(CONF_FROM_HOME, default=False): cv.boolean,
                }
            ),
            cv.Optional(CONF_PASSWORD_RESET, default={}): PASSWORD_RESET_SCHEMA,
            # a network saved from the page that does not connect after a restart: back to the
            # previous one, then to the one from the YAML, each step `timeout` long
            cv.Optional(CONF_ROUTER_FALLBACK, default={}): cv.Schema(
                {
                    cv.Optional(CONF_TIMEOUT, default="45s"): cv.All(
                        cv.positive_time_period_milliseconds,
                        cv.Range(min=cv.TimePeriod(seconds=15), max=cv.TimePeriod(minutes=10)),
                    ),
                    cv.Optional(CONF_PREVIOUS, default=True): cv.boolean,
                    # true: the network from wifi: (ssid / networks[0]); false: off;
                    # {ssid, password}: a fallback network of its own
                    cv.Optional(CONF_FIRMWARE, default=True): cv.Any(
                        cv.boolean,
                        cv.Schema(
                            {
                                cv.Required(CONF_SSID): cv.ssid,
                                cv.Optional(CONF_PASSWORD, default=""): cv.sensitive(
                                    cv.All(cv.string_strict, _sta_password)
                                ),
                            }
                        ),
                    ),
                }
            ),
            cv.Optional(CONF_ON_ROUTER_FALLBACK): automation.validate_automation(
                {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(RouterFallbackTrigger)}
            ),
            cv.Optional(CONF_ON_POWER_CYCLE): automation.validate_automation(
                {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(PowerCycleTrigger)}
            ),
            cv.Optional(CONF_ON_PASSWORD_RESET): automation.validate_automation(
                {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(PasswordResetTrigger)}
            ),
            cv.Optional(CONF_ON_CLIENT_CONNECTED): automation.validate_automation(
                {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(ClientConnectedTrigger)}
            ),
            cv.Optional(CONF_ON_CLIENT_DISCONNECTED): automation.validate_automation(
                {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(ClientDisconnectedTrigger)}
            ),
            cv.Optional(CONF_ON_AP_RESTORED): automation.validate_automation(
                {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(ApRestoredTrigger)}
            ),
            cv.Optional(CONF_ON_PORTAL_START): automation.validate_automation(
                {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(PortalStartTrigger)}
            ),
            cv.Optional(CONF_ON_PORTAL_STOP): automation.validate_automation(
                {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(PortalStopTrigger)}
            ),
            cv.Optional(CONF_ON_LANDING): automation.validate_automation(
                {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(LandingTrigger)}
            ),
            cv.Optional(CONF_ON_ENTER): automation.validate_automation(
                {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(EnterTrigger)}
            ),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    _ap_raise,
    cv.only_on_esp32,
    cv.only_with_framework("esp-idf"),
)


# ───────────────────────────── build checks ─────────────────────────────
PRIVATE_NETS = [ipaddress.ip_network(n) for n in ("10.0.0.0/8", "172.16.0.0/12", "192.168.0.0/16")]
SUFFIX_LEN = 7  # "-4FD30C"


def _final_validate(config):
    full = fv.full_config.get()
    wifi = full.get("wifi", {})
    ap = wifi.get("ap")
    if ap is None:
        raise cv.Invalid(
            "ap_portal needs 'wifi: ap:' — the SSID, password and address of the access point "
            "are taken from there (one place, nothing to keep in sync)."
        )

    api_conf = full.get("api")
    if not wifi.get("networks") and api_conf is not None:
        api_rt = api_conf.get("reboot_timeout")
        if api_rt is None or api_rt.total_milliseconds != 0:
            # A device without a router network: ap_portal never reboots it for the missing router,
            # but ESPHome's API does after 15 min without Home Assistant — even with a phone on the
            # access point.
            _LOGGER.warning(
                "ap_portal: no router network in wifi: — this device lives on its access point, but "
                "api: reboot_timeout (default 15min) reboots it whenever Home Assistant is not connected. "
                "Set api: reboot_timeout: 0s."
            )

    manual = ap.get(CONF_MANUAL_IP)
    if manual is None:
        _LOGGER.warning(
            "ap_portal: 'wifi: ap:' has no manual_ip, so the access point gets 192.168.4.1. "
            "Android does not show the 'Sign in to network' window for a private address; "
            "100.64.4.1 is recommended (manual_ip: {static_ip: 100.64.4.1, gateway: 100.64.4.1, "
            "subnet: 255.255.255.0})."
        )
    else:
        ip = ipaddress.ip_address(str(manual[CONF_STATIC_IP]))
        if any(ip in n for n in PRIVATE_NETS):
            _LOGGER.warning(
                "ap_portal: the access point address %s is in a private range. Android does not "
                "show the 'Sign in to network' window for it; 100.64.4.1 is recommended.",
                ip,
            )

    if config[CONF_SSID_SUFFIX_MAC]:
        ssid = ap.get(CONF_SSID) or CORE.name
        if len(ssid.encode()) + SUFFIX_LEN > 32:
            raise cv.Invalid(
                f"SSID '{ssid}' with the MAC suffix ('-XXXXXX') is longer than 32 bytes; "
                f"keep it to {32 - SUFFIX_LEN}."
            )

    wifi_conf = full.get("wifi", {})
    wifi_ap_mode = str(wifi_conf.get("ap", {}).get("ap_mode", "FALLBACK")).upper()
    if wifi_ap_mode == "WISP":
        raise cv.Invalid(
            "wifi: ap: ap_mode: WISP gives the access point's clients the router's internet; ap_portal "
            "answers every DNS name with the device itself, so they would get none. Use ALWAYS_ON."
        )
    if (config[CONF_KEEP_AP_ON_CONNECT] or config[CONF_AP_RAISE] == "without_router") and wifi_conf.get(
        "post_connect_roaming", False
    ):
        _LOGGER.warning(
            "ap_portal: keep_ap_on_connect with wifi: post_connect_roaming: true — a roaming scan may "
            "take the access point off its channel for a moment. Set post_connect_roaming: false "
            "(ESPHome requires it for its own always-on access point)."
        )
    if config[CONF_WIFI_PAGE]["ui_button"] and "web_server" not in full:
        raise cv.Invalid("wifi_page: ui_button: true puts a button on web_server's page; add web_server:")
    reset = config[CONF_PASSWORD_RESET]
    if config[CONF_PASSWORD_PERSIST] and reset.get(CONF_PIN) is None:
        boot, used = _boot_pin(full, config)
        if used:
            _LOGGER.warning(
                "ap_portal: GPIO%d (the BOOT button) is used elsewhere in this configuration, so "
                "holding BOOT does not reset the access point password. Set password_reset: pin: "
                "to another button, or pin: false to silence this.",
                boot,
            )
    if not config[CONF_PASSWORD_PERSIST]:
        unused = [k for k in (CONF_ON_POWER_CYCLE, CONF_ON_PASSWORD_RESET) if config.get(k)]
        if unused:
            _LOGGER.warning(
                "ap_portal: %s never fire without password_persist: true (nothing to reset)",
                ", ".join(unused),
            )
    if wifi_ap_mode != "FALLBACK":
        # ESPHome 2026.11+ keeps the access point next to the router itself
        if config[CONF_KEEP_AP_ON_CONNECT]:
            raise cv.Invalid(
                f"keep_ap_on_connect is not needed with wifi: ap: ap_mode: {wifi_ap_mode} — ESPHome keeps the "
                "access point on router connections itself; remove one of them"
            )
        if config[CONF_AP_RAISE] == "without_router":
            raise cv.Invalid(
                f"ap_portal ap_raise: without_router switches the access point off, wifi: ap: ap_mode: {wifi_ap_mode} keeps "
                "it on; use one of them"
            )
        if CONF_REBOOT_TIMEOUT in config:
            raise cv.Invalid(
                f"with wifi: ap: ap_mode: {wifi_ap_mode} ESPHome's own wifi: reboot_timeout applies (it waits while "
                "someone is on the access point); remove ap_portal reboot_timeout"
            )
    wp = config[CONF_WIFI_PAGE]
    if wp[CONF_ENABLED] and not config[CONF_API][CONF_ENABLED]:
        raise cv.Invalid("wifi_page needs api: enabled (the page talks to /ap_portal/)")
    if wp[CONF_ENABLED]:
        land = config[CONF_LANDING]
        if wp[CONF_PATH] in (land[CONF_PATH], land.get(CONF_ENTER_PATH)):
            raise cv.Invalid(f"wifi_page path '{wp[CONF_PATH]}' clashes with the landing paths")
        if wp[CONF_PATH].startswith("/ap_portal/"):
            raise cv.Invalid("wifi_page path: /ap_portal/ is reserved for its endpoints")

    cycles = config[CONF_PASSWORD_RESET][CONF_POWER_CYCLES]
    if config[CONF_PASSWORD_PERSIST] and cycles and "safe_mode" in full:
        attempts = int(full["safe_mode"].get("num_attempts", 10))
        if cycles >= attempts:
            raise cv.Invalid(
                f"password_reset: power_cycles ({cycles}) must be below safe_mode num_attempts "
                f"({attempts}), or safe mode starts first"
            )

    if not any(ESPHOME_VERSION.startswith(v + ".") for v in CHECKED_ESPHOME):
        _LOGGER.warning(
            "ap_portal is tested with ESPHome %s.x; you have %s. It uses ESPHome Wi-Fi "
            "internals — check the access point and quiet mode after updating.",
            TESTED_ESPHOME,
            ESPHOME_VERSION,
        )
        if config[CONF_KEEP_AP_ON_CONNECT] or config[CONF_AP_RAISE] == "without_router":
            _LOGGER.warning(
                "ap_portal: keep_ap_on_connect%s clears ESPHome's private WiFiComponent::has_ap_, "
                "checked on ESPHome 2026.10.x-%s.x. On %s check after flashing: the log shows "
                "'Access point kept on router connections', there is no 'Disabling AP' when the "
                "router connects, and the device does not reboot without a router.",
                " (implied by ap_raise: without_router)" if not config[CONF_KEEP_AP_ON_CONNECT] else "",
                TESTED_ESPHOME,
                ESPHOME_VERSION,
            )
    return config


FINAL_VALIDATE_SCHEMA = _final_validate


def _contrast_text(color):
    """White or black text on the accent color, whichever reads better (WCAG luminance)."""
    r, g, b = (int(color[i : i + 2], 16) / 255 for i in (1, 3, 5))
    lin = [c / 12.92 if c <= 0.03928 else ((c + 0.055) / 1.055) ** 2.4 for c in (r, g, b)]
    lum = 0.2126 * lin[0] + 0.7152 * lin[1] + 0.0722 * lin[2]
    return "#000000" if (lum + 0.05) / 0.05 > 1.05 / (lum + 0.05) else "#ffffff"


def _read(value):
    if isinstance(value, dict):
        return CORE.relative_config_path(value[CONF_FILE]).read_text(encoding="utf-8")
    return value


async def to_code(config):
    from esphome.components import wifi

    wifi.enable_runtime_roaming_suppression()  # ap_portal.scan pauses ESPHome's roaming scans
    if "ota" in CORE.config:
        # reboot without a router waits while an OTA upload runs
        from esphome.components.ota import request_ota_state_listeners

        request_ota_state_listeners()
    if "logger" in CORE.config:
        # the "log while away" ring: a failed router switch can be read once the device is back
        from esphome.components.logger import request_log_listener

        request_log_listener()
    if config[CONF_API][CONF_ENABLED]:
        # The network list needs every scan record, not only the configured network's (ESPHome
        # otherwise filters them and, with one network, scans for its SSID alone); the HTTP
        # handler reads them from the web server task, so under ESPHome's lock.
        wifi.request_wifi_scan_results()
        wifi.request_wifi_scan_results_lock()
        # ESPHome never sorts a scan list it stores while connected; ap_portal puts the configured
        # network first after each one (and goes on with a router switch waiting for it)
        wifi.request_wifi_scan_results_listener()
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    if config[CONF_API][CONF_ENABLED]:
        cg.add(cg.RawExpression(f"wifi::global_wifi_component->add_scan_results_listener({var})"))

    active = config[CONF_ACTIVE]
    if isinstance(active, bool):
        cg.add(var.set_active(active))
    else:
        lam = await cg.process_lambda(active, [], return_type=cg.bool_)
        cg.add(var.set_active_lambda(lam))

    cg.add(var.set_ssid_suffix_mac(config[CONF_SSID_SUFFIX_MAC]))
    cg.add(var.set_password_persist(config[CONF_PASSWORD_PERSIST]))
    for p in config[CONF_LOG_EXCLUDE_PREFIXES]:
        cg.add(var.add_log_exclude_prefix(p))
    cg.add(var.set_dns_enabled(config[CONF_DNS][CONF_ENABLED]))

    landing = config[CONF_LANDING]
    cg.add(var.set_mode(MODES[landing[CONF_MODE]]))
    cg.add(var.set_landing_path(landing[CONF_PATH]))
    if CONF_HTML in landing:
        html = _read(landing[CONF_HTML])
        if "{{" in html:
            cg.add(var.set_html(html))
        else:
            # A page without template tags goes out gzip-compressed, straight from flash.
            data = gzip.compress(html.encode("utf-8"), compresslevel=9, mtime=0)
            arr = cg.progmem_array(ID(f"{config[CONF_ID].id}_html_gz", is_declaration=True, type=cg.uint8), list(data))
            cg.add(var.set_html_gz(arr, len(data)))
    accent = landing.get(CONF_ACCENT_COLOR, "#3d7bff")
    cg.add(var.add_static_var("accent_color", accent))
    cg.add(var.add_static_var("accent_text", _contrast_text(accent)))
    if CONF_TITLE in landing:
        cg.add(var.add_static_var("title", landing[CONF_TITLE]))
    if CONF_ICON_SVG in landing:
        cg.add(var.add_static_var("icon_svg", _read(landing[CONF_ICON_SVG])))
    cg.add(var.add_static_var("primary_label", landing.get(CONF_PRIMARY_LABEL, "Open device page")))
    cg.add(var.add_static_var("primary_default", "" if CONF_PRIMARY_LABEL in landing else "1"))
    cg.add(var.set_keep_ap_on_connect(config[CONF_KEEP_AP_ON_CONNECT]))
    if CONF_REBOOT_TIMEOUT in config:
        reboot_ms = config[CONF_REBOOT_TIMEOUT].total_milliseconds
    else:
        wifi_rt = CORE.config.get("wifi", {}).get("reboot_timeout")
        reboot_ms = wifi_rt.total_milliseconds if wifi_rt is not None else 15 * 60 * 1000
    cg.add(var.set_reboot_timeout(reboot_ms))
    if config[CONF_AP_RAISE] == "without_router":
        apf = config[CONF_FALLBACK]
        cg.add(var.set_ap_fallback(apf[CONF_AFTER].total_milliseconds, apf[CONF_OFF_WHEN_IDLE].total_milliseconds))
    fb = config[CONF_ROUTER_FALLBACK]
    firmware = fb[CONF_FIRMWARE]
    cg.add(var.set_router_fallback(fb[CONF_TIMEOUT].total_milliseconds, fb[CONF_PREVIOUS], bool(firmware)))
    if isinstance(firmware, dict):
        cg.add(var.set_firmware_sta(firmware[CONF_SSID], firmware[CONF_PASSWORD]))
    elif firmware:
        # ESPHome does not hand out its YAML network before Wi-Fi starts; it is in the firmware anyway
        nets = CORE.config.get("wifi", {}).get("networks", [])
        if nets:
            cg.add(var.set_firmware_sta(nets[0].get(CONF_SSID, ""), nets[0].get(CONF_PASSWORD, "")))
    for conf in config.get(CONF_ON_ROUTER_FALLBACK, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [(cg.std_string, "to"), (cg.std_string, "ssid")], conf)
    api = config[CONF_API]
    cg.add(var.set_api(api[CONF_ENABLED], api[CONF_FROM_HOME]))
    if api[CONF_ENABLED]:
        # client.js / popup.js: gzip-compressed here and served straight from flash (no heap)
        client, popup = _builtin_js()
        cid = config[CONF_ID].id
        cgz = gzip.compress(client.encode("utf-8"), compresslevel=9, mtime=0)
        pgz = gzip.compress((client + popup).encode("utf-8"), compresslevel=9, mtime=0)
        carr = cg.progmem_array(ID(f"{cid}_client_js_gz", is_declaration=True, type=cg.uint8), list(cgz))
        parr = cg.progmem_array(ID(f"{cid}_popup_js_gz", is_declaration=True, type=cg.uint8), list(pgz))
        cg.add(var.set_js_gz(carr, len(cgz), parr, len(pgz)))
        etag = '"' + hashlib.sha1(cgz + pgz).hexdigest()[:12] + '"'
        cg.add(var.set_js_etag(etag))
    wifi_page = config[CONF_WIFI_PAGE]
    later_button = wifi_page.get("later_button")
    if later_button is None:
        wifi_ap_mode = str(CORE.config.get("wifi", {}).get("ap", {}).get("ap_mode", "FALLBACK")).upper()
        later_button = wifi_ap_mode == "FALLBACK" and not config[CONF_KEEP_AP_ON_CONNECT]
    cg.add(var.set_later_button(later_button))  # the popup asks too, page or not
    if wifi_page[CONF_ENABLED]:
        cg.add(var.set_wifi_path(wifi_page[CONF_PATH]))
        cg.add(var.set_wifi_ap_password(wifi_page[CONF_AP_PASSWORD]))
        cg.add(var.set_ui_button(wifi_page["ui_button"]))
    if CONF_SECONDARY in landing:
        sec = landing[CONF_SECONDARY]
        cg.add(var.add_static_var("secondary_label", sec[CONF_LABEL]))
        cg.add(var.add_static_var("secondary_href", sec[CONF_HREF]))
    elif wifi_page[CONF_ENABLED]:
        # the second button of the built-in page leads to the Wi-Fi page by default
        cg.add(var.add_static_var("secondary_label", "Router Wi-Fi"))
        cg.add(var.add_static_var("secondary_href", wifi_page[CONF_PATH]))
        cg.add(var.add_static_var("secondary_default", "1"))
    for name, value in landing[CONF_VARS].items():
        if isinstance(value, cv.Lambda):
            lam = await cg.process_lambda(value, [], return_type=cg.std_string)
            cg.add(var.add_lambda_var(name, lam))
        else:
            cg.add(var.add_static_var(name, value))
    if landing[CONF_MODE] == "gate":
        cg.add(var.set_enter_path(landing[CONF_ENTER_PATH]))
        cg.add(var.set_enter_window(landing[CONF_ENTER_WINDOW].total_milliseconds))
        for p in landing[CONF_PASS_PATHS]:
            cg.add(var.add_pass_path(p))

    for conf in config.get(CONF_ON_CLIENT_CONNECTED, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [(cg.std_string, "mac")], conf)
    for conf in config.get(CONF_ON_CLIENT_DISCONNECTED, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [(cg.std_string, "mac")], conf)
    for conf in config.get(CONF_ON_LANDING, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [(cg.std_string, "ip"), (cg.std_string, "path")], conf)
    for conf in config.get(CONF_ON_ENTER, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [(cg.std_string, "mac"), (cg.std_string, "ip")], conf)
    for key in (CONF_ON_AP_RESTORED, CONF_ON_PORTAL_START, CONF_ON_PORTAL_STOP):
        for conf in config.get(key, []):
            trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
            await automation.build_automation(trigger, [], conf)

    if config[CONF_PASSWORD_PERSIST]:
        cg.add_define("USE_AP_PORTAL_PASSWORD_PERSIST")
        reset = config[CONF_PASSWORD_RESET]
        cg.add(var.set_power_cycles(reset[CONF_POWER_CYCLES]))
        cg.add(var.set_reset_hold(reset[CONF_HOLD].total_milliseconds))
        pin = reset.get(CONF_PIN)
        if pin:
            cg.add(var.set_reset_pin(await cg.gpio_pin_expression(pin)))
        elif pin is None:
            boot = _boot_pin_if_free(config)
            if boot is not None:
                cg.add(var.set_boot_pin(boot))
    for conf in config.get(CONF_ON_POWER_CYCLE, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [(cg.int_, "count")], conf)
    for conf in config.get(CONF_ON_PASSWORD_RESET, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [(cg.std_string, "source")], conf)


# Keys whose plain value is a pin number (buses and drivers that take a bare pin, not a pin block).
_PIN_KEYS = ("sda", "scl", "clk", "mosi", "miso", "din", "dout", "bclk", "lrclk", "mclk", "xclk")


def _plain_pin(key, value):
    if not isinstance(key, str) or isinstance(value, bool):
        return None
    k = key.lower()
    if not (k in _PIN_KEYS or k == "pin" or k.endswith("_pin") or k.endswith("_pin_number")):
        return None
    if isinstance(value, int):
        return value
    if isinstance(value, str) and value.upper().startswith("GPIO") and value[4:].isdigit():
        return int(value[4:])
    return None


def _pin_numbers(node, skip):
    """Every pin number in the configuration: pin blocks (a dict with 'number' and 'mode'/'inverted')
    and bare pin numbers (I2C sda/scl, camera clocks, *_pin keys)."""
    if node is skip:
        return
    if isinstance(node, dict):
        if "number" in node and ("mode" in node or "inverted" in node):
            yield node["number"]
        for k, v in node.items():
            n = _plain_pin(k, v)
            if n is not None:
                yield n
            else:
                yield from _pin_numbers(v, skip)
    elif isinstance(node, (list, tuple)):
        for v in node:
            yield from _pin_numbers(v, skip)


def _boot_pin(full, config):
    """(BOOT pin of this chip, is it used elsewhere) — None when the chip has none."""
    from esphome.components import esp32

    boot = BOOT_PINS.get(esp32.get_esp32_variant())
    if boot is None:
        return None, False
    return boot, boot in set(_pin_numbers(full, config))


def _boot_pin_if_free(config):
    boot, used = _boot_pin(CORE.config, config)
    return None if used else boot


# ───────────────────────────── sockets ─────────────────────────────
def _consume_sockets(config):
    from esphome.components import socket

    socket.consume_sockets(1, "ap_portal", socket.SocketType.UDP)(config)  # DNS
    return config


CONFIG_SCHEMA = cv.All(CONFIG_SCHEMA, _consume_sockets)


# ───────────────────────────── actions ─────────────────────────────
ID_SCHEMA = automation.maybe_simple_id({cv.GenerateID(): cv.use_id(ApPortal)})


@automation.register_action("ap_portal.scan", ScanAction, ID_SCHEMA, synchronous=True)
async def scan_to_code(config, action_id, template_arg, args):
    paren = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, paren)


def _ap_password(value):
    value = cv.string_strict(value)
    if value and not 8 <= len(value) <= 63:
        raise cv.Invalid("an access point password is empty (open network) or 8..63 characters")
    return value


@automation.register_action(
    "ap_portal.set_password",
    SetPasswordAction,
    cv.Schema(
        {
            cv.GenerateID(): cv.use_id(ApPortal),
            cv.Required(CONF_PASSWORD): cv.sensitive(cv.templatable(_ap_password)),
        }
    ),
    synchronous=True,
)
async def set_password_to_code(config, action_id, template_arg, args):
    paren = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, paren)
    pw = await cg.templatable(config[CONF_PASSWORD], args, cg.std_string)
    cg.add(var.set_password(pw))
    return var


@automation.register_action(
    "ap_portal.reset_password", ResetPasswordAction, ID_SCHEMA, synchronous=True
)
async def reset_password_to_code(config, action_id, template_arg, args):
    paren = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, paren)
