import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    DEVICE_CLASS_DURATION,
    ENTITY_CATEGORY_DIAGNOSTIC,
    ICON_COUNTER,
    ICON_TIMER,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_TOTAL_INCREASING,
    UNIT_EMPTY,
    UNIT_SECOND,
)

from .. import CONF_MESHTASTIC_ID, Meshtastic

CONF_NODES_ONLINE = "nodes_online"
CONF_NODES_KNOWN = "nodes_known"
CONF_NEIGHBORS = "neighbors"
CONF_LAST_RX_AGE = "last_rx_age"
CONF_RX_PACKETS = "rx_packets"
CONF_TX_PACKETS = "tx_packets"
CONF_RELAYED_PACKETS = "relayed_packets"
CONF_DROPPED_DUPLICATE = "dropped_duplicate"
CONF_NO_KEY = "no_key"
CONF_DECODE_FAILED = "decode_failed"


def _count(icon):
    return sensor.sensor_schema(
        unit_of_measurement=UNIT_EMPTY,
        icon=icon,
        accuracy_decimals=0,
        state_class=STATE_CLASS_MEASUREMENT,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    )


def _counter():
    return sensor.sensor_schema(
        unit_of_measurement=UNIT_EMPTY,
        icon=ICON_COUNTER,
        accuracy_decimals=0,
        state_class=STATE_CLASS_TOTAL_INCREASING,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    )


_SENSORS = {
    CONF_NODES_ONLINE: (_count("mdi:radio-tower"), "set_nodes_online_sensor"),
    CONF_NODES_KNOWN: (_count("mdi:account-group"), "set_nodes_known_sensor"),
    CONF_NEIGHBORS: (_count("mdi:lan"), "set_neighbors_sensor"),
    CONF_LAST_RX_AGE: (
        sensor.sensor_schema(
            unit_of_measurement=UNIT_SECOND,
            device_class=DEVICE_CLASS_DURATION,
            icon=ICON_TIMER,
            accuracy_decimals=0,
            state_class=STATE_CLASS_MEASUREMENT,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        "set_last_rx_age_sensor",
    ),
    CONF_RX_PACKETS: (_counter(), "set_rx_packets_sensor"),
    CONF_TX_PACKETS: (_counter(), "set_tx_packets_sensor"),
    CONF_RELAYED_PACKETS: (_counter(), "set_relayed_packets_sensor"),
    CONF_DROPPED_DUPLICATE: (_counter(), "set_dropped_duplicate_sensor"),
    CONF_NO_KEY: (_counter(), "set_no_key_sensor"),
    CONF_DECODE_FAILED: (_counter(), "set_decode_failed_sensor"),
}

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_MESHTASTIC_ID): cv.use_id(Meshtastic),
        **{cv.Optional(key): schema for key, (schema, _setter) in _SENSORS.items()},
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_MESHTASTIC_ID])
    for key, (_schema, setter) in _SENSORS.items():
        if key in config:
            cg.add(getattr(parent, setter)(await sensor.new_sensor(config[key])))
