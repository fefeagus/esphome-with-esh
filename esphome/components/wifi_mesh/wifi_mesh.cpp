#include "wifi_mesh.h"
#include "esphome/core/log.h"

namespace esphome::wifi_mesh {

static const char *const TAG = "wifi_mesh";

void WiFiMesh::setup() {
  if (this->wifi_ == nullptr) {
    ESP_LOGE(TAG, "WiFi component is unavailable");
    this->mark_failed();
    return;
  }
  this->last_connected_ = this->wifi_->is_connected();
  ESP_LOGI(TAG, "Started; WiFi is %s",
           this->last_connected_ ? LOG_STR_LITERAL("connected") : LOG_STR_LITERAL("disconnected"));
}

void WiFiMesh::loop() {
  if (this->wifi_ == nullptr)
    return;
  const bool connected = this->wifi_->is_connected();
  if (connected == this->last_connected_)
    return;
  this->last_connected_ = connected;
  ESP_LOGI(TAG, "WiFi %s", connected ? LOG_STR_LITERAL("connected") : LOG_STR_LITERAL("disconnected"));
}

}  // namespace esphome::wifi_mesh
