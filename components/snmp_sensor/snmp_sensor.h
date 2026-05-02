#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"

#include <string>
#include <vector>
#include <cstdint>

namespace esphome {
namespace snmp_sensor {

class SNMPSensor : public sensor::Sensor, public PollingComponent {
 public:
  void set_host(const std::string &h) { host_ = h; }
  void set_port(uint16_t p) { port_ = p; }
  void set_community(const std::string &c) { community_ = c; }
  void set_oid(const std::string &o) { oid_ = o; }

  void setup() override {}
  void update() override;
  void loop() override;
  void dump_config() override;

 protected:
  std::string host_;
  uint16_t port_{161};
  std::string community_;
  std::string oid_;
  uint16_t req_id_{1};
  int sock_{-1};
  uint32_t send_time_ms_{0};
  bool waiting_{false};

  std::vector<uint8_t> build_packet_(uint16_t req_id);
  bool parse_(const uint8_t *data, size_t len, int32_t *out_value);
};

}  // namespace snmp_sensor
}  // namespace esphome
