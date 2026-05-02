import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor

DEPENDENCIES = ["network"]

snmp_sensor_ns = cg.esphome_ns.namespace("snmp_sensor")
SNMPSensor = snmp_sensor_ns.class_("SNMPSensor", sensor.Sensor, cg.PollingComponent)

CONF_HOST = "host"
CONF_PORT = "port"
CONF_COMMUNITY = "community"
CONF_OID = "oid"

CONFIG_SCHEMA = (
    sensor.sensor_schema(SNMPSensor, accuracy_decimals=0)
    .extend(
        {
            cv.Required(CONF_HOST): cv.string,
            cv.Optional(CONF_PORT, default=161): cv.port,
            cv.Optional(CONF_COMMUNITY, default="public"): cv.string,
            cv.Required(CONF_OID): cv.string,
        }
    )
    .extend(cv.polling_component_schema("30s"))
)


async def to_code(config):
    var = await sensor.new_sensor(config)
    await cg.register_component(var, config)
    cg.add(var.set_host(config[CONF_HOST]))
    cg.add(var.set_port(config[CONF_PORT]))
    cg.add(var.set_community(config[CONF_COMMUNITY]))
    cg.add(var.set_oid(config[CONF_OID]))
