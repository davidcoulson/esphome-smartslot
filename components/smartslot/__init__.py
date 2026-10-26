# SPDX-License-Identifier: GPL-2.0-or-later
from pathlib import Path
import re

import esphome.codegen as cg
from esphome import pins
import esphome.config_validation as cv
from esphome.components import esp32, ota, time, web_server_base
from esphome.components.esp32 import add_idf_component
from esphome.components.web_server_base import CONF_WEB_SERVER_BASE_ID
from esphome.const import (
    CONF_ID,
    CONF_INTERVAL,
    CONF_SIZE,
    CONF_TIME_ID,
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
CONF_HISTORY = "history"

smartslot_ns = cg.esphome_ns.namespace("smartslot")
SmartSlot = smartslot_ns.class_("SmartSlot", cg.PollingComponent)
SmartSlotHistory = smartslot_ns.class_("SmartSlotHistory", cg.PollingComponent)

NUT_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_PORT, default=3493): cv.port,
        cv.Optional(CONF_USERNAME, default=""): cv.string,
        cv.Optional(CONF_PASSWORD, default=""): cv.string,
    }
)

def _partition_size(value):
    # Binary units like esp32: flash_size ("8MB" = 8 MiB); a bare int is bytes.
    if isinstance(value, str):
        m = re.fullmatch(r"(\d+)\s*([KM])B", value.strip().upper())
        if m is None:
            raise cv.Invalid("history size must look like 8MB or 512KB")
        value = int(m.group(1)) << (10 if m.group(2) == "K" else 20)
    value = cv.int_(value)
    if value % 0x1000 or value < 0x100000:
        raise cv.Invalid("history size must be a multiple of 4KB and at least 1MB")
    return value


# On-flash history (esp_tsdb on a LittleFS `tsdb` partition appended to the partition table).
# update_interval is how often the driver values are sampled; `interval` is how often their average is written.
HISTORY_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(SmartSlotHistory),
        cv.GenerateID(CONF_TIME_ID): cv.use_id(time.RealTimeClock),
        cv.GenerateID(CONF_WEB_SERVER_BASE_ID): cv.use_id(web_server_base.WebServerBase),
        cv.Optional(CONF_SIZE, default="8MB"): _partition_size,
        cv.Optional(CONF_INTERVAL, default="5min"): cv.All(
            cv.positive_time_period_seconds,
            cv.Range(min=cv.TimePeriod(seconds=60), max=cv.TimePeriod(seconds=3600)),
        ),
    }
).extend(cv.polling_component_schema("10s"))

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(SmartSlot),
        cv.Optional(CONF_TX_PIN, default="GPIO20"): pins.internal_gpio_output_pin_number,
        cv.Optional(CONF_RX_PIN, default="GPIO21"): pins.internal_gpio_input_pin_number,
        cv.Optional(CONF_UART_SEL_PIN, default="GPIO53"): pins.internal_gpio_output_pin_number,
        cv.Optional(CONF_UART_SEL_LEVEL, default=0): cv.one_of(0, 1, int=True),
        cv.Optional(CONF_UART_NUM, default=1): cv.int_range(min=0, max=4),
        cv.Optional(CONF_CMDSRC, default="smartslot1"): cv.string,
        cv.Optional(CONF_BAUD_RATE, default=9600): cv.int_range(min=1200, max=115200),
        cv.Optional(CONF_NUT, default={}): NUT_SCHEMA,
        cv.Optional(CONF_HISTORY): HISTORY_SCHEMA,
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
    if CONF_HISTORY in config:
        h = config[CONF_HISTORY]
        esp32.add_partition("tsdb", "data", "littlefs", h[CONF_SIZE])
        add_idf_component(name="zakery292/esp_tsdb", ref="^2.4.1")  # >= 2.4.0: sidecar header, esp32p4 target
        add_idf_component(name="joltwallet/littlefs", ref="^1.16")
        esp32.require_vfs_dir()  # esp_tsdb >= 2.4.4 keeps its sidecar in a directory (mkdir/rmdir)
        ota.request_ota_state_listeners()
        cg.add_define("USE_SMARTSLOT_HISTORY")
        hist = cg.new_Pvariable(h[CONF_ID])
        await cg.register_component(hist, h)
        cg.add(hist.set_time(await cg.get_variable(h[CONF_TIME_ID])))
        cg.add(hist.set_web(await cg.get_variable(h[CONF_WEB_SERVER_BASE_ID])))
        cg.add(hist.set_config(h[CONF_INTERVAL].total_seconds, h[CONF_SIZE]))
