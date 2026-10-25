// SPDX-License-Identifier: GPL-2.0-or-later
#include "smartslot.h"

#include <cstdlib>
#include <cstring>

#include "esphome/core/log.h"

extern "C" {
#include "nutdrv.h"
#include "nutshim.h"
#include "upsd.h"
}

namespace esphome {
namespace smartslot {

static const char *const TAG = "smartslot";

// upsd takes plain C callbacks, so the credentials live in statics (one SmartSlot per node).
static std::string g_user, g_pass;
static nutdrv_config g_cfg;
static nutproto_hooks g_hooks;

static bool check_user(const char *user, const char *pass) {
  return !g_user.empty() && g_user == user && g_pass == pass;
}
// Queue-and-forget (timeout 0): upsd serves every client from one task and the button runs on the ESPHome loop,
// so neither may wait on the driver. OK means forwarded, as in NUT's own upsd without TRACKING.
static int hook_instcmd(const char *cmd, const char *param) { return nutdrv_instcmd(cmd, param, 0); }
static int hook_setvar(const char *var, const char *val) { return nutdrv_setvar(var, val, 0); }

void SmartSlot::setup() {
  g_user = this->nut_user_;
  g_pass = this->nut_pass_;
  nutshim_init();
  nutshim_serial_config(this->uart_num_, this->tx_, this->rx_);
  g_cfg = {this->cmdsrc_.c_str(), this->baud_, this->sel_, this->sel_level_};
  if (nutdrv_start(&g_cfg) != 0) {
    ESP_LOGE(TAG, "could not start the driver task");
    this->mark_failed();
    return;
  }
  g_hooks.check_user = check_user;
  g_hooks.instcmd = hook_instcmd;
  g_hooks.setvar = hook_setvar;
  g_hooks.version = "esphome-smartslot upsd " SMARTSLOT_VERSION;
  g_hooks.numlogins = upsd_num_logins;
  if (upsd_start(&g_hooks, this->nut_port_) != 0) ESP_LOGE(TAG, "could not start upsd on port %u", this->nut_port_);
}

int SmartSlot::instcmd(const std::string &cmd) { return nutdrv_instcmd(cmd.c_str(), nullptr, 0); }

void SmartSlot::update() {
  char buf[256];
  for (auto &p : this->sensors_) {
    if (!nutshim_value_for_publish(p.second.c_str(), buf, sizeof buf)) continue;
    char *end;
    float v = strtof(buf, &end);
    if (end != buf) p.first->publish_state(v);
  }
  for (auto &p : this->text_sensors_)
    if (nutshim_value_for_publish(p.second.c_str(), buf, sizeof buf)) p.first->publish_state(buf);
  if (nutdrv_state() != NUTDRV_RUNNING || !dstate_is_dataok())
    this->status_set_warning();
  else
    this->status_clear_warning();
}

void SmartSlot::dump_config() {
  ESP_LOGCONFIG(TAG, "SmartSlot " SMARTSLOT_VERSION);
  ESP_LOGCONFIG(TAG, "  TX pin: GPIO%d, RX pin: GPIO%d, UART_SEL: GPIO%d (level %d), UART%d", this->tx_, this->rx_,
                this->sel_, this->sel_level_, this->uart_num_);
  ESP_LOGCONFIG(TAG, "  cmdsrc: %s, baud: %u", this->cmdsrc_.c_str(), (unsigned) this->baud_);
  ESP_LOGCONFIG(TAG, "  NUT port: %u, login user set: %s", this->nut_port_, YESNO(!this->nut_user_.empty()));
  ESP_LOGCONFIG(TAG, "  driver state: %d", (int) nutdrv_state());
}

}  // namespace smartslot
}  // namespace esphome
