#pragma once

#include "esphome/core/component.h"
#include "esphome/components/wifi/wifi_component.h"

namespace esphome::wifi_mesh {

class WiFiMesh : public Component {
 public:
  void set_wifi(wifi::WiFiComponent *wifi) { this->wifi_ = wifi; }
  void setup() override;
  void loop() override;

 protected:
  wifi::WiFiComponent *wifi_{nullptr};
  bool last_connected_{false};
};

}  // namespace esphome::wifi_mesh
