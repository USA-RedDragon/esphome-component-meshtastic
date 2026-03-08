import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sx126x, sx127x
from esphome.const import CONF_ID

CODEOWNERS = ["@USA-RedDragon"]
MULTI_CONF = True

CONF_RADIO = "radio"
CONF_LONG_NAME = "long_name"
CONF_SHORT_NAME = "short_name"
CONF_NODE_NUM = "node_num"

meshtastic_ns = cg.esphome_ns.namespace("meshtastic")
Meshtastic = meshtastic_ns.class_("Meshtastic", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(Meshtastic),
        cv.Required(CONF_RADIO): cv.Any(
            cv.use_id(sx126x.SX126x), cv.use_id(sx127x.SX127x)
        ),
        cv.Optional(CONF_LONG_NAME): cv.All(cv.string, cv.Length(max=40)),
        cv.Optional(CONF_SHORT_NAME): cv.All(cv.string, cv.Length(max=4)),
        cv.Optional(CONF_NODE_NUM): cv.hex_uint32_t,
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
