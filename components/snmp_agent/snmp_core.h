// SPDX-License-Identifier: GPL-2.0-or-later
// SNMPv1/v2c request handling and a minimal BER codec. No ESPHome or lwIP dependencies, so it builds on the host.
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

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

class Agent {
 public:
  std::string community{"public"};

  Entry *add(const Oid &oid, Type type);  // returns the existing entry if the OID is already present
  void set(Entry *e, int64_t v);
  void set(Entry *e, const std::string &s);
  void set(Entry *e, const Oid &o);
  void clear(Entry *e);
  const std::map<Oid, Entry> &table() const { return table_; }

  // Returns false when the datagram must be dropped silently (garbage, wrong community, unsupported PDU).
  bool handle(const uint8_t *in, size_t n, Bytes &out);

 private:
  std::mutex mu_;
  std::map<Oid, Entry> table_;  // node-based, so Entry pointers stay valid
};

}  // namespace snmp
