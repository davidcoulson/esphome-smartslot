// SPDX-License-Identifier: GPL-2.0-or-later
// On-flash UPS history: esp_tsdb (zakery292/esp_tsdb, MIT) on a LittleFS `tsdb` partition, one averaged row per
// snapshot interval, served as JSON and a chart page on the ESPHome web server. The storage approach follows
// RAR/esphome-tigomonitor (MIT); the code here is our own.
#pragma once
#include "esphome/core/defines.h"
#ifdef USE_SMARTSLOT_HISTORY

#include <atomic>

#include "esphome/components/ota/ota_backend.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include "esphome/core/component.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

extern "C" {
#include "esp_tsdb.h"
}

namespace esphome {
namespace smartslot {

// Columns of /tsdb/ups.tsdb. int16 with a scale; INT16_MIN marks "no reading in this interval".
enum Col : uint8_t { COL_CHARGE, COL_RUNTIME_MIN, COL_VIN_DV, COL_VOUT_DV, COL_LOAD, COL_TEMP_DC, COL_ONBATT_PCT, NUM_COLS };

struct HistoryRow {
  uint32_t ts;
  int16_t v[NUM_COLS];
};

class SmartSlotHistory : public PollingComponent,
                         public web_server_idf::AsyncWebHandler,
                         public ota::OTAGlobalStateListener {
 public:
  void set_time(time::RealTimeClock *t) { this->time_ = t; }
  void set_web(web_server_base::WebServerBase *w) { this->web_ = w; }
  void set_config(uint32_t interval_s, uint32_t partition_bytes) {
    this->interval_s_ = interval_s;
    this->partition_bytes_ = partition_bytes;
  }

  void setup() override;
  void update() override;  // one poll: accumulate the driver's current values
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_CONNECTION; }

  bool canHandle(web_server_idf::AsyncWebServerRequest *request) const override;
  void handleRequest(web_server_idf::AsyncWebServerRequest *request) override;
  void on_ota_global_state(ota::OTAState state, float, uint8_t, ota::OTAComponent *) override {
    // An OTA writes flash; a LittleFS write at the same time can fault, so snapshots are dropped meanwhile.
    this->ota_active_ = state == ota::OTA_STARTED || state == ota::OTA_IN_PROGRESS;
  }

 protected:
  void snapshot_();
  static void writer_task_(void *arg);
  void send_json_(web_server_idf::AsyncWebServerRequest *request);

  time::RealTimeClock *time_{nullptr};
  web_server_base::WebServerBase *web_{nullptr};
  uint32_t interval_s_{300}, partition_bytes_{8u << 20};
  tsdb_t *db_{nullptr};
  SemaphoreHandle_t db_mutex_{nullptr};  // esp_tsdb/LittleFS calls: writer task vs HTTP task
  QueueHandle_t queue_{nullptr};
  std::atomic<bool> ota_active_{false};
  const char *error_{nullptr};
  // Accumulators for the current interval, reset by snapshot_().
  double sum_[NUM_COLS]{};
  uint32_t n_[NUM_COLS]{};
  uint32_t status_polls_{0}, onbatt_polls_{0};
};

}  // namespace smartslot
}  // namespace esphome
#endif
