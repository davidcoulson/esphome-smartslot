# SPDX-License-Identifier: GPL-2.0-or-later
from pathlib import Path

import esphome.codegen as cg
from esphome import pins
import esphome.config_validation as cv
from esphome.components.esp32 import add_idf_component
from esphome.const import (
    CONF_ID,
    CONF_PASSWORD,
    CONF_PORT,
    CONF_RX_PIN,
    CONF_TX_PIN,
    CONF_USERNAME,
)

CODEOWNERS = ["@davidcoulson"]
DEPENDENCIES = ["esp32", "network"]
AUTO_LOAD = []

CONF_SMARTSLOT_ID = "smartslot_id"
CONF_UART_SEL_PIN = "uart_sel_pin"
CONF_UART_SEL_LEVEL = "uart_sel_level"
CONF_UART_NUM = "uart_num"
CONF_CMDSRC = "cmdsrc"
CONF_BAUD_RATE = "baud_rate"
CONF_NUT = "nut"

smartslot_ns = cg.esphome_ns.namespace("smartslot")
SmartSlot = smartslot_ns.class_("SmartSlot", cg.PollingComponent)

NUT_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_PORT, default=3493): cv.port,
        cv.Optional(CONF_USERNAME, default=""): cv.string,
        cv.Optional(CONF_PASSWORD, default=""): cv.string,
    }
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(SmartSlot),
        cv.Optional(CONF_TX_PIN, default="GPIO47"): pins.internal_gpio_output_pin_number,
        cv.Optional(CONF_RX_PIN, default="GPIO48"): pins.internal_gpio_input_pin_number,
        cv.Optional(CONF_UART_SEL_PIN, default="GPIO53"): pins.internal_gpio_output_pin_number,
        cv.Optional(CONF_UART_SEL_LEVEL, default=0): cv.one_of(0, 1, int=True),
        cv.Optional(CONF_UART_NUM, default=1): cv.int_range(min=0, max=4),
        cv.Optional(CONF_CMDSRC, default="smartslot1"): cv.string,
        cv.Optional(CONF_BAUD_RATE, default=9600): cv.int_range(min=1200, max=115200),
        cv.Optional(CONF_NUT, default={}): NUT_SCHEMA,
    }
).extend(cv.polling_component_schema("10s"))


async def to_code(config):
    # The component dir is itself the IDF component holding the C sources (see CMakeLists.txt).
    add_idf_component(name="smartslot", path=str(Path(__file__).parent.resolve()))
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(
        var.set_pins(
            config[CONF_TX_PIN],
            config[CONF_RX_PIN],
            config[CONF_UART_SEL_PIN],
            config[CONF_UART_SEL_LEVEL],
            config[CONF_UART_NUM],
        )
    )
    cg.add(var.set_driver(config[CONF_CMDSRC], config[CONF_BAUD_RATE]))
    nut = config[CONF_NUT]
    cg.add(var.set_nut(nut[CONF_PORT], nut[CONF_USERNAME], nut[CONF_PASSWORD]))
