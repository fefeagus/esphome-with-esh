import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import esp32
from esphome.components import sensor
from esphome.const import CONF_ID, CONF_PASSWORD, CONF_SSID

DEPENDENCIES = ["esp32"]
AUTO_LOAD = ["network", "sensor"]
CONFLICTS_WITH = ["wifi"]
wifi_mesh_ns = cg.esphome_ns.namespace("wifi_mesh")
WiFiMesh = wifi_mesh_ns.class_("WiFiMesh", cg.Component)
CONF_ROLE = "role"
CONF_MESH_ID = "mesh_id"
CONF_CHANNEL = "channel"
CONF_NODE_ID = "node_id"
CONF_LOCAL_SENSORS = "local_sensors"
CONF_REMOTE_SENSORS = "remote_sensors"


def _mesh_identifier(value):
    value = cv.string(value)
    if not value or len(value) > 23 or not value.isascii() or not all(
        char.isalnum() or char in "_.-" for char in value
    ):
        raise cv.Invalid("must be 1-23 ASCII letters, digits, dots, underscores, or hyphens")
    return value


_LOCAL_SENSOR_SCHEMA = cv.Schema(
    {
        cv.Required("sensor"): cv.use_id(sensor.Sensor),
        cv.Required("key"): _mesh_identifier,
    }
)
_REMOTE_SENSOR_SCHEMA = sensor.sensor_schema().extend(
    {
        cv.Required(CONF_NODE_ID): _mesh_identifier,
        cv.Required("key"): _mesh_identifier,
    }
)
_ROUTER_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_SSID): cv.ssid,
        cv.Required(CONF_PASSWORD): cv.All(
            cv.string, cv.Any(cv.Length(max=0), cv.Length(min=8, max=63))
        ),
    }
)
_MESH_OPTIONS_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_ROLE): cv.one_of("root", "child", lower=True),
        cv.Required(CONF_MESH_ID): cv.int_range(min=0, max=255),
        cv.Required(CONF_CHANNEL): cv.int_range(min=1, max=13),
        cv.Required(CONF_NODE_ID): _mesh_identifier,
        cv.Required("mesh_ssid"): cv.ssid,
        cv.Required("mesh_password"): cv.All(cv.string, cv.Length(min=8, max=63)),
        cv.Optional("router"): _ROUTER_SCHEMA,
        cv.Optional("join_without_router"): cv.boolean,
        cv.Optional("maximum_level", default=6): cv.int_range(min=1, max=15),
        cv.Optional(CONF_LOCAL_SENSORS, default=[]): cv.ensure_list(_LOCAL_SENSOR_SCHEMA),
        cv.Optional(CONF_REMOTE_SENSORS, default=[]): cv.ensure_list(_REMOTE_SENSOR_SCHEMA),
    }
)

CONFIG_SCHEMA = cv.All(
    cv.Schema({cv.GenerateID(): cv.declare_id(WiFiMesh)})
    .extend(_MESH_OPTIONS_SCHEMA)
    .extend(cv.COMPONENT_SCHEMA),
    cv.only_on_esp32,
    esp32.only_on_variant(supported=[esp32.VARIANT_ESP32, esp32.VARIANT_ESP32C3]),
    cv.only_with_framework("esp-idf"),
)


def _validate_mesh_role(config):
    if config[CONF_ROLE] == "root" and "router" in config:
        raise cv.Invalid("the mesh root omits router credentials")
    if config[CONF_ROLE] == "root" and config.get("join_without_router", True) is False:
        raise cv.Invalid("the root must be allowed to form a mesh without router credentials")
    if config[CONF_ROLE] == "child" and config.get(CONF_REMOTE_SENSORS):
        raise cv.Invalid("remote_sensors are only configured on the root")
    return config


_MESH_OPTIONS_SCHEMA = cv.All(_MESH_OPTIONS_SCHEMA, _validate_mesh_role)


def _join_without_router(config):
    return config.get("join_without_router", config[CONF_ROLE] == "root" or "router" not in config)


async def to_code(config):
    esp32.add_idf_sdkconfig_option("CONFIG_MESH_LITE_ENABLE", True)
    esp32.add_idf_sdkconfig_option("CONFIG_LWIP_DHCPS", True)
    esp32.add_idf_sdkconfig_option("CONFIG_LWIP_DHCPS_MAX_STATION_NUM", 8)
    esp32.add_idf_sdkconfig_option("CONFIG_BRIDGE_ETHERNET_NETIF_ENABLE", False)
    esp32.add_idf_component(name="espressif/iot_bridge", ref="1.0.1")
    esp32.add_idf_component(name="espressif/mesh_lite", ref="1.0.2")
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_mesh_ssid(config["mesh_ssid"]))
    cg.add(var.set_mesh_password(config["mesh_password"]))
    cg.add(var.set_role(config[CONF_ROLE] == "root"))
    cg.add(var.set_mesh_id(config[CONF_MESH_ID]))
    cg.add(var.set_channel(config[CONF_CHANNEL]))
    cg.add(var.set_node_id(config[CONF_NODE_ID]))
    cg.add(var.set_join_without_router(_join_without_router(config)))
    cg.add(var.set_maximum_level(config["maximum_level"]))
    if router := config.get("router"):
        cg.add(var.set_router_ssid(router[CONF_SSID]))
        cg.add(var.set_router_password(router[CONF_PASSWORD]))
    for local in config[CONF_LOCAL_SENSORS]:
        local_sensor = await cg.get_variable(local["sensor"])
        cg.add(var.add_local_sensor(local_sensor, local["key"]))
    for remote in config[CONF_REMOTE_SENSORS]:
        remote_sensor = await sensor.new_sensor(remote)
        cg.add(var.add_remote_sensor(remote_sensor, remote[CONF_NODE_ID], remote["key"]))
