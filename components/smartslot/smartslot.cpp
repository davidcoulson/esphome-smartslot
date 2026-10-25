// SPDX-License-Identifier: GPL-2.0-or-later
#include "smartslot.h"

#include "esphome/core/log.h"

extern "C" {
#include "os.h"
}

namespace esphome {
namespace smartslot {

static const char *const TAG = "smartslot";

void SmartSlot::setup() {
  ESP_LOGI(TAG, "SmartSlot " SMARTSLOT_VERSION " starting (uptime %llu ms)", (unsigned long long) os_now_ms());
}

void SmartSlot::update() {}

void SmartSlot::dump_config() {
  ESP_LOGCONFIG(TAG, "SmartSlot " SMARTSLOT_VERSION);
  ESP_LOGCONFIG(TAG, "  TX pin: GPIO%d, RX pin: GPIO%d, UART_SEL: GPIO%d (level %d)", this->tx_, this->rx_, this->sel_,
                this->sel_level_);
}

}  // namespace smartslot
}  // namespace esphome
