"""ap_portal — the device's own Wi-Fi access point next to the router connection.

DNS hijack for the phone's "Sign in to network" window, an own landing page (redirect / page /
gate), and a quiet mode that keeps phones on the access point when there is no router.
https://github.com/daimonkor/esphome-ap-portal
"""

import ipaddress
import logging

from esphome import automation
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    CONF_MANUAL_IP,
    CONF_PASSWORD,
    CONF_PATH,
    CONF_SSID,
    CONF_STATIC_IP,
    CONF_TRIGGER_ID,
    __version__ as ESPHOME_VERSION,
)
from esphome.core import CORE
import esphome.final_validate as fv

_LOGGER = logging.getLogger(__name__)

CODEOWNERS = ["@daimonkor"]
DEPENDENCIES = ["wifi", "esp32"]
AUTO_LOAD = ["web_server_base", "captive_portal", "network", "socket"]

# The library leans on ESPHome internals (fallback AP, captive_portal quiet mode, handler order),
# so it is tested against one ESPHome version. Others may work; the build says so.
TESTED_ESPHOME = "2026.10"

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
CONF_ON_CLIENT_CONNECTED = "on_client_connected"
CONF_ON_CLIENT_DISCONNECTED = "on_client_disconnected"
CONF_ON_AP_RESTORED = "on_ap_restored"
CONF_ON_PORTAL_START = "on_portal_start"
CONF_ON_PORTAL_STOP = "on_portal_stop"

STYLE_KEYS = (CONF_TITLE, CONF_ACCENT_COLOR, CONF_ICON_SVG, CONF_PRIMARY_LABEL, CONF_SECONDARY)
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


def _hex_color(value):
    value = cv.string_strict(value).strip()
    h = value[1:] if value.startswith("#") else value
    if len(h) == 3:
        h = "".join(c * 2 for c in h)
    if len(h) != 6 or any(c not in "0123456789abcdefABCDEF" for c in h):
        raise cv.Invalid(f"'{value}' is not a color like #3d7bff")
    return "#" + h.lower()


# Inline text, or `file: path` read at build time.
TEXT_OR_FILE = cv.Any(
    cv.Schema({cv.Required(CONF_FILE): cv.file_}),
    cv.string_strict,
)

LANDING_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_MODE, default="redirect"): cv.one_of(*MODES, lower=True),
        cv.Optional(CONF_PATH, default="/welcome"): _url_path,
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
        cv.Optional(CONF_ENTER_PATH): _url_path,
        cv.Optional(CONF_ENTER_WINDOW): cv.All(
            cv.positive_time_period_milliseconds,
            cv.Range(min=cv.TimePeriod(minutes=1), max=cv.TimePeriod(hours=24)),
        ),
        cv.Optional(CONF_PASS_PATHS): cv.ensure_list(_url_path),
    }
)


def _validate_landing(config):
    if CONF_HTML in config:
        clash = [k for k in STYLE_KEYS if k in config]
        if clash:
            raise cv.Invalid(
                f"'html' is your whole page, so {', '.join(repr(k) for k in clash)} "
                "would be ignored. Remove them, or remove 'html' to use the built-in page."
            )
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
        }
    ).extend(cv.COMPONENT_SCHEMA),
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

    if not ESPHOME_VERSION.startswith(TESTED_ESPHOME + "."):
        _LOGGER.warning(
            "ap_portal is tested with ESPHome %s.x; you have %s. It uses ESPHome Wi-Fi "
            "internals — check the access point and quiet mode after updating.",
            TESTED_ESPHOME,
            ESPHOME_VERSION,
        )
    return config


FINAL_VALIDATE_SCHEMA = _final_validate


def _read(value):
    if isinstance(value, dict):
        return CORE.relative_config_path(value[CONF_FILE]).read_text(encoding="utf-8")
    return value


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

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
        cg.add(var.set_html(_read(landing[CONF_HTML])))
    if CONF_TITLE in landing:
        cg.add(var.set_title(landing[CONF_TITLE]))
    if CONF_ACCENT_COLOR in landing:
        cg.add(var.set_accent_color(landing[CONF_ACCENT_COLOR]))
    if CONF_ICON_SVG in landing:
        cg.add(var.set_icon_svg(_read(landing[CONF_ICON_SVG])))
    if CONF_PRIMARY_LABEL in landing:
        cg.add(var.set_primary_label(landing[CONF_PRIMARY_LABEL]))
    if CONF_SECONDARY in landing:
        sec = landing[CONF_SECONDARY]
        cg.add(var.set_secondary(sec[CONF_LABEL], sec[CONF_HREF]))
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
    for key in (CONF_ON_AP_RESTORED, CONF_ON_PORTAL_START, CONF_ON_PORTAL_STOP):
        for conf in config.get(key, []):
            trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
            await automation.build_automation(trigger, [], conf)

    if config[CONF_PASSWORD_PERSIST]:
        cg.add_define("USE_AP_PORTAL_PASSWORD_PERSIST")


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
            cv.Required(CONF_PASSWORD): cv.templatable(_ap_password),
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
