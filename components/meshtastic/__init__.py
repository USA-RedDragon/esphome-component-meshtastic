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
CONF_ON_TELEMETRY = "on_telemetry"
CONF_TEXT = "text"
CONF_CHANNEL = "channel"
CONF_TO = "to"
CONF_WANT_ACK = "want_ack"
# send_position
CONF_LATITUDE = "latitude"
CONF_LONGITUDE = "longitude"
CONF_ALTITUDE = "altitude"
CONF_PRECISION_BITS = "precision_bits"
# send_telemetry
CONF_BATTERY_LEVEL = "battery_level"
CONF_VOLTAGE = "voltage"
CONF_CHANNEL_UTILIZATION = "channel_utilization"
CONF_AIR_UTIL_TX = "air_util_tx"
CONF_UPTIME_SECONDS = "uptime_seconds"
# send_environment_metrics
CONF_TEMPERATURE = "temperature"
CONF_RELATIVE_HUMIDITY = "relative_humidity"
CONF_BAROMETRIC_PRESSURE = "barometric_pressure"
CONF_GAS_RESISTANCE = "gas_resistance"
CONF_CURRENT = "current"
CONF_IAQ = "iaq"
CONF_DISTANCE = "distance"
CONF_LUX = "lux"
CONF_WHITE_LUX = "white_lux"
CONF_IR_LUX = "ir_lux"
CONF_UV_LUX = "uv_lux"
CONF_WIND_DIRECTION = "wind_direction"
CONF_WIND_SPEED = "wind_speed"
CONF_WIND_GUST = "wind_gust"
CONF_WIND_LULL = "wind_lull"
CONF_WEIGHT = "weight"
CONF_RADIATION = "radiation"
CONF_RAINFALL_1H = "rainfall_1h"
CONF_RAINFALL_24H = "rainfall_24h"
CONF_SOIL_MOISTURE = "soil_moisture"
CONF_SOIL_TEMPERATURE = "soil_temperature"
CONF_ONE_WIRE_TEMPERATURE = "one_wire_temperature"

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
    automation.Trigger.template(cg.uint32, cg.uint32, cg.std_string, cg.std_string, cg.float_, cg.float_),
)
NodeInfoTrigger = meshtastic_ns.class_(
    "NodeInfoTrigger",
    automation.Trigger.template(
        cg.uint32, cg.std_string, cg.std_string, cg.std_string, cg.std_string, cg.std_string, cg.float_, cg.float_
    ),
)
PositionTrigger = meshtastic_ns.class_(
    "PositionTrigger",
    automation.Trigger.template(
        cg.uint32, cg.std_string, cg.double, cg.double, cg.int32, cg.uint32, cg.uint32, cg.float_, cg.float_
    ),
)
TelemetryTrigger = meshtastic_ns.class_(
    "TelemetryTrigger",
    automation.Trigger.template(
        cg.uint32, cg.std_string, cg.uint32, cg.float_, cg.float_, cg.float_, cg.uint32, cg.float_, cg.float_
    ),
)
SendTextAction = meshtastic_ns.class_("SendTextAction", automation.Action)
SendPositionAction = meshtastic_ns.class_("SendPositionAction", automation.Action)
SendTelemetryAction = meshtastic_ns.class_("SendTelemetryAction", automation.Action)
SendEnvironmentMetricsAction = meshtastic_ns.class_("SendEnvironmentMetricsAction", automation.Action)
SendNodeInfoAction = meshtastic_ns.class_("SendNodeInfoAction", automation.Action)


@automation.register_action(
    "meshtastic.send_text",
    SendTextAction,
    cv.Schema(
        {
            cv.GenerateID(): cv.use_id(Meshtastic),
            cv.Required(CONF_TEXT): cv.templatable(cv.string),
            cv.Optional(CONF_CHANNEL, default=""): cv.templatable(cv.string),
            cv.Optional(CONF_TO, default=0xFFFFFFFF): cv.templatable(cv.hex_uint32_t),
            cv.Optional(CONF_WANT_ACK, default=False): cv.templatable(cv.boolean),
        }
    ),
    synchronous=True,
)
async def send_text_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg, await cg.get_variable(config[CONF_ID]))
    cg.add(var.set_text(await cg.templatable(config[CONF_TEXT], args, cg.std_string)))
    cg.add(var.set_channel(await cg.templatable(config[CONF_CHANNEL], args, cg.std_string)))
    cg.add(var.set_dest(await cg.templatable(config[CONF_TO], args, cg.uint32)))
    cg.add(var.set_want_ack(await cg.templatable(config[CONF_WANT_ACK], args, cg.bool_)))
    return var


