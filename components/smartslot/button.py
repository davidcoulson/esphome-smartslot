# SPDX-License-Identifier: GPL-2.0-or-later
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import button, switch

from . import CONF_SMARTSLOT_ID, SmartSlot, smartslot_ns

AUTO_LOAD = ["switch"]
CONF_COMMAND = "command"
CONF_ARM = "arm"

SmartSlotButton = smartslot_ns.class_("SmartSlotButton", button.Button, cg.Parented.template(SmartSlot))

CONFIG_SCHEMA = button.button_schema(SmartSlotButton).extend(
    {
        cv.GenerateID(CONF_SMARTSLOT_ID): cv.use_id(SmartSlot),
        cv.Required(CONF_COMMAND): cv.string,
        cv.Optional(CONF_ARM): cv.use_id(switch.Switch),
    }
)


async def to_code(config):
    var = await button.new_button(config)
    await cg.register_parented(var, config[CONF_SMARTSLOT_ID])
    cg.add(var.set_command(config[CONF_COMMAND]))
    if CONF_ARM in config:
        cg.add(var.set_arm(await cg.get_variable(config[CONF_ARM])))
