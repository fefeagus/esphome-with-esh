# `wifi_mesh` (ESP-Mesh-Lite)

Este componente integra ESP-Mesh-Lite como componente externo de ESP-IDF. Toma
el control de la radio Wi-Fi; no declares `wifi:` en el mismo dispositivo.
Funciona en ESP32 original con ESP-IDF.

Configura el mismo `mesh_id`, `channel`, `mesh_ssid` y `mesh_password` en todos
los nodos. El nodo raíz no lleva credenciales del router y queda limitado al
nivel 1. Los hijos bloquean el nivel 1 para no ser elegidos raíz.

```yaml
wifi_mesh:
  role: root
  node_id: central
  mesh_id: 42
  channel: 6
  mesh_ssid: "plant-mesh"
  mesh_password: !secret mesh_password
  remote_sensors:
    - node_id: greenhouse
      key: temperature
      name: "Greenhouse temperature"
      unit_of_measurement: "°C"
      accuracy_decimals: 1
```

En el hijo se declara cada sensor local con un `key` que debe coincidir con la
clave de su entrada en `remote_sensors` del root:

```yaml
wifi_mesh:
  role: child
  node_id: greenhouse
  mesh_id: 42
  channel: 6
  mesh_ssid: "plant-mesh"
  mesh_password: !secret mesh_password
  local_sensors:
    - sensor: air_temperature
      key: temperature

sensor:
  - platform: dht
    id: air_temperature
    pin: GPIO4
    temperature:
      name: "Local temperature"
    humidity:
      name: "Local humidity"
```

`node_id` y `key` admiten hasta 23 caracteres ASCII visibles. Declara los
sensores locales normalmente en ESPHome; `local_sensors` selecciona cuáles
publicar por la malla. En la raíz, `remote_sensors` crea entidades ESPHome y
actualiza sus estados al recibir lecturas. Los paquetes se encolan en la
callback de Mesh-Lite y se aplican desde el loop de ESPHome.

Los nodos que necesiten un uplink de router pueden configurarlo en un hijo con
`router: { ssid: ..., password: ... }`; por omisión, ese hijo espera al router.
Un hijo sin `router:` se une sin credenciales de router. El root siempre forma
la malla sin credenciales de router. ESP-IDF Component Manager descarga
ESP-Mesh-Lite e IoT Bridge durante la compilación.
