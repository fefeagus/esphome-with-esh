#include "wifi_mesh.h"

#include "esphome/core/log.h"
#include "esp_mesh_lite.h"
#include "esp_mesh_lite_port.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_netif_net_stack.h"
#include "lwip/lwip_napt.h"

#include <cstring>
#include <cmath>
#include <freertos/queue.h>

// IoT Bridge 1.0.1 references this hook, but its patch does not define it with IDF 5.5.
// Reset NAPT through lwIP's public API; a strong implementation from the patch overrides this fallback.
extern "C" __attribute__((weak)) void ip_napt_table_clear(void) {
  auto *softap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
  if (softap_netif == nullptr)
    return;

  auto *lwip_netif = static_cast<struct netif *>(esp_netif_get_netif_impl(softap_netif));
  if (lwip_netif == nullptr)
    return;

  ip_napt_enable_netif(lwip_netif, 0);
  ip_napt_enable_netif(lwip_netif, 1);
}

namespace esphome::wifi_mesh {

static const char *const TAG = "wifi_mesh";
static constexpr uint32_t SENSOR_MESSAGE_ID = 0x45535048;
static constexpr size_t NODE_ID_LENGTH = 24;
static constexpr size_t SENSOR_KEY_LENGTH = 24;

struct SensorPacket {
  uint8_t version;
  char node_id[NODE_ID_LENGTH];
  char key[SENSOR_KEY_LENGTH];
  float value;
};

static WiFiMesh *mesh_instance = nullptr;

esp_err_t WiFiMesh::process_sensor_packet(uint8_t *data, uint32_t length, uint8_t **out_data, uint32_t *out_length,
                                           uint32_t sequence) {
  (void) sequence;
  *out_data = nullptr;
  *out_length = 0;
  if (mesh_instance == nullptr || data == nullptr || length != sizeof(SensorPacket))
    return ESP_FAIL;
  SensorPacket packet{};
  std::memcpy(&packet, data, sizeof(packet));
  if (packet.version != 1 || packet.node_id[NODE_ID_LENGTH - 1] != '\0' ||
      packet.key[SENSOR_KEY_LENGTH - 1] != '\0' || !std::isfinite(packet.value))
    return ESP_FAIL;
  auto queue = mesh_instance->received_sensor_queue_;
  if (queue == nullptr || xQueueSend(queue, &packet, 0) != pdTRUE)
    return ESP_FAIL;
  return ESP_OK;
}

void WiFiMesh::setup() {
  if (this->mesh_ssid_.empty() || this->mesh_password_.empty() || this->node_id_.empty()) {
    ESP_LOGE(TAG, "Mesh credentials and node_id must be configured");
    this->mark_failed();
    return;
  }

  if (esp_netif_create_default_wifi_sta() == nullptr || esp_netif_create_default_wifi_ap() == nullptr) {
    ESP_LOGE(TAG, "Failed to create WiFi network interfaces");
    this->mark_failed();
    return;
  }

  wifi_init_config_t wifi_config = WIFI_INIT_CONFIG_DEFAULT();
  esp_err_t err = esp_wifi_init(&wifi_config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize WiFi: %s", esp_err_to_name(err));
    this->mark_failed();
    return;
  }
  err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
  if (err == ESP_OK)
    err = esp_wifi_set_mode(WIFI_MODE_APSTA);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to configure WiFi mode: %s", esp_err_to_name(err));
    this->mark_failed();
    return;
  }

  wifi_config_t station_config{};
  err = esp_wifi_set_config(WIFI_IF_STA, &station_config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize the mesh station interface: %s", esp_err_to_name(err));
    this->mark_failed();
    return;
  }

  wifi_config_t access_point_config{};
  std::memcpy(access_point_config.ap.ssid, this->mesh_ssid_.c_str(), this->mesh_ssid_.size());
  access_point_config.ap.ssid_len = this->mesh_ssid_.size();
  strlcpy(reinterpret_cast<char *>(access_point_config.ap.password), this->mesh_password_.c_str(),
          sizeof(access_point_config.ap.password));
  access_point_config.ap.channel = this->channel_;
  access_point_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
  access_point_config.ap.max_connection = 6;
  err = esp_wifi_set_config(WIFI_IF_AP, &access_point_config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize the mesh access point: %s", esp_err_to_name(err));
    this->mark_failed();
    return;
  }

