// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <string>

#include "esphome/components/button/button.h"
#include "esphome/core/log.h"
#include "smartslot.h"

extern "C" {
#include "nutshim.h"
}

namespace esphome {
namespace smartslot {

class SmartSlotButton : public button::Button, public Parented<SmartSlot> {
 public:
  void set_command(const std::string &c) { this->command_ = c; }

 protected:
  void press_action() override {
    int r = this->parent_->instcmd(this->command_);
    if (r != STAT_INSTCMD_HANDLED) ESP_LOGW("smartslot", "%s failed (%d)", this->command_.c_str(), r);
  }
  std::string command_;
};

}  // namespace smartslot
}  // namespace esphome
