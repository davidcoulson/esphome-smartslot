// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "lwip/sockets.h"  // sockaddr_in; newlib's netinet/in.h does not declare it under ESP-IDF

#include "esphome/components/binary_sensor/binary_sensor.h"
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
  void set_v2c(bool v) { this->agent_.v2c = v; }
  void add_user(const std::string &name, int auth, const std::string &auth_pw, int priv, const std::string &priv_pw) {
    this->users_.push_back({name, auth, auth_pw, priv, priv_pw});
  }
  void set_status(text_sensor::TextSensor *t) { this->status_ = t; }
  void set_link(binary_sensor::BinarySensor *b) { this->link_ = b; }
  void add_receiver(const std::string &host, uint16_t port, const std::string &community, const std::string &user);

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
  void setup_engine_();
  void traps_();
  void send_trap_(const snmp::Oid &trap, const char *text);

  struct UserConf {
    std::string name;
    int auth;
    std::string auth_pw;
    int priv;
    std::string priv_pw;
  };
  struct Receiver {
    sockaddr_in to;
    std::string community, user;  // user empty: v2c
  };

  snmp::Agent agent_;
  uint16_t port_{161};
  std::string name_;
  snmp::Entry *uptime_{nullptr};
  std::vector<Reading> readings_;
  std::vector<UserConf> users_;  // keys are derived in setup(), once the engine ID is known
  text_sensor::TextSensor *status_{nullptr};
  binary_sensor::BinarySensor *link_{nullptr};
  std::vector<Receiver> receivers_;
  int trap_fd_{-1};
  bool cold_sent_{false}, last_link_{false};
  std::string last_status_;
};

}  // namespace snmp_agent
}  // namespace esphome
