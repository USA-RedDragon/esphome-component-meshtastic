import base64
import binascii

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.components import sx126x, sx127x
from esphome.const import CONF_ID, CONF_NAME, CONF_TRIGGER_ID
from esphome.core import CORE

from . import _enums

CODEOWNERS = ["@USA-RedDragon"]
MULTI_CONF = True

CONF_RADIO = "radio"
CONF_LONG_NAME = "long_name"
CONF_SHORT_NAME = "short_name"
CONF_NODE_NUM = "node_num"
CONF_CHANNELS = "channels"
CONF_PSK = "psk"
CONF_UPLINK = "uplink"
CONF_DOWNLINK = "downlink"
CONF_ROLE = "role"
CONF_HOP_LIMIT = "hop_limit"
CONF_NODE_INFO_INTERVAL = "node_info_interval"
CONF_HW_MODEL = "hw_model"
CONF_NODE_DB_SIZE = "node_db_size"
CONF_ON_PACKET = "on_packet"
CONF_ON_TEXT = "on_text"
CONF_ON_NODEINFO = "on_nodeinfo"
CONF_ON_POSITION = "on_position"

MIN_NODE_INFO_INTERVAL_MS = 60 * 60 * 1000


def default_node_db_size():
    if CORE.is_esp8266:
        return 40
    if CORE.is_esp32:
        return 100
    return 80


def validate_node_info_interval(value):
    value = cv.positive_time_period_milliseconds(value)
    if value.total_milliseconds < MIN_NODE_INFO_INTERVAL_MS:
        raise cv.Invalid("node_info_interval must be at least 1h (Meshtastic minimum)")
    return value

# Meshtastic's well-known default channel key ("AQ==" index 1 expands to this).
DEFAULT_PSK = list(base64.b64decode("1PG7OiApB1nwvP+rz05pAQ=="))

meshtastic_ns = cg.esphome_ns.namespace("meshtastic")
Meshtastic = meshtastic_ns.class_("Meshtastic", cg.Component)

PacketTrigger = meshtastic_ns.class_(
    "PacketTrigger",
    automation.Trigger.template(
        cg.uint32, cg.uint32, cg.uint32, cg.std_vector.template(cg.uint8), cg.float_, cg.float_
    ),
)
TextTrigger = meshtastic_ns.class_(
    "TextTrigger",
    automation.Trigger.template(cg.uint32, cg.uint32, cg.uint8, cg.std_string, cg.float_, cg.float_),
)
NodeInfoTrigger = meshtastic_ns.class_(
    "NodeInfoTrigger",
    automation.Trigger.template(cg.uint32, cg.std_string, cg.std_string, cg.uint32, cg.uint32),
)
PositionTrigger = meshtastic_ns.class_(
    "PositionTrigger",
    automation.Trigger.template(cg.uint32, cg.double, cg.double, cg.int32, cg.uint32, cg.float_, cg.float_),
)


def _expand_psk_index(idx):
    if idx == 0:
        return []
    if idx == 1:
        return list(DEFAULT_PSK)
    if 2 <= idx <= 10:
        key = list(DEFAULT_PSK)
        key[-1] = (key[-1] + idx - 1) & 0xFF
        return key
    raise cv.Invalid(f"PSK index must be 0-10, got {idx}")


def validate_psk(value):
    if isinstance(value, int):
        return _expand_psk_index(value)
    if not isinstance(value, str):
        raise cv.Invalid("psk must be a string or an index 0-10")
    text = value.strip()
    low = text.lower()
    if low == "none":
        return []
    if low == "default":
        return list(DEFAULT_PSK)
    try:
        return _expand_psk_index(int(text, 0))
    except ValueError:
        pass
    hexstr = text[2:] if low.startswith("0x") else text
    if len(hexstr) in (32, 64) and all(c in "0123456789abcdefABCDEF" for c in hexstr):
        return list(bytes.fromhex(hexstr))
    try:
        raw = base64.b64decode(text, validate=True)
    except (binascii.Error, ValueError):
        raise cv.Invalid("psk must be 'none', 'default', an index 0-10, hex, or base64 (16/32 bytes)")
    if len(raw) == 0:
        return []
    if len(raw) == 1:
        return _expand_psk_index(raw[0])
    if len(raw) in (16, 32):
        return list(raw)
    raise cv.Invalid("psk base64 must decode to 1, 16, or 32 bytes")


