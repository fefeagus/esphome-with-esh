"""Focused validation checks for the ESP-Mesh-Lite YAML options."""

import unittest

import esphome.config_validation as cv
from esphome.components.wifi_mesh import _MESH_OPTIONS_SCHEMA, _join_without_router


def _config(**overrides):
    config = {
        "role": "root",
        "node_id": "central",
        "mesh_id": 42,
        "channel": 6,
        "mesh_ssid": "plant-mesh",
        "mesh_password": "mesh-secret",
    }
    config.update(overrides)
    return config


class WiFiMeshConfigTest(unittest.TestCase):
    def test_root_forms_mesh_without_router_credentials(self):
        validated = _MESH_OPTIONS_SCHEMA(_config())
        self.assertTrue(_join_without_router(validated))

    def test_child_may_use_a_router_uplink(self):
        validated = _MESH_OPTIONS_SCHEMA(
            _config(role="child", router={"ssid": "home", "password": "router-secret"})
        )
        self.assertFalse(_join_without_router(validated))

    def test_root_rejects_router_credentials(self):
        with self.assertRaises(cv.Invalid):
            _MESH_OPTIONS_SCHEMA(
                _config(router={"ssid": "home", "password": "router-secret"})
            )

    def test_child_rejects_root_remote_sensor_configuration(self):
        with self.assertRaises(cv.Invalid):
            _MESH_OPTIONS_SCHEMA(_config(role="child", remote_sensors=[{}]))


if __name__ == "__main__":
    unittest.main()
