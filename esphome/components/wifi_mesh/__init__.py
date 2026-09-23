import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

DEPENDENCIES = ["wifi"]

wifi_mesh_ns = cg.esphome_ns.namespace("wifi_mesh")
WiFiMesh = wifi_mesh_ns.class_("WiFiMesh", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {cv.GenerateID(): cv.declare_id(WiFiMesh)}
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_wifi(cg.RawExpression("wifi::global_wifi_component")))