CHANNEL_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_NAME): cv.All(cv.string, cv.Length(max=11)),
        cv.Optional(CONF_PSK, default="default"): validate_psk,
        cv.Optional(CONF_UPLINK, default=False): cv.boolean,
        cv.Optional(CONF_DOWNLINK, default=False): cv.boolean,
    }
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(Meshtastic),
        cv.Required(CONF_RADIO): cv.Any(
            cv.use_id(sx126x.SX126x), cv.use_id(sx127x.SX127x)
        ),
        cv.Optional(CONF_LONG_NAME): cv.All(cv.string, cv.Length(max=40)),
        cv.Optional(CONF_SHORT_NAME): cv.All(cv.string, cv.Length(max=4)),
        cv.Optional(CONF_NODE_NUM): cv.hex_uint32_t,
        cv.Optional(CONF_ROLE, default="CLIENT"): cv.enum(_enums.ROLES, upper=True),
        cv.Optional(CONF_HOP_LIMIT, default=3): cv.int_range(min=0, max=7),
        cv.Optional(CONF_NODE_INFO_INTERVAL, default="3h"): validate_node_info_interval,
        cv.Optional(CONF_HW_MODEL, default="DIY_V1"): cv.enum(_enums.HARDWARE_MODELS, upper=True),
        cv.Optional(CONF_NODE_DB_SIZE): cv.int_range(min=0, max=500),  # 0 disables the node DB
        cv.Optional(CONF_ON_PACKET): automation.validate_automation(
            {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(PacketTrigger)}
        ),
        cv.Optional(CONF_ON_TEXT): automation.validate_automation(
            {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(TextTrigger)}
        ),
        cv.Optional(CONF_ON_NODEINFO): automation.validate_automation(
            {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(NodeInfoTrigger)}
        ),
        cv.Optional(CONF_ON_POSITION): automation.validate_automation(
            {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(PositionTrigger)}
        ),
        cv.Optional(CONF_CHANNELS): cv.ensure_list(CHANNEL_SCHEMA),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    cg.add_library("nanopb/Nanopb", "0.4.91")

    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    radio = await cg.get_variable(config[CONF_RADIO])
    cg.add(var.set_radio(radio))

    if CONF_LONG_NAME in config:
        cg.add(var.set_long_name(config[CONF_LONG_NAME]))
    if CONF_SHORT_NAME in config:
        cg.add(var.set_short_name(config[CONF_SHORT_NAME]))
    if CONF_NODE_NUM in config:
        cg.add(var.set_node_num(config[CONF_NODE_NUM]))
    cg.add(var.set_role(config[CONF_ROLE]))
    cg.add(var.set_hop_limit(config[CONF_HOP_LIMIT]))
    cg.add(var.set_node_info_interval(config[CONF_NODE_INFO_INTERVAL]))
    cg.add(var.set_hw_model(config[CONF_HW_MODEL]))
    node_db_size = config[CONF_NODE_DB_SIZE] if CONF_NODE_DB_SIZE in config else default_node_db_size()
    cg.add(var.set_node_db_size(node_db_size))

    for ch in config.get(CONF_CHANNELS, []):
        cg.add(var.add_channel(ch[CONF_NAME], ch[CONF_PSK], ch[CONF_UPLINK], ch[CONF_DOWNLINK]))

    for conf in config.get(CONF_ON_PACKET, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(
            trigger,
            [
                (cg.uint32, "from"),
                (cg.uint32, "to"),
                (cg.uint32, "portnum"),
                (cg.std_vector.template(cg.uint8), "payload"),
                (cg.float_, "rssi"),
                (cg.float_, "snr"),
            ],
            conf,
        )
    for conf in config.get(CONF_ON_TEXT, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(
            trigger,
            [
                (cg.uint32, "from"),
                (cg.uint32, "to"),
                (cg.uint8, "channel"),
                (cg.std_string, "text"),
                (cg.float_, "rssi"),
                (cg.float_, "snr"),
            ],
            conf,
        )
    for conf in config.get(CONF_ON_NODEINFO, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(
            trigger,
            [
                (cg.uint32, "from"),
                (cg.std_string, "long_name"),
                (cg.std_string, "short_name"),
                (cg.uint32, "hw_model"),
                (cg.uint32, "role"),
            ],
            conf,
        )
    for conf in config.get(CONF_ON_POSITION, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(
            trigger,
            [
                (cg.uint32, "from"),
                (cg.double, "latitude"),
                (cg.double, "longitude"),
                (cg.int32, "altitude"),
                (cg.uint32, "time"),
                (cg.float_, "rssi"),
                (cg.float_, "snr"),
            ],
            conf,
        )
