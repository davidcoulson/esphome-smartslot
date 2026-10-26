// SPDX-License-Identifier: GPL-2.0-or-later
#include "esphome/core/defines.h"
#ifdef USE_SMARTSLOT_HISTORY
#include "history.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "esp_littlefs.h"
#include "esp_timer.h"
#include "esphome/core/log.h"
#include "history_page.h"

extern "C" {
#include "nutdrv.h"
}

namespace esphome {
namespace smartslot {

static const char *const TAG = "smartslot.history";
static const char *kNames[NUM_COLS] = {"charge", "runtime_min", "vin_dv", "vout_dv",
                                             "load",   "temp_dc",     "onbatt_pct"};
static const char *const kVars[NUM_COLS] = {"battery.charge", "battery.runtime", "input.voltage", "output.voltage",
                                            "ups.load",       "ups.temperature", "ups.status"};
static const float kScale[NUM_COLS] = {1, 1.0f / 60, 10, 10, 1, 10, 1};  // value -> stored int16
static const char *const kDbPath = "/tsdb/ups.tsdb";

class DbLock {
 public:
  explicit DbLock(SemaphoreHandle_t m) : m_(m) { xSemaphoreTake(m_, portMAX_DELAY); }
  ~DbLock() { xSemaphoreGive(m_); }

 private:
  SemaphoreHandle_t m_;
};

void SmartSlotHistory::setup() {
  this->db_mutex_ = xSemaphoreCreateMutex();
  this->queue_ = xQueueCreate(4, sizeof(HistoryRow));
  esp_vfs_littlefs_conf_t conf = {};
  conf.base_path = "/tsdb";
  conf.partition_label = "tsdb";
  conf.format_if_mount_failed = true;
  esp_err_t err = esp_vfs_littlefs_register(&conf);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    this->error_ = err == ESP_ERR_NOT_FOUND
                       ? "no `tsdb` partition in flash: flash the new partition table over serial once"
                       : "LittleFS could not mount the `tsdb` partition";
    ESP_LOGE(TAG, "%s (%s)", this->error_, esp_err_to_name(err));
    this->mark_failed();
    return;
  }
  tsdb_config_t cfg = {};
  cfg.filepath = kDbPath;
  cfg.num_params = NUM_COLS;
  cfg.param_names = kNames;
  // LittleFS is copy-on-write and stops committing when nearly full, so the ring file gets half the partition.
  cfg.max_records = TSDB_CALC_MAX_RECORDS(this->partition_bytes_ / 2, NUM_COLS);
  cfg.index_stride = 380;
  cfg.buffer_pool_size = 10 * 1024;
  cfg.alloc_strategy = TSDB_ALLOC_AUTO;
  this->db_ = tsdb_open(&cfg);
  if (this->db_ == nullptr) {
    this->error_ = "ups.tsdb could not be opened or created";
    ESP_LOGE(TAG, "%s", this->error_);
    this->mark_failed();
    return;
  }
  // 8 KB stack: tsdb_write + LittleFS + logging overflow 4 KB in practice (tigomonitor's measurement).
  xTaskCreate(&SmartSlotHistory::writer_task_, "tsdb_writer", 8192, this, 1, nullptr);
  this->set_interval(this->interval_s_ * 1000, [this]() { this->snapshot_(); });
#ifdef USE_OTA_STATE_LISTENER
  ota::get_global_ota_callback()->add_global_state_listener(this);
#endif
  if (this->web_ != nullptr) this->web_->add_handler(this);
}

void SmartSlotHistory::update() {
  char buf[64];
  for (uint8_t c = 0; c < NUM_COLS; c++) {
    if (!nutshim_value_for_publish(kVars[c], buf, sizeof buf)) continue;
    if (c == COL_ONBATT_PCT) {
      this->status_polls_++;
      // ups.status is a space-separated flag list; OB = on battery.
      if (strstr(buf, "OB") != nullptr) this->onbatt_polls_++;
      continue;
    }
    char *end;
    float v = strtof(buf, &end);
    if (end == buf) continue;
    this->sum_[c] += v * kScale[c];
    this->n_[c]++;
  }
}

void SmartSlotHistory::snapshot_() {
  HistoryRow row;
  row.ts = this->time_->timestamp_now();
  for (uint8_t c = 0; c < NUM_COLS; c++) {
    row.v[c] = this->n_[c] ? (int16_t) lround(this->sum_[c] / this->n_[c]) : INT16_MIN;
    this->sum_[c] = 0;
    this->n_[c] = 0;
  }
  row.v[COL_ONBATT_PCT] = this->status_polls_ ? (int16_t) (100 * this->onbatt_polls_ / this->status_polls_) : INT16_MIN;
  this->status_polls_ = this->onbatt_polls_ = 0;
  if (!this->time_->now().is_valid()) {
    ESP_LOGD(TAG, "clock not set yet, snapshot skipped");
    return;
  }
  if (xQueueSend(this->queue_, &row, 0) != pdTRUE) ESP_LOGW(TAG, "writer busy, snapshot @ %lu dropped", (unsigned long) row.ts);
}

void SmartSlotHistory::writer_task_(void *arg) {
  auto *self = static_cast<SmartSlotHistory *>(arg);
  HistoryRow row;
  for (;;) {
    if (xQueueReceive(self->queue_, &row, portMAX_DELAY) != pdTRUE) continue;
    if (self->ota_active_) {
      ESP_LOGD(TAG, "OTA in progress, snapshot @ %lu skipped", (unsigned long) row.ts);
      continue;
    }
    int64_t t0 = esp_timer_get_time();
    esp_err_t err;
    {
      DbLock lock(self->db_mutex_);
      err = tsdb_write_h(self->db_, row.ts, row.v);
      if (err == ESP_OK) err = tsdb_sync_h(self->db_);
    }
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "tsdb_write @ %lu failed: %s", (unsigned long) row.ts, esp_err_to_name(err));
    } else {
      ESP_LOGI(TAG, "snapshot @ %lu written in %lld ms", (unsigned long) row.ts, (esp_timer_get_time() - t0) / 1000);
    }
  }
}

