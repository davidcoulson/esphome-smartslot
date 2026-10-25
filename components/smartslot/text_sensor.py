# SPDX-License-Identifier: GPL-2.0-or-later
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor

from . import CONF_SMARTSLOT_ID, SmartSlot

CONF_VARIABLE = "variable"

CONFIG_SCHEMA = text_sensor.text_sensor_schema().extend(
    {
        cv.GenerateID(CONF_SMARTSLOT_ID): cv.use_id(SmartSlot),
        cv.Required(CONF_VARIABLE): cv.string,
    }
)


async def to_code(config):
    var = await text_sensor.new_text_sensor(config)
    parent = await cg.get_variable(config[CONF_SMARTSLOT_ID])
    cg.add(parent.register_text_sensor(var, config[CONF_VARIABLE]))
