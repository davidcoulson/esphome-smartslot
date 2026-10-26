# SPDX-License-Identifier: GPL-2.0-or-later
import re

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor, text_sensor
from esphome.const import CONF_ID, CONF_PORT, CONF_SENSOR, CONF_TYPE, CONF_VALUE

CODEOWNERS = ["@davidcoulson"]
DEPENDENCIES = ["esp32", "network"]

CONF_COMMUNITY = "community"
CONF_SYS_DESCR = "sys_descr"
CONF_SYS_OBJECT_ID = "sys_object_id"
CONF_SYS_NAME = "sys_name"
CONF_SYS_CONTACT = "sys_contact"
CONF_SYS_LOCATION = "sys_location"
CONF_READINGS = "readings"
CONF_OID = "oid"
CONF_TEXT_SENSOR = "text_sensor"
CONF_SCALE = "scale"
CONF_MAP = "map"

snmp_ns = cg.esphome_ns.namespace("snmp_agent")
SnmpAgent = snmp_ns.class_("SnmpAgent", cg.PollingComponent)

# Values of snmp::Type in snmp_core.h
TYPES = {"integer": 0, "gauge": 1, "timeticks": 2, "string": 3, "oid": 4}


def valid_oid(value):
    value = cv.string_strict(value).lstrip(".")
    if not re.fullmatch(r"\d+(\.\d+)+", value):
        raise cv.Invalid(f"'{value}' is not a dotted OID such as 1.3.6.1.2.1.1.5.0")
    return value


READING_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.Required(CONF_OID): valid_oid,
            cv.Optional(CONF_SENSOR): cv.use_id(sensor.Sensor),
            cv.Optional(CONF_TEXT_SENSOR): cv.use_id(text_sensor.TextSensor),
            cv.Optional(CONF_VALUE): cv.string,  # fixed value, e.g. a manufacturer name
            cv.Optional(CONF_TYPE, default="gauge"): cv.one_of(*TYPES, lower=True),
            cv.Optional(CONF_SCALE, default=1.0): cv.float_,
            # text sensor -> number: the first space-separated token of the state that matches a key wins; "*" matches anything.
            cv.Optional(CONF_MAP): cv.Schema({cv.string: cv.int_}),
        }
    ),
    cv.has_exactly_one_key(CONF_SENSOR, CONF_TEXT_SENSOR, CONF_VALUE),
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(SnmpAgent),
        cv.Optional(CONF_PORT, default=161): cv.port,
        cv.Optional(CONF_COMMUNITY, default="public"): cv.string,
        cv.Optional(CONF_SYS_DESCR, default="APC SmartSlot network card (esphome-smartslot)"): cv.string,
        cv.Optional(CONF_SYS_OBJECT_ID, default="1.3.6.1.4.1.318.1.3.2"): valid_oid,
        cv.Optional(CONF_SYS_NAME, default=""): cv.string,  # empty = the ESPHome node name
        cv.Optional(CONF_SYS_CONTACT, default=""): cv.string,
        cv.Optional(CONF_SYS_LOCATION, default=""): cv.string,
        cv.Optional(CONF_READINGS, default=[]): cv.ensure_list(READING_SCHEMA),
    }
).extend(cv.polling_component_schema("1s"))


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(
        var.set_agent(
            config[CONF_PORT],
            config[CONF_COMMUNITY],
            config[CONF_SYS_DESCR],
            config[CONF_SYS_OBJECT_ID],
            config[CONF_SYS_NAME],
            config[CONF_SYS_CONTACT],
            config[CONF_SYS_LOCATION],
        )
    )
    for r in config[CONF_READINGS]:
        t = TYPES[r[CONF_TYPE]]
        if CONF_SENSOR in r:
            s = await cg.get_variable(r[CONF_SENSOR])
            cg.add(var.add_sensor(r[CONF_OID], s, t, r[CONF_SCALE]))
        elif CONF_TEXT_SENSOR in r:
            s = await cg.get_variable(r[CONF_TEXT_SENSOR])
            cg.add(var.add_text_sensor(r[CONF_OID], s, t, r[CONF_SCALE]))
            for token, num in r.get(CONF_MAP, {}).items():
                cg.add(var.add_map(token, num))
        else:
            cg.add(var.add_static(r[CONF_OID], r[CONF_VALUE], t))