# Common channel/want_ack fields for the origination actions.
_SEND_BASE = {
    cv.GenerateID(): cv.use_id(Meshtastic),
    cv.Optional(CONF_CHANNEL, default=""): cv.templatable(cv.string),
    cv.Optional(CONF_WANT_ACK, default=False): cv.templatable(cv.boolean),
}


async def _add_channel_want_ack(var, config, args):
    cg.add(var.set_channel(await cg.templatable(config[CONF_CHANNEL], args, cg.std_string)))
    cg.add(var.set_want_ack(await cg.templatable(config[CONF_WANT_ACK], args, cg.bool_)))


@automation.register_action(
    "meshtastic.send_position",
    SendPositionAction,
    cv.Schema(
        {
            **_SEND_BASE,
            cv.Required(CONF_LATITUDE): cv.templatable(cv.float_),
            cv.Required(CONF_LONGITUDE): cv.templatable(cv.float_),
            cv.Optional(CONF_ALTITUDE, default=0): cv.templatable(cv.int_),
            cv.Optional(CONF_PRECISION_BITS, default=32): cv.templatable(cv.int_range(min=0, max=32)),
        }
    ),
    synchronous=True,
)
async def send_position_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg, await cg.get_variable(config[CONF_ID]))
    cg.add(var.set_latitude(await cg.templatable(config[CONF_LATITUDE], args, cg.double)))
    cg.add(var.set_longitude(await cg.templatable(config[CONF_LONGITUDE], args, cg.double)))
    cg.add(var.set_altitude(await cg.templatable(config[CONF_ALTITUDE], args, cg.int32)))
    cg.add(var.set_precision_bits(await cg.templatable(config[CONF_PRECISION_BITS], args, cg.uint32)))
    await _add_channel_want_ack(var, config, args)
    return var


@automation.register_action(
    "meshtastic.send_telemetry",
    SendTelemetryAction,
    cv.Schema(
        {
            **_SEND_BASE,
            cv.Optional(CONF_BATTERY_LEVEL): cv.templatable(cv.int_range(min=0, max=100)),
            cv.Optional(CONF_VOLTAGE): cv.templatable(cv.float_),
            cv.Optional(CONF_CHANNEL_UTILIZATION): cv.templatable(cv.float_),
            cv.Optional(CONF_AIR_UTIL_TX): cv.templatable(cv.float_),
            cv.Optional(CONF_UPTIME_SECONDS): cv.templatable(cv.uint32_t),
        }
    ),
    synchronous=True,
)
async def send_telemetry_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg, await cg.get_variable(config[CONF_ID]))
    if CONF_BATTERY_LEVEL in config:
        cg.add(var.set_battery_level(await cg.templatable(config[CONF_BATTERY_LEVEL], args, cg.uint32)))
    if CONF_VOLTAGE in config:
        cg.add(var.set_voltage(await cg.templatable(config[CONF_VOLTAGE], args, cg.float_)))
    if CONF_CHANNEL_UTILIZATION in config:
        cg.add(var.set_channel_utilization(await cg.templatable(config[CONF_CHANNEL_UTILIZATION], args, cg.float_)))
    if CONF_AIR_UTIL_TX in config:
        cg.add(var.set_air_util_tx(await cg.templatable(config[CONF_AIR_UTIL_TX], args, cg.float_)))
    if CONF_UPTIME_SECONDS in config:
        cg.add(var.set_uptime_seconds(await cg.templatable(config[CONF_UPTIME_SECONDS], args, cg.uint32)))
    await _add_channel_want_ack(var, config, args)
    return var


_ENV_FIELDS = [
    (CONF_TEMPERATURE, "set_temperature", cv.float_, cg.float_),
    (CONF_RELATIVE_HUMIDITY, "set_relative_humidity", cv.float_, cg.float_),
    (CONF_BAROMETRIC_PRESSURE, "set_barometric_pressure", cv.float_, cg.float_),
    (CONF_GAS_RESISTANCE, "set_gas_resistance", cv.float_, cg.float_),
    (CONF_VOLTAGE, "set_voltage", cv.float_, cg.float_),
    (CONF_CURRENT, "set_current", cv.float_, cg.float_),
    (CONF_DISTANCE, "set_distance", cv.float_, cg.float_),
    (CONF_LUX, "set_lux", cv.float_, cg.float_),
    (CONF_WHITE_LUX, "set_white_lux", cv.float_, cg.float_),
    (CONF_IR_LUX, "set_ir_lux", cv.float_, cg.float_),
    (CONF_UV_LUX, "set_uv_lux", cv.float_, cg.float_),
    (CONF_WIND_SPEED, "set_wind_speed", cv.float_, cg.float_),
    (CONF_WIND_GUST, "set_wind_gust", cv.float_, cg.float_),
    (CONF_WIND_LULL, "set_wind_lull", cv.float_, cg.float_),
    (CONF_WEIGHT, "set_weight", cv.float_, cg.float_),
    (CONF_RADIATION, "set_radiation", cv.float_, cg.float_),
    (CONF_RAINFALL_1H, "set_rainfall_1h", cv.float_, cg.float_),
    (CONF_RAINFALL_24H, "set_rainfall_24h", cv.float_, cg.float_),
    (CONF_SOIL_TEMPERATURE, "set_soil_temperature", cv.float_, cg.float_),
    (CONF_IAQ, "set_iaq", cv.int_range(min=0, max=65535), cg.uint32),
    (CONF_WIND_DIRECTION, "set_wind_direction", cv.int_range(min=0, max=359), cg.uint32),
    (CONF_SOIL_MOISTURE, "set_soil_moisture", cv.int_range(min=0, max=100), cg.uint32),
]


