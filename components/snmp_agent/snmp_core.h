// SPDX-License-Identifier: GPL-2.0-or-later
// SNMPv1/v2c/v3 (USM) request handling, notifications and a minimal BER codec. No ESPHome or lwIP dependencies, so
// it builds on the host.
#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "snmp_crypto.h"

namespace snmp {

using Oid = std::vector<uint32_t>;  // std::vector's operator< is lexicographic, which is SNMP OID order
using Bytes = std::vector<uint8_t>;

enum Type { INTEGER, GAUGE, TIMETICKS, STRING, OBJECT_ID };

// BER primitives (exposed for the tests)
bool parse_oid(const std::string &s, Oid &out);  // "1.3.6.1" or ".1.3.6.1"; at least two subids
Bytes ber_tlv(uint8_t tag, const Bytes &content);
Bytes ber_int(uint8_t tag, int64_t v);
Bytes ber_oid(const Oid &oid);
// Reads one TLV from [*p, end); on success advances *p past it and sets the tag and content span.
bool ber_read(const uint8_t **p, const uint8_t *end, uint8_t *tag, const uint8_t **c, size_t *clen);
bool ber_read_int(const uint8_t *c, size_t clen, int64_t *v);
bool ber_read_oid(const uint8_t *c, size_t clen, Oid *oid);

struct Entry {
  Type type;
  bool valid{false};  // false until a value is set: skipped by GETNEXT, noSuchInstance for GET
  int64_t num{0};
  std::string str;
  Oid oid;
};

struct User {
  std::string name;
  Auth auth{AUTH_NONE};
  Priv priv{PRIV_NONE};
  Bytes auth_key, priv_key;  // localized to the engine ID
  int level() const { return priv != PRIV_NONE ? 2 : auth != AUTH_NONE ? 1 : 0; }
};

// RFC 3414 A.2: password to key, then localized to the engine ID. Empty on failure.
Bytes localize_key(Auth a, const std::string &password, const Bytes &engine_id);

// Persisted engine state. snmpEngineBoots must go up on every restart (RFC 3414 2.2.2); the salt is part of the
// engine ID, so lost or unreadable state (loaded false) gives a new engine ID and new localized keys, and messages
// captured under the old boot count no longer authenticate. Latches at the maximum boot count.
struct EngineState {
  uint32_t salt;
  uint32_t boots;
};
void next_engine_state(EngineState &st, bool loaded, uint32_t fresh_salt);
// RFC 3411 SnmpEngineID format 5: enterprise 318 (APC) with the high bit set, then the MAC address and the salt.
Bytes engine_id_from(const uint8_t mac[6], uint32_t salt);

// PowerNet-MIB traps (ENTERPRISE apc, sent as 1.3.6.1.4.1.318.0.<id>) implied by a ups.status change.
struct TrapEvent {
  uint32_t id;
  const char *text;
};
std::vector<TrapEvent> status_traps(const std::string &prev, const std::string &cur);

struct Pdu;
struct Result;

class Agent {
 public:
  std::string community{"public"};
  bool v2c{true};  // also covers v1

  Entry *add(const Oid &oid, Type type);  // returns the existing entry if the OID is already present
  void set(Entry *e, int64_t v);
  void set(Entry *e, const std::string &s);
  void set(Entry *e, const Oid &o);
  void clear(Entry *e);
  const std::map<Oid, Entry> &table() const { return table_; }

  // Returns false when the datagram must be dropped silently (garbage, wrong community, unsupported PDU).
  bool handle(const uint8_t *in, size_t n, Bytes &out);

  // SNMPv3. Call set_engine before add_user; boots must go up on every restart (RFC 3414 2.2.2).
  void set_engine(const Bytes &engine_id, uint32_t boots, uint64_t salt_seed);
  bool add_user(const std::string &name, Auth auth, const std::string &auth_pw, Priv priv, const std::string &priv_pw);
  const Bytes &engine_id() const { return engine_id_; }
  uint32_t engine_boots() const { return boots_; }
  uint32_t engine_time() const;
  const User *user(const std::string &name) const;

  // Notifications (SNMPv2-Trap-PDU). vars are extra varbinds after sysUpTime.0 and snmpTrapOID.0.
  Bytes trap_v2c(const std::string &comm, uint32_t uptime_cs, const Oid &trap,
                 const std::vector<std::pair<Oid, Entry>> &vars);
  Bytes trap_v3(const User &u, uint32_t uptime_cs, const Oid &trap, const std::vector<std::pair<Oid, Entry>> &vars);
  // A whole v3 message from this engine at the user's level (for traps, and for the tests to build requests).
  Bytes v3_message(int64_t msg_id, int level, const std::string &user_name, const User *u, const Bytes &pdu,
                   bool reportable);

 private:
  Result process_(const Pdu &p, bool v1, size_t budget);
  bool handle_v3_(const uint8_t *msg, size_t n, const uint8_t *p, const uint8_t *end, Bytes &out);
  Bytes report_(int64_t msg_id, int level, const std::string &user_name, const User *u, int64_t reqid,
                const Oid &counter, uint32_t value);
  Bytes trap_pdu_(uint32_t uptime_cs, const Oid &trap, const std::vector<std::pair<Oid, Entry>> &vars);

  std::mutex mu_;
  std::map<Oid, Entry> table_;  // node-based, so Entry pointers stay valid
  Bytes engine_id_;
  uint32_t boots_{1};
  std::chrono::steady_clock::time_point t0_{std::chrono::steady_clock::now()};
  std::vector<User> users_;
  std::atomic<uint64_t> salt_{0};
  std::atomic<uint32_t> trap_id_{1};
  // usmStats counters (1.3.6.1.6.3.15.1.1.x) and snmpUnknownContexts, only touched by handle()
  uint32_t unsupported_levels_{0}, not_in_window_{0}, unknown_users_{0}, unknown_engines_{0}, wrong_digests_{0},
      decryption_errors_{0}, unknown_contexts_{0};
};

}  // namespace snmp
