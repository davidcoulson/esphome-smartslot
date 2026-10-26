// SPDX-License-Identifier: GPL-2.0-or-later
#include "snmp_agent.h"

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
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

void SnmpAgent::setup() {
  // sysName is filled here because App.get_name() is not known to the generated set_agent() call.
  this->agent_.set(this->agent_.add({1, 3, 6, 1, 2, 1, 1, 5, 0}, snmp::STRING), this->name_.empty() ? App.get_name() : this->name_);
  this->update();
  xTaskCreate(&SnmpAgent::task_, "snmp", 6144, this, 4, nullptr);
}

void SnmpAgent::update() {
  this->agent_.set(this->uptime_, (int64_t) (millis() / 10));  // shortcut: wraps with millis() at ~49 days, as sysUpTime itself does at 497
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
  ESP_LOGCONFIG(TAG, "SNMP agent (v1/v2c, read-only): UDP port %u, %u readings", this->port_,
                (unsigned) this->readings_.size());
}

}  // namespace snmp_agent
}  // namespace esphome
