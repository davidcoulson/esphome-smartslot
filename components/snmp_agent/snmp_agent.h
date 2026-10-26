// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/component.h"
#include "snmp_core.h"

namespace esphome {
namespace snmp_agent {

class SnmpAgent : public PollingComponent {
 public:
  void set_agent(uint16_t port, const std::string &community, const std::string &descr, const std::string &object_id,
                 const std::string &name, const std::string &contact, const std::string &location);
  void add_sensor(const std::string &oid, sensor::Sensor *s, int type, float scale) {
    this->add_reading_(oid, type, scale, s, nullptr);
  }
  void add_text_sensor(const std::string &oid, text_sensor::TextSensor *t, int type, float scale) {
    this->add_reading_(oid, type, scale, nullptr, t);
  }
  void add_map(const std::string &token, int value) {
    if (!this->readings_.empty()) this->readings_.back().map.emplace_back(token, value);
  }
  void add_static(const std::string &oid, const std::string &value, int type);

  void setup() override;
  void update() override;  // copies current readings into the SNMP table on the ESPHome loop task
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_CONNECTION; }

 protected:
  struct Reading {
    snmp::Entry *entry;
    sensor::Sensor *s;
    text_sensor::TextSensor *t;
    float scale;
    std::vector<std::pair<std::string, int>> map;
  };
  void add_reading_(const std::string &oid, int type, float scale, sensor::Sensor *s, text_sensor::TextSensor *t);
  static void task_(void *arg);
  void serve_();

  snmp::Agent agent_;
  uint16_t port_{161};
  std::string name_;
  snmp::Entry *uptime_{nullptr};
  std::vector<Reading> readings_;
};

}  // namespace snmp_agent
}  // namespace esphome
