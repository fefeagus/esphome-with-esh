"""Configuration and code generation tests for ESP-Mesh-Lite."""

import asyncio
from unittest.mock import AsyncMock, MagicMock, call, patch

import esphome.config_validation as cv
import pytest
from esphome.components import wifi_mesh
from esphome.const import CONF_ID, CONF_PASSWORD, CONF_SSID


def _config(**overrides):
    config = {
        "role": "root",
        "node_id": "central",
        "mesh_id": 42,
        "channel": 6,
        "mesh_ssid": "plant-mesh",
        "mesh_password": "mesh-secret",
        "maximum_level": 6,
        "local_sensors": [],
        "remote_sensors": [],
        CONF_ID: MagicMock(name="wifi_mesh_id"),
    }
    config.update(overrides)
    return config


@pytest.mark.parametrize(
    ("overrides", "expected"),
    [
        ({}, True),
        ({"role": "child"}, True),
        (
            {
                "role": "child",
                "router": {CONF_SSID: "home", CONF_PASSWORD: "router-secret"},
            },
            False,
        ),
        (
            {
                "role": "child",
                "router": {CONF_SSID: "home", CONF_PASSWORD: "router-secret"},
                "join_without_router": True,
            },
            True,
        ),
    ],
)
def test_join_policy_for_roles(overrides, expected):
    assert wifi_mesh._join_without_router(_config(**overrides)) is expected


@pytest.mark.parametrize(
    "overrides",
    [
        {"role": "invalid"},
        {"mesh_id": 256},
        {"channel": 0},
        {"node_id": "a" * 24},
        {"node_id": "nódo"},
        {"mesh_password": "short"},
        {"maximum_level": 16},
        {"router": {CONF_SSID: "home", CONF_PASSWORD: "router-secret"}},
        {"join_without_router": False},
        {"role": "child", "remote_sensors": [{"name": "remote"}]},
    ],
)
def test_rejects_invalid_mesh_configuration(overrides):
    with pytest.raises(cv.Invalid):
        wifi_mesh._MESH_OPTIONS_SCHEMA(_config(**overrides))


@pytest.mark.parametrize("role", ["root", "child"])
def test_accepts_root_and_child_roles(role):
    validated = wifi_mesh._MESH_OPTIONS_SCHEMA(_config(role=role))
    assert validated["role"] == role
    assert validated["mesh_id"] == 42
    assert validated["channel"] == 6


def test_local_sensor_key_must_be_stable_ascii():
    with pytest.raises(cv.Invalid):
        wifi_mesh._mesh_identifier("temperatura-é")


def test_to_code_registers_dependencies_and_role_settings():
    config = _config()
    mesh_var = MagicMock(name="mesh_var")

    async def run():
        with (
            patch.object(wifi_mesh.esp32, "add_idf_sdkconfig_option") as sdkconfig,
            patch.object(wifi_mesh.esp32, "add_idf_component") as add_component,
            patch.object(wifi_mesh.cg, "new_Pvariable", return_value=mesh_var) as new_var,
            patch.object(wifi_mesh.cg, "add") as add,
            patch.object(wifi_mesh.cg, "register_component", new_callable=AsyncMock),
        ):
            await wifi_mesh.to_code(config)

        assert sdkconfig.call_args_list == [
            call("CONFIG_MESH_LITE_ENABLE", True),
            call("CONFIG_LWIP_DHCPS", True),
            call("CONFIG_LWIP_DHCPS_MAX_STATION_NUM", 8),
            call("CONFIG_BRIDGE_ETHERNET_NETIF_ENABLE", False),
        ]
        assert add_component.call_args_list == [
            call(name="espressif/iot_bridge", ref="1.0.1"),
            call(name="espressif/mesh_lite", ref="1.0.2"),
        ]
        new_var.assert_called_once_with(config[CONF_ID])
        assert add.call_count == 8
        mesh_var.set_role.assert_called_once_with(True)
        mesh_var.set_mesh_id.assert_called_once_with(42)
        mesh_var.set_channel.assert_called_once_with(6)
        mesh_var.set_node_id.assert_called_once_with("central")
        mesh_var.set_join_without_router.assert_called_once_with(True)

    asyncio.run(run())