  esp_mesh_lite_config_t mesh_config = ESP_MESH_LITE_DEFAULT_INIT();
  mesh_config.softap_ssid = this->mesh_ssid_.c_str();
  mesh_config.softap_password = this->mesh_password_.c_str();
  mesh_config.max_level = this->maximum_level_;
  mesh_config.join_mesh_without_configured_wifi = this->join_without_router_;
  mesh_config.mesh_id = this->mesh_id_;
  esp_mesh_lite_init(&mesh_config);
  if (this->root_)
    esp_mesh_lite_erase_rtc_store();
  esp_mesh_lite_set_mesh_id(this->mesh_id_, true);
  err = esp_mesh_lite_set_softap_info(this->mesh_ssid_.c_str(), this->mesh_password_.c_str());
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set the mesh access point credentials: %s", esp_err_to_name(err));
    this->mark_failed();
    return;
  }

  if (!this->router_ssid_.empty()) {
    mesh_lite_sta_config_t router_config{};
    std::memcpy(router_config.ssid, this->router_ssid_.c_str(), this->router_ssid_.size());
    strlcpy(reinterpret_cast<char *>(router_config.password), this->router_password_.c_str(),
            sizeof(router_config.password));
    err = esp_mesh_lite_set_router_config(&router_config);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Failed to configure the upstream WiFi router: %s", esp_err_to_name(err));
      this->mark_failed();
      return;
    }
  }

  if (this->root_)
    err = esp_mesh_lite_set_allowed_level(1);
  else
    err = esp_mesh_lite_set_disallowed_level(1);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to enforce the configured mesh role: %s", esp_err_to_name(err));
    this->mark_failed();
    return;
  }

  this->received_sensor_queue_ = xQueueCreate(8, sizeof(SensorPacket));
  if (this->received_sensor_queue_ == nullptr) {
    ESP_LOGE(TAG, "Failed to allocate sensor message queue");
    this->mark_failed();
    return;
  }
  mesh_instance = this;
  static const esp_mesh_lite_raw_msg_action_t sensor_action{
      .msg_id = SENSOR_MESSAGE_ID, .resp_msg_id = 0, .raw_process = WiFiMesh::process_sensor_packet};
  err = esp_mesh_lite_raw_msg_action_list_register(&sensor_action);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register sensor message handler: %s", esp_err_to_name(err));
    this->mark_failed();
    return;
  }

  err = esp_wifi_start();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start WiFi radio: %s", esp_err_to_name(err));
    this->mark_failed();
    return;
  }
  esp_mesh_lite_start();
  for (const auto &local : this->local_sensors_) {
    local.sensor->add_on_state_callback([this, key = local.key](float value) {
      if (this->root_ || !std::isfinite(value))
        return;
      SensorPacket packet{};
      packet.version = 1;
      strlcpy(packet.node_id, this->node_id_.c_str(), sizeof(packet.node_id));
      strlcpy(packet.key, key.c_str(), sizeof(packet.key));
      packet.value = value;
      esp_mesh_lite_msg_config_t message{};
      message.raw_msg.msg_id = SENSOR_MESSAGE_ID;
      message.raw_msg.max_retry = 3;
      message.raw_msg.retry_interval = 500;
      message.raw_msg.data = reinterpret_cast<const uint8_t *>(&packet);
      message.raw_msg.size = sizeof(packet);
      message.raw_msg.raw_resend = esp_mesh_lite_send_raw_msg_to_root;
      esp_err_t send_err = esp_mesh_lite_send_msg(ESP_MESH_LITE_RAW_MSG, &message);
      if (send_err != ESP_OK)
        ESP_LOGW(TAG, "Could not send sensor '%s' to mesh root: %s", key.c_str(), esp_err_to_name(send_err));
    });
  }
  ESP_LOGI(TAG, "ESP-Mesh-Lite started as %s on channel %u (mesh ID %u)", this->root_ ? "root" : "child",
           this->channel_, this->mesh_id_);
}

void WiFiMesh::loop() {
  if (!this->root_ || this->received_sensor_queue_ == nullptr)
    return;
  SensorPacket packet{};
  while (xQueueReceive(this->received_sensor_queue_, &packet, 0) == pdTRUE) {
    for (const auto &remote : this->remote_sensors_) {
      if (remote.node_id == packet.node_id && remote.key == packet.key) {
        remote.sensor->publish_state(packet.value);
        break;
      }
    }
  }
}

}  // namespace esphome::wifi_mesh
