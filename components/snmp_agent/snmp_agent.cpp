// SPDX-License-Identifier: GPL-2.0-or-later
#include "snmp_agent.h"

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#include "esphome/components/network/util.h"
#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/core/preferences.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace esphome {
namespace snmp_agent {

static const char *const TAG = "snmp_agent";

void SnmpAgent::set_agent(uint16_t port, const std::string &community, const std::string &descr,
                          const std::string &object_id, const std::string &name, const std::string &contact,
                          const std::string &location) {
  this->port_ = port;
  this->agent_.community = community;
  this->name_ = name;
  snmp::Oid oid;
  // System group (RFC 1213 / SNMPv2-MIB), fixed at boot except sysUpTime.
  this->agent_.set(this->agent_.add({1, 3, 6, 1, 2, 1, 1, 1, 0}, snmp::STRING), descr);
  if (snmp::parse_oid(object_id, oid)) this->agent_.set(this->agent_.add({1, 3, 6, 1, 2, 1, 1, 2, 0}, snmp::OBJECT_ID), oid);
  this->uptime_ = this->agent_.add({1, 3, 6, 1, 2, 1, 1, 3, 0}, snmp::TIMETICKS);
  this->agent_.set(this->agent_.add({1, 3, 6, 1, 2, 1, 1, 4, 0}, snmp::STRING), contact);
  this->agent_.set(this->agent_.add({1, 3, 6, 1, 2, 1, 1, 6, 0}, snmp::STRING), location);
}

void SnmpAgent::add_reading_(const std::string &oid, int type, float scale, sensor::Sensor *s,
                             text_sensor::TextSensor *t) {
  snmp::Oid o;
  if (!snmp::parse_oid(oid, o)) return;
  this->readings_.push_back({this->agent_.add(o, (snmp::Type) type), s, t, scale, {}});
}

void SnmpAgent::add_static(const std::string &oid, const std::string &value, int type) {
  snmp::Oid o, v;
  if (!snmp::parse_oid(oid, o)) return;
  snmp::Entry *e = this->agent_.add(o, (snmp::Type) type);
  if (type == snmp::STRING) {
    this->agent_.set(e, value);
  } else if (type == snmp::OBJECT_ID) {
    if (snmp::parse_oid(value, v)) this->agent_.set(e, v);
  } else {
    this->agent_.set(e, (int64_t) strtoll(value.c_str(), nullptr, 10));
  }
}

void SnmpAgent::add_receiver(const std::string &host, uint16_t port, const std::string &community,
                             const std::string &user) {
  Receiver r{};
  r.to.sin_family = AF_INET;
  r.to.sin_port = htons(port);
  if (inet_aton(host.c_str(), &r.to.sin_addr) == 0) return;  // validated as IPv4 in the schema
  r.community = community;
  r.user = user;
  this->receivers_.push_back(r);
}

void SnmpAgent::setup_engine_() {
  snmp::EngineState st{};
  ESPPreferenceObject pref =
      global_preferences->make_preference<snmp::EngineState>(fnv1_hash("snmp_engine_state"), true);
  bool loaded = pref.load(&st);
  if (!loaded || st.salt == 0) ESP_LOGW(TAG, "no SNMP engine state in flash: new engine ID");
  snmp::next_engine_state(st, loaded, random_uint32());
  // If the new count is not in flash, a later restart could reuse it and accept replayed requests: serve only
  // users without authentication.
  bool durable = pref.save(&st) && global_preferences->sync();
  if (!durable) ESP_LOGE(TAG, "engine boots not saved to flash: authenticated SNMPv3 users are disabled");
  uint8_t mac[6];
  get_mac_address_raw(mac);
  this->agent_.set_engine(snmp::engine_id_from(mac, st.salt), st.boots, ((uint64_t) random_uint32() << 32) | random_uint32());
  for (auto &u : this->users_) {
    if (!durable && u.auth != snmp::AUTH_NONE) continue;
    if (!this->agent_.add_user(u.name, (snmp::Auth) u.auth, u.auth_pw, (snmp::Priv) u.priv, u.priv_pw))
      ESP_LOGE(TAG, "SNMPv3 user %s: key setup failed", u.name.c_str());
    u.auth_pw.clear();
    u.priv_pw.clear();
  }
}

void SnmpAgent::setup() {
  this->setup_engine_();
  if (!this->receivers_.empty()) {
    this->trap_fd_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (this->trap_fd_ < 0) ESP_LOGE(TAG, "trap socket failed (errno %d)", errno);
  }
  // sysName is filled here because App.get_name() is not known to the generated set_agent() call.
  this->agent_.set(this->agent_.add({1, 3, 6, 1, 2, 1, 1, 5, 0}, snmp::STRING), this->name_.empty() ? App.get_name() : this->name_);
  this->update();
  xTaskCreate(&SnmpAgent::task_, "snmp", 6144, this, 4, nullptr);
}

void SnmpAgent::update() {
  this->agent_.set(this->uptime_, (int64_t) (millis() / 10));  // shortcut: wraps with millis() at ~49 days, as sysUpTime itself does at 497
  this->traps_();
  for (auto &r : this->readings_) {
    snmp::Entry *e = r.entry;
    if (r.s != nullptr) {
      float v = r.s->state;
      if (!r.s->has_state() || std::isnan(v)) {
        this->agent_.clear(e);
      } else if (e->type == snmp::STRING) {
        char buf[24];
        snprintf(buf, sizeof buf, "%g", v * r.scale);
        this->agent_.set(e, std::string(buf));
      } else {
        this->agent_.set(e, (int64_t) llroundf(v * r.scale));
      }
    } else if (r.t != nullptr) {
      if (!r.t->has_state()) {
        this->agent_.clear(e);
      } else if (e->type == snmp::STRING) {
        this->agent_.set(e, r.t->state);
      } else if (!r.map.empty()) {
        bool hit = false;
        for (auto &m : r.map) {
          // Match whole space-separated tokens of the state ("OL CHRG" has tokens OL and CHRG).
          std::string s = " " + r.t->state + " ";
          if (m.first == "*" || s.find(" " + m.first + " ") != std::string::npos) {
            this->agent_.set(e, (int64_t) m.second);
            hit = true;
            break;
          }
        }
        if (!hit) this->agent_.clear(e);
      } else {
        char *end;
        double v = strtod(r.t->state.c_str(), &end);
        if (end == r.t->state.c_str()) {
          this->agent_.clear(e);
        } else {
          this->agent_.set(e, (int64_t) llround(v * r.scale));
        }
      }
    }
  }
}

static snmp::Oid apc_trap(uint32_t id) { return {1, 3, 6, 1, 4, 1, 318, 0, id}; }

void SnmpAgent::traps_() {
  if (this->trap_fd_ < 0 || !network::is_connected()) return;  // changes are picked up once the network is back
  if (!this->cold_sent_) {
    this->send_trap_({1, 3, 6, 1, 6, 3, 1, 1, 5, 1}, nullptr);  // coldStart
    this->cold_sent_ = true;
  }
  bool link = this->link_ == nullptr || (this->link_->has_state() && this->link_->state);
  if (this->link_ != nullptr && link != this->last_link_) {
    this->last_link_ = link;
    this->send_trap_(apc_trap(link ? 8 : 1), link ? "UPS: Communication established." : "UPS: Communication lost.");
  }
  // While the UPS link is down the status is stale; changes made meanwhile are reported once it is back.
  if (this->status_ == nullptr || !link || !this->status_->has_state() || this->status_->state == this->last_status_)
    return;
  for (auto &ev : snmp::status_traps(this->last_status_, this->status_->state)) this->send_trap_(apc_trap(ev.id), ev.text);
  this->last_status_ = this->status_->state;
}

void SnmpAgent::send_trap_(const snmp::Oid &trap, const char *text) {
  std::vector<std::pair<snmp::Oid, snmp::Entry>> vars;
  if (text != nullptr) {
    snmp::Entry e;
    e.type = snmp::STRING;
    e.valid = true;
    e.str = text;
    vars.push_back({{1, 3, 6, 1, 4, 1, 318, 2, 3, 3, 0}, e});  // mtrapargsString.0
  }
  uint32_t up = millis() / 10;
  for (auto &r : this->receivers_) {
    snmp::Bytes b;
    const snmp::User *u = r.user.empty() ? nullptr : this->agent_.user(r.user);
    if (r.user.empty()) {
      b = this->agent_.trap_v2c(r.community, up, trap, vars);
    } else if (u != nullptr) {
      b = this->agent_.trap_v3(*u, up, trap, vars);
    }
    if (b.empty() || sendto(this->trap_fd_, b.data(), b.size(), 0, (const sockaddr *) &r.to, sizeof r.to) < 0)
      ESP_LOGW(TAG, "trap to %s:%u failed", inet_ntoa(r.to.sin_addr), ntohs(r.to.sin_port));
  }
  ESP_LOGI(TAG, "trap .%u.%u: %s", (unsigned) trap[trap.size() - 2], (unsigned) trap.back(), text ? text : "coldStart");
}

void SnmpAgent::task_(void *arg) {
  static_cast<SnmpAgent *>(arg)->serve_();
  vTaskDelete(nullptr);
}

void SnmpAgent::serve_() {
  int fd = -1;
  while (true) {
    if (fd < 0) {
      fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
      sockaddr_in sa = {};
      sa.sin_family = AF_INET;
      sa.sin_port = htons(this->port_);
      sa.sin_addr.s_addr = htonl(INADDR_ANY);
      if (fd < 0 || bind(fd, (sockaddr *) &sa, sizeof sa) < 0) {
        ESP_LOGW(TAG, "bind UDP %u failed (errno %d), retrying", this->port_, errno);
        if (fd >= 0) close(fd);
        fd = -1;
        vTaskDelay(pdMS_TO_TICKS(2000));
        continue;
      }
      ESP_LOGI(TAG, "listening on UDP %u", this->port_);
    }
    uint8_t buf[1500];
    sockaddr_in from;
    socklen_t fl = sizeof from;
    int n = recvfrom(fd, buf, sizeof buf, 0, (sockaddr *) &from, &fl);
    if (n < 0) {
      close(fd);
      fd = -1;
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }
    snmp::Bytes out;
    if (this->agent_.handle(buf, n, out)) sendto(fd, out.data(), out.size(), 0, (sockaddr *) &from, fl);
  }
}

void SnmpAgent::dump_config() {
  ESP_LOGCONFIG(TAG, "SNMP agent (read-only): UDP port %u, %u readings", this->port_,
                (unsigned) this->readings_.size());
  ESP_LOGCONFIG(TAG, "  v1/v2c: %s, v3 users: %u, trap receivers: %u", YESNO(this->agent_.v2c),
                (unsigned) this->users_.size(), (unsigned) this->receivers_.size());
  // Trap receivers need this engine ID for v3 users (snmptrapd: createUser -e 0x<id> ...).
  ESP_LOGCONFIG(TAG, "  engine ID: %s, boots: %u", format_hex(this->agent_.engine_id()).c_str(),
                (unsigned) this->agent_.engine_boots());
}

}  // namespace snmp_agent
}  // namespace esphome