@automation.register_action(
    "meshtastic.send_environment_metrics",
    SendEnvironmentMetricsAction,
    cv.Schema(
        {
            **_SEND_BASE,
            **{cv.Optional(conf): cv.templatable(validator) for conf, _s, validator, _t in _ENV_FIELDS},
            cv.Optional(CONF_ONE_WIRE_TEMPERATURE): cv.templatable(cv.ensure_list(cv.float_)),
        }
    ),
    synchronous=True,
)
async def send_environment_metrics_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg, await cg.get_variable(config[CONF_ID]))
    for conf, setter, _validator, typ in _ENV_FIELDS:
        if conf in config:
            cg.add(getattr(var, setter)(await cg.templatable(config[conf], args, typ)))
    if CONF_ONE_WIRE_TEMPERATURE in config:
        templ = await cg.templatable(config[CONF_ONE_WIRE_TEMPERATURE], args, cg.std_vector.template(cg.float_))
        cg.add(var.set_one_wire_temperature(templ))
    await _add_channel_want_ack(var, config, args)
    return var


@automation.register_action(
    "meshtastic.send_node_info",
    SendNodeInfoAction,
    cv.Schema({cv.GenerateID(): cv.use_id(Meshtastic)}),
    synchronous=True,
)
async def send_node_info_to_code(config, action_id, template_arg, args):
    return cg.new_Pvariable(action_id, template_arg, await cg.get_variable(config[CONF_ID]))


def _expand_psk_index(idx):
    # Meshtastic 1-byte PSK: 0 = no encryption, otherwise the default key with its last byte
    # bumped by (idx - 1). Any byte value 0-255 is valid (idx 1 == the unmodified default key).
    if idx == 0:
        return []
    if 1 <= idx <= 255:
        key = list(DEFAULT_PSK)
        key[-1] = (key[-1] + idx - 1) & 0xFF
        return key
    raise cv.Invalid(f"PSK index must be 0-255, got {idx}")


def validate_psk(value):
    if isinstance(value, int):
        return _expand_psk_index(value)
    if not isinstance(value, str):
        raise cv.Invalid("psk must be a string or an index 0-255")
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
        raise cv.Invalid("psk must be 'none', 'default', an index 0-255, hex, or base64 (16/32 bytes)")
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
        cv.Optional(CONF_ON_TELEMETRY): automation.validate_automation(
            {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(TelemetryTrigger)}
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
                (cg.std_string, "channel"),
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
                (cg.std_string, "channel"),
                (cg.std_string, "long_name"),
                (cg.std_string, "short_name"),
                (cg.std_string, "hw_model"),
                (cg.std_string, "role"),
                (cg.float_, "rssi"),
                (cg.float_, "snr"),
            ],
            conf,
        )
    for conf in config.get(CONF_ON_POSITION, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(
            trigger,
            [
                (cg.uint32, "from"),
                (cg.std_string, "channel"),
                (cg.double, "latitude"),
                (cg.double, "longitude"),
                (cg.int32, "altitude"),
                (cg.uint32, "precision_bits"),
                (cg.uint32, "time"),
                (cg.float_, "rssi"),
                (cg.float_, "snr"),
            ],
            conf,
        )
    for conf in config.get(CONF_ON_TELEMETRY, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(
            trigger,
            [
                (cg.uint32, "from"),
                (cg.std_string, "channel"),
                (cg.uint32, "battery_level"),
                (cg.float_, "voltage"),
                (cg.float_, "channel_utilization"),
                (cg.float_, "air_util_tx"),
                (cg.uint32, "uptime_seconds"),
                (cg.float_, "rssi"),
                (cg.float_, "snr"),
            ],
            conf,
        )
