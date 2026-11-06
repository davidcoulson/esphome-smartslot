// SPDX-License-Identifier: GPL-2.0-or-later
#include "smartslot.h"

#include <cmath>
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

bool SmartSlot::online() const { return nutdrv_state() == NUTDRV_RUNNING && dstate_is_servable(); }

// Once a second, independent of the publish interval: remembers the last good poll and, LINK_GRACE_S after the
// link drops, publishes unknown to every entity, so no reader (HA, web page, MQTT, SNMP) is left looking at a frozen
// "OL 100 %" after the UPS has gone. A shorter blip keeps the last values.
void SmartSlot::check_link_() {
  bool on = this->online();
  if (on) {
    this->last_contact_ms_ = millis_64();
    this->cleared_ = false;
  } else if (!this->cleared_ && this->data_age() >= LINK_GRACE_S) {
    for (auto &p : this->sensors_)
      if (!std::isnan(p.first->state)) p.first->publish_state(NAN);
    for (auto &p : this->text_sensors_)
      if (!p.first->state.empty()) p.first->publish_state("");
    this->cleared_ = true;
  }
  // Logged so that syslog carries UPS events.
  if (on != this->online_) ESP_LOGI(TAG, "UPS communication %s", on ? "established" : "lost");
  this->online_ = on;
  if (on)
    this->status_clear_warning();
  else
    this->status_set_warning();
}

void SmartSlot::loop() {
  uint32_t now = millis();
  if (now - this->last_check_ms_ < 1000) return;
  this->last_check_ms_ = now;
  this->check_link_();
}

void SmartSlot::update() {
  char buf[256];
  this->check_link_();
  // A variable the UPS stopped reporting (ups.alarm cleared, charge with the battery disconnected) becomes unknown
  // at once; while nothing is being served the values stay until check_link_() clears them.
  for (auto &p : this->sensors_) {
    int r = nutshim_lookup(p.second.c_str(), buf, sizeof buf);
    float v;
    if (r == 1 && nutshim_parse_number(buf, &v))
      p.first->publish_state(v);
    else if (r != -1 && !std::isnan(p.first->state))
      p.first->publish_state(NAN);
  }
  for (auto &p : this->text_sensors_) {
    int r = nutshim_lookup(p.second.c_str(), buf, sizeof buf);
    if (r == 1)
      p.first->publish_state(buf);
    else if (r == 0 && !p.first->state.empty())
      p.first->publish_state("");
  }
  if (this->online_ && nutshim_value_for_publish("ups.status", buf, sizeof buf) && this->last_status_ != buf) {
    ESP_LOGI(TAG, "UPS status: %s -> %s", this->last_status_.empty() ? "-" : this->last_status_.c_str(), buf);
    this->last_status_ = buf;
  }
  if (!this->online_) this->stale_polls_++;
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