bool SmartSlotHistory::canHandle(web_server_idf::AsyncWebServerRequest *request) const {
  if (request->method() != HTTP_GET) return false;
  char url_buf[web_server_idf::AsyncWebServerRequest::URL_BUF_SIZE];
  StringRef url = request->url_to(url_buf);
  return url == "/history" || url == "/history.json";
}

void SmartSlotHistory::handleRequest(web_server_idf::AsyncWebServerRequest *request) {
  char url_buf[web_server_idf::AsyncWebServerRequest::URL_BUF_SIZE];
  if (request->url_to(url_buf) == "/history") {
    request->send(200, "text/html", HISTORY_PAGE);
    return;
  }
  this->send_json_(request);
}

static uint32_t arg_u32(web_server_idf::AsyncWebServerRequest *request, const char *name, uint32_t dflt) {
  if (!request->hasArg(name)) return dflt;
  return (uint32_t) strtoul(request->arg(name).c_str(), nullptr, 10);
}

// GET /history.json?from=<epoch>&to=<epoch>&bucket=<s>
// {"interval":300,"from":..,"to":..,"bucket":..,"rows":[[ts,charge %,runtime s,vin V,vout V,load %,temp C,onbatt %],...],
//  "stats":{...}}  A null in a row means no reading in that bucket.
void SmartSlotHistory::send_json_(web_server_idf::AsyncWebServerRequest *request) {
  if (this->db_ == nullptr) {
    std::string body = std::string("{\"error\":\"") + (this->error_ ? this->error_ : "history not running") + "\"}";
    request->send(503, "application/json", body.c_str());
    return;
  }
  uint32_t now = this->time_->timestamp_now();
  uint32_t to = arg_u32(request, "to", now), from = arg_u32(request, "from", to - 86400);
  if (from >= to) from = to - 86400;
  // Cap the response at ~720 buckets: 30 days come back hourly, a day at the snapshot interval.
  uint32_t bucket = std::max({arg_u32(request, "bucket", 0), this->interval_s_, (to - from) / 720});

  auto *stream = request->beginResponseStream("application/json");
  stream->printf("{\"interval\":%lu,\"from\":%lu,\"to\":%lu,\"bucket\":%lu,\"rows\":[", (unsigned long) this->interval_s_,
                 (unsigned long) from, (unsigned long) to, (unsigned long) bucket);
  double sum[NUM_COLS] = {};
  uint32_t n[NUM_COLS] = {}, bstart = 0;
  bool any = false;
  auto flush = [&]() {
    if (bstart == 0) return;
    stream->printf("%s[%lu", any ? "," : "", (unsigned long) bstart);
    for (uint8_t c = 0; c < NUM_COLS; c++) {
      if (n[c] == 0) {
        stream->print(",null");
      } else {
        stream->printf(",%.1f", sum[c] / n[c] / kScale[c]);
      }
      sum[c] = 0;
      n[c] = 0;
    }
    stream->print("]");
    any = true;
  };
  tsdb_stats_t st = {};
  size_t fs_total = 0, fs_used = 0;
  {
    DbLock lock(this->db_mutex_);
    tsdb_query_t q;
    if (tsdb_query_init_h(this->db_, &q, from, to, nullptr, 0) == ESP_OK) {
      uint32_t ts;
      int16_t v[NUM_COLS];
      while (tsdb_query_next(&q, &ts, v) == ESP_OK) {
        uint32_t b = ts - (ts - from) % bucket;
        if (b != bstart) {
          flush();
          bstart = b;
        }
        for (uint8_t c = 0; c < NUM_COLS; c++) {
          if (v[c] == INT16_MIN) continue;
          sum[c] += v[c];
          n[c]++;
        }
      }
      tsdb_query_close(&q);
    }
    flush();
    tsdb_get_stats_h(this->db_, &st);
    esp_littlefs_info("tsdb", &fs_total, &fs_used);
  }
  stream->printf("],\"stats\":{\"records\":%lu,\"capacity\":%lu,\"oldest\":%lu,\"newest\":%lu,\"writes\":%lu,"
                 "\"evictions\":%lu,\"file_bytes\":%lu,\"fs_used\":%u,\"fs_total\":%u}}",
                 (unsigned long) st.total_records, (unsigned long) st.max_records, (unsigned long) st.oldest_timestamp,
                 (unsigned long) st.newest_timestamp, (unsigned long) st.total_writes, (unsigned long) st.total_evictions,
                 (unsigned long) st.storage_bytes, (unsigned) fs_used, (unsigned) fs_total);
  request->send(stream);
}

void SmartSlotHistory::dump_config() {
  ESP_LOGCONFIG(TAG, "SmartSlot history: every %lu s into %s (partition %lu KB)", (unsigned long) this->interval_s_,
                kDbPath, (unsigned long) (this->partition_bytes_ / 1024));
  if (this->error_ != nullptr) ESP_LOGE(TAG, "  not running: %s", this->error_);
}

}  // namespace smartslot
}  // namespace esphome
#endif
