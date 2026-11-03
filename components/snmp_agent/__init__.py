# SPDX-License-Identifier: GPL-2.0-or-later
import re

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor, sensor, text_sensor
from esphome.const import CONF_ID, CONF_NAME, CONF_PORT, CONF_SENSOR, CONF_STATUS, CONF_TYPE, CONF_VALUE

CODEOWNERS = ["@davidcoulson"]
DEPENDENCIES = ["esp32", "network"]
AUTO_LOAD = ["binary_sensor", "sensor", "text_sensor"]

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
CONF_HOST = "host"
CONF_V2C = "v2c"
CONF_USERS = "users"
CONF_AUTH_PROTOCOL = "auth_protocol"
CONF_AUTH_PASSWORD = "auth_password"
CONF_PRIV_PROTOCOL = "priv_protocol"
CONF_PRIV_PASSWORD = "priv_password"
CONF_TRAPS = "traps"
CONF_LINK = "link"
CONF_RECEIVERS = "receivers"
CONF_USER = "user"

snmp_ns = cg.esphome_ns.namespace("snmp_agent")
SnmpAgent = snmp_ns.class_("SnmpAgent", cg.PollingComponent)

# Values of snmp::Type in snmp_core.h
TYPES = {"integer": 0, "gauge": 1, "timeticks": 2, "string": 3, "oid": 4}
# Values of snmp::Auth and snmp::Priv in snmp_crypto.h
AUTH = {"none": 0, "md5": 1, "sha": 2, "sha224": 3, "sha256": 4, "sha384": 5, "sha512": 6}
PRIV = {"none": 0, "aes": 1}


def valid_oid(value):
    value = cv.string_strict(value).lstrip(".")
    if not re.fullmatch(r"\d+(\.\d+)+", value):
        raise cv.Invalid(f"'{value}' is not a dotted OID such as 1.3.6.1.2.1.1.5.0")
    return value


def valid_user(config):
    if config[CONF_AUTH_PROTOCOL] != "none" and CONF_AUTH_PASSWORD not in config:
        raise cv.Invalid("auth_password is required with an auth_protocol")
    if config[CONF_PRIV_PROTOCOL] != "none":
        if config[CONF_AUTH_PROTOCOL] == "none":
            raise cv.Invalid("priv_protocol needs an auth_protocol (SNMPv3 has no privacy without authentication)")
        if CONF_PRIV_PASSWORD not in config:
            raise cv.Invalid("priv_password is required with a priv_protocol")
    return config


PASSWORD = cv.All(cv.string, cv.Length(min=8))  # RFC 3414 11.2: at least 8 characters

USER_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.Required(CONF_NAME): cv.All(cv.string, cv.Length(min=1, max=32)),
            cv.Optional(CONF_AUTH_PROTOCOL, default="none"): cv.one_of(*AUTH, lower=True),
            cv.Optional(CONF_AUTH_PASSWORD): PASSWORD,
            cv.Optional(CONF_PRIV_PROTOCOL, default="none"): cv.one_of(*PRIV, lower=True),
            cv.Optional(CONF_PRIV_PASSWORD): PASSWORD,
        }
    ),
    valid_user,
)

RECEIVER_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.Required(CONF_HOST): cv.ipv4address,
            cv.Optional(CONF_PORT, default=162): cv.port,
            cv.Optional(CONF_COMMUNITY): cv.string,  # v2c; defaults to the agent's community
            cv.Optional(CONF_USER): cv.string,  # v3, from this user
        }
    ),
    cv.has_at_most_one_key(CONF_COMMUNITY, CONF_USER),
)

TRAPS_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_STATUS): cv.use_id(text_sensor.TextSensor),  # ups.status: on battery, low battery, ...
        cv.Optional(CONF_LINK): cv.use_id(binary_sensor.BinarySensor),  # UPS communication up/down
        cv.Required(CONF_RECEIVERS): cv.All(cv.ensure_list(RECEIVER_SCHEMA), cv.Length(min=1)),
    }
)


def valid_agent(config):
    users = [u[CONF_NAME] for u in config[CONF_USERS]]
    if len(set(users)) != len(users):
        raise cv.Invalid("SNMPv3 user names must be unique")
    for r in config.get(CONF_TRAPS, {}).get(CONF_RECEIVERS, []):
        if CONF_USER in r and r[CONF_USER] not in users:
            raise cv.Invalid(f"trap receiver {r[CONF_HOST]}: no SNMPv3 user named '{r[CONF_USER]}'")
    if not config[CONF_V2C] and not users:
        raise cv.Invalid("with v2c: false, add at least one SNMPv3 user")
    return config


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

CONFIG_SCHEMA = cv.All(
    cv.Schema(
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
            cv.Optional(CONF_V2C, default=True): cv.boolean,  # also covers v1
            cv.Optional(CONF_USERS, default=[]): cv.ensure_list(USER_SCHEMA),
            cv.Optional(CONF_TRAPS): TRAPS_SCHEMA,
        }
    ).extend(cv.polling_component_schema("1s")),
    valid_agent,
)


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
    cg.add(var.set_v2c(config[CONF_V2C]))
    for u in config[CONF_USERS]:
        cg.add(
            var.add_user(
                u[CONF_NAME],
                AUTH[u[CONF_AUTH_PROTOCOL]],
                u.get(CONF_AUTH_PASSWORD, ""),
                PRIV[u[CONF_PRIV_PROTOCOL]],
                u.get(CONF_PRIV_PASSWORD, ""),
            )
        )
    if traps := config.get(CONF_TRAPS):
        if CONF_STATUS in traps:
            cg.add(var.set_status(await cg.get_variable(traps[CONF_STATUS])))
        if CONF_LINK in traps:
            cg.add(var.set_link(await cg.get_variable(traps[CONF_LINK])))
        for r in traps[CONF_RECEIVERS]:
            cg.add(
                var.add_receiver(
                    str(r[CONF_HOST]), r[CONF_PORT], r.get(CONF_COMMUNITY, config[CONF_COMMUNITY]), r.get(CONF_USER, "")
                )
            )
