import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor
from esphome.const import ENTITY_CATEGORY_DIAGNOSTIC

from .. import CONF_MESHTASTIC_ID, Meshtastic

CONF_NODE_ID = "node_id"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_MESHTASTIC_ID): cv.use_id(Meshtastic),
        cv.Optional(CONF_NODE_ID): text_sensor.text_sensor_schema(
            icon="mdi:identifier",
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_MESHTASTIC_ID])
    if CONF_NODE_ID in config:
        cg.add(parent.set_node_id_text_sensor(await text_sensor.new_text_sensor(config[CONF_NODE_ID])))
