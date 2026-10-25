// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <string>

#include <utility>
#include <vector>

#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/component.h"

#define SMARTSLOT_VERSION "0.1.0-dev"

namespace esphome {
namespace smartslot {

class SmartSlot : public PollingComponent {
 public:
  void set_pins(int tx, int rx, int sel, int sel_level, int uart_num) {
    this->tx_ = tx;
    this->rx_ = rx;
    this->sel_ = sel;
    this->sel_level_ = sel_level;
    this->uart_num_ = uart_num;
  }
  void set_driver(const std::string &cmdsrc, uint32_t baud) {
    this->cmdsrc_ = cmdsrc;
    this->baud_ = baud;
  }
  void set_nut(uint16_t port, const std::string &user, const std::string &pass) {
    this->nut_port_ = port;
    this->nut_user_ = user;
    this->nut_pass_ = pass;
  }

  void register_sensor(sensor::Sensor *s, const std::string &var) { this->sensors_.emplace_back(s, var); }
  void register_text_sensor(text_sensor::TextSensor *t, const std::string &var) { this->text_sensors_.emplace_back(t, var); }
  int instcmd(const std::string &cmd);

  void setup() override;
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_CONNECTION; }

 protected:
  int tx_{47}, rx_{48}, sel_{53}, sel_level_{0}, uart_num_{1};
  std::string cmdsrc_{"smartslot1"};
  uint32_t baud_{9600};
  uint16_t nut_port_{3493};
  std::string nut_user_, nut_pass_;
  std::vector<std::pair<sensor::Sensor *, std::string>> sensors_;
  std::vector<std::pair<text_sensor::TextSensor *, std::string>> text_sensors_;
};

}  // namespace smartslot
}  // namespace esphome
