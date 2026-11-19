#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"

#include <array>
#include <string>
#include <vector>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <esp_err.h>

namespace esphome::wifi_mesh {

class WiFiMesh : public Component {
 public:
  static esp_err_t process_sensor_packet(uint8_t *data, uint32_t length, uint8_t **out_data, uint32_t *out_length,
                                         uint32_t sequence);
  void set_mesh_ssid(const std::string &ssid) { this->mesh_ssid_ = ssid; }
  void set_mesh_password(const std::string &password) { this->mesh_password_ = password; }
  void set_router_ssid(const std::string &ssid) { this->router_ssid_ = ssid; }
  void set_router_password(const std::string &password) { this->router_password_ = password; }
  void set_join_without_router(bool join) { this->join_without_router_ = join; }
  void set_maximum_level(uint8_t level) { this->maximum_level_ = level; }
  void set_role(bool root) { this->root_ = root; }
  void set_mesh_id(uint8_t mesh_id) { this->mesh_id_ = mesh_id; }
  void set_channel(uint8_t channel) { this->channel_ = channel; }
  void set_node_id(const std::string &node_id) { this->node_id_ = node_id; }
  void add_local_sensor(sensor::Sensor *local_sensor, const std::string &key) {
    this->local_sensors_.push_back({local_sensor, key});
  }
  void add_remote_sensor(sensor::Sensor *remote_sensor, const std::string &node_id, const std::string &key) {
    this->remote_sensors_.push_back({remote_sensor, node_id, key});
  }

  void setup() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

 protected:
  std::string mesh_ssid_;
  std::string mesh_password_;
  std::string router_ssid_;
  std::string router_password_;
  std::string node_id_;
  bool join_without_router_{true};
  bool root_{false};
  uint8_t maximum_level_{6};
  uint8_t mesh_id_{0};
  uint8_t channel_{1};

  struct LocalSensor {
    sensor::Sensor *sensor;
    std::string key;
  };
  struct RemoteSensor {
    sensor::Sensor *sensor;
    std::string node_id;
    std::string key;
  };
  std::vector<LocalSensor> local_sensors_;
  std::vector<RemoteSensor> remote_sensors_;
  QueueHandle_t received_sensor_queue_{nullptr};
};

}  // namespace esphome::wifi_mesh
