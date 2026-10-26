// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <map>
#include <string>

#include "esphome/components/button/button.h"
#include "esphome/components/switch/switch.h"
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
  // Optional arming switch: the command is sent only after the switch has gone on, and one press consumes the
  // arming for every button sharing the switch, whether or not the switch acknowledges turn_off() at once.
  void set_arm(switch_::Switch *s) {
    this->arm_ = s;
    s->add_on_state_callback([s](bool on) { armed()[s] = on; });
  }

 protected:
  void press_action() override {
    if (this->arm_ != nullptr) {
      bool &latch = armed()[this->arm_];
      if (!latch) {
        ESP_LOGW("smartslot", "%s refused: turn on '%s' first", this->command_.c_str(), this->arm_->get_name().c_str());
        return;
      }
      latch = false;  // before turn_off(): a re-entrant press from a turn-off automation must see it consumed
      this->arm_->turn_off();
    }
    int r = this->parent_->instcmd(this->command_);
    if (r != STAT_INSTCMD_HANDLED) ESP_LOGW("smartslot", "%s failed (%d)", this->command_.c_str(), r);
  }
  static std::map<switch_::Switch *, bool> &armed() {
    static std::map<switch_::Switch *, bool> m;
    return m;
  }
  std::string command_;
  switch_::Switch *arm_{nullptr};
};

}  // namespace smartslot
}  // namespace esphome
