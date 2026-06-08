import esphome.codegen as cg
from esphome.components.packet_transport import (
    new_packet_transport,
    transport_schema,
)
import esphome.config_validation as cv
from esphome.cpp_types import PollingComponent

from .. import CONF_CHANNEL, Meshtastic, meshtastic_ns

CONF_MESHTASTIC_ID = "meshtastic_id"

MeshtasticTransport = meshtastic_ns.class_("MeshtasticTransport", cg.Parented.template(Meshtastic), PollingComponent)

CONFIG_SCHEMA = transport_schema(MeshtasticTransport).extend(
    {
        cv.GenerateID(CONF_MESHTASTIC_ID): cv.use_id(Meshtastic),
        cv.Required(CONF_CHANNEL): cv.string,
    }
)


async def to_code(config):
    var, _ = await new_packet_transport(config)
    await cg.register_parented(var, config[CONF_MESHTASTIC_ID])
    cg.add(var.set_channel(config[CONF_CHANNEL]))
