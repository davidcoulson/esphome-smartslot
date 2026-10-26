// SPDX-License-Identifier: GPL-2.0-or-later
// Host tests for the SNMP BER codec, OID ordering and the v1/v2c request handler. Build: make snmp
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "snmp_core.h"

using namespace snmp;

static Oid O(const char *s) {
  Oid o;
  assert(parse_oid(s, o));
  return o;
}
static void cat(Bytes &a, const Bytes &b) { a.insert(a.end(), b.begin(), b.end()); }

// Independent of the library's message builder: hand-assembled request.
static Bytes request(int version, const char *comm, uint8_t pdu, int64_t f1, int64_t f2, std::vector<const char *> oids) {
  Bytes vbs;
  for (auto s : oids) {
    Bytes vb = ber_oid(O(s));
    cat(vb, Bytes{0x05, 0x00});
    cat(vbs, ber_tlv(0x30, vb));
  }
  Bytes pd = ber_int(2, 42);
  cat(pd, ber_int(2, f1));
  cat(pd, ber_int(2, f2));
  cat(pd, ber_tlv(0x30, vbs));
  Bytes m = ber_int(2, version);
  cat(m, ber_tlv(4, Bytes(comm, comm + strlen(comm))));
  cat(m, ber_tlv(pdu, pd));
  return ber_tlv(0x30, m);
}

struct Resp {
  int64_t err = -1, idx = -1;
  std::vector<std::pair<Oid, std::pair<uint8_t, Bytes>>> vbs;
};
static Resp parse(const Bytes &b) {
  Resp r;
  const uint8_t *p = b.data(), *end = p + b.size(), *c;
  size_t cl;
  uint8_t t;
  assert(ber_read(&p, end, &t, &c, &cl) && t == 0x30);
  p = c; end = c + cl;
  int64_t v;
  assert(ber_read(&p, end, &t, &c, &cl) && ber_read_int(c, cl, &v));
  assert(ber_read(&p, end, &t, &c, &cl) && t == 4);
  assert(ber_read(&p, end, &t, &c, &cl) && t == 0xA2);
  p = c; end = c + cl;
  assert(ber_read(&p, end, &t, &c, &cl) && ber_read_int(c, cl, &v) && v == 42);
  assert(ber_read(&p, end, &t, &c, &cl) && ber_read_int(c, cl, &r.err));
  assert(ber_read(&p, end, &t, &c, &cl) && ber_read_int(c, cl, &r.idx));
  assert(ber_read(&p, end, &t, &c, &cl) && t == 0x30);
  p = c; end = c + cl;
  while (p < end) {
    const uint8_t *vb;
    size_t vl;
    assert(ber_read(&p, end, &t, &vb, &vl));
    const uint8_t *ve = vb + vl;
    Oid o;
    assert(ber_read(&vb, ve, &t, &c, &cl) && t == 6 && ber_read_oid(c, cl, &o));
    assert(ber_read(&vb, ve, &t, &c, &cl));
    r.vbs.push_back({o, {t, Bytes(c, c + cl)}});
  }
  return r;
}

static Agent *make() {
  Agent *a = new Agent;
  a->set(a->add(O("1.3.6.1.2.1.1.1.0"), STRING), std::string("hello"));
  a->set(a->add(O("1.3.6.1.4.1.318.1.1.1.2.2.1.0"), GAUGE), (int64_t) 100);
  a->set(a->add(O("1.3.6.1.4.1.318.1.1.1.2.2.10.0"), GAUGE), (int64_t) 7);
  a->set(a->add(O("1.3.6.1.4.1.318.1.1.1.2.2.2.0"), INTEGER), (int64_t) -5);
  a->set(a->add(O("1.3.6.1.4.1.318.1.1.1.2.2.3.0"), TIMETICKS), (int64_t) 600000);
  a->add(O("1.3.6.1.4.1.318.1.1.1.9.9.0"), GAUGE);  // never set: must be invisible
  return a;
}
static Resp ask(Agent &a, const Bytes &q) {
  Bytes out;
  assert(a.handle(q.data(), q.size(), out));
  return parse(out);
}

int main() {
  // --- BER known vectors
  assert((ber_int(2, 128) == Bytes{2, 2, 0, 0x80}));
  assert((ber_int(2, -129) == Bytes{2, 2, 0xFF, 0x7F}));
  assert((ber_int(2, 0) == Bytes{2, 1, 0}));
  assert((ber_int(2, -1) == Bytes{2, 1, 0xFF}));
  assert((ber_int(0x42, 4294967295LL) == Bytes{0x42, 5, 0, 0xFF, 0xFF, 0xFF, 0xFF}));
  assert((ber_oid(O("1.3.6.1.4.1.318")) == Bytes{6, 7, 0x2B, 6, 1, 4, 1, 0x82, 0x3E}));
  assert(ber_tlv(4, Bytes(200, 'x')).size() == 203 && ber_tlv(4, Bytes(200, 'x'))[1] == 0x81);
  for (int64_t v : {0LL, 1LL, 127LL, 128LL, 255LL, 256LL, -128LL, -129LL, 65535LL, 2147483647LL, -2147483648LL, 4294967295LL}) {
    Bytes b = ber_int(2, v);
    int64_t r;
    assert(ber_read_int(b.data() + 2, b[1], &r) && r == v);
  }
  Oid big = {1, 3, 4294967295u, 128, 16384};
  Bytes b = ber_oid(big);
  Oid back;
  assert(ber_read_oid(b.data() + 2, b[1], &back) && back == big);
  Oid bad;
  assert(!parse_oid("1", bad) && !parse_oid("1.x", bad) && !parse_oid("1.3.", bad) && !parse_oid("1.99999999999", bad));

  // --- OID table ordering: numeric per subid, parent before child
  Agent *a = make();
  Oid prev;
  for (auto &kv : a->table()) {
    assert(prev < kv.first);
    prev = kv.first;
  }
  assert(O("1.3.6.1.4.1.318.1.1.1.2.2.2.0") < O("1.3.6.1.4.1.318.1.1.1.2.2.10.0"));  // 2 < 10, not "10" < "2"
  assert(O("1.3.6.1.2.1.1.1") < O("1.3.6.1.2.1.1.1.0"));

  // --- GET
  Resp r = ask(*a, request(1, "public", 0xA0, 0, 0,
                           {"1.3.6.1.2.1.1.1.0", "1.3.6.1.4.1.318.1.1.1.2.2.2.0", "1.3.6.1.4.1.318.1.1.1.2.2.1.1",
                            "1.3.6.1.4.1.318.1.1.1.9.9.0", "1.3.6.1.4.1.999.0", "1.3.6.1.4.1.318.1.1.1.2.2.1.0"}));
  assert(r.err == 0 && r.vbs.size() == 6);
  assert(r.vbs[0].second.first == 4 && r.vbs[0].second.second == Bytes({'h', 'e', 'l', 'l', 'o'}));
  assert(r.vbs[1].second.first == 2 && r.vbs[1].second.second == Bytes{0xFB});
  assert(r.vbs[2].second.first == 0x81);  // object exists, instance does not
  assert(r.vbs[3].second.first == 0x81);  // unset entry
  assert(r.vbs[4].second.first == 0x80);  // noSuchObject
  assert(r.vbs[5].second.first == 0x42 && r.vbs[5].second.second == Bytes{100});

  // --- wrong community / garbage / unsupported PDUs are dropped
  Bytes q = request(1, "private", 0xA0, 0, 0, {"1.3.6.1.2.1.1.1.0"}), out;
  assert(!a->handle(q.data(), q.size(), out));
  q = request(3, "public", 0xA0, 0, 0, {"1.3.6.1.2.1.1.1.0"});
  assert(!a->handle(q.data(), q.size(), out));
  q = request(1, "public", 0xA2, 0, 0, {"1.3.6.1.2.1.1.1.0"});  // a response PDU
  assert(!a->handle(q.data(), q.size(), out));
  q = request(0, "public", 0xA5, 0, 5, {"1.3.6"});  // GETBULK in v1
  assert(!a->handle(q.data(), q.size(), out));

  // --- full walk via GETNEXT terminates, in order, skipping the unset entry
  Oid cur = O("1.3");
  size_t n = 0;
  while (true) {
    Bytes rq = request(1, "public", 0xA1, 0, 0, {});
    // build with the current oid as a string
    std::string s;
    for (size_t i = 0; i < cur.size(); i++) s += (i ? "." : "") + std::to_string(cur[i]);
    rq = request(1, "public", 0xA1, 0, 0, {s.c_str()});
    Resp w = ask(*a, rq);
    assert(w.vbs.size() == 1);
    if (w.vbs[0].second.first == 0x82) break;
    assert(cur < w.vbs[0].first);
    cur = w.vbs[0].first;
    assert(++n < 100);
  }
  assert(n == 5);

  // --- GETBULK: 1 non-repeater, 3 repetitions; and running off the end yields endOfMibView, not an endless walk
  r = ask(*a, request(1, "public", 0xA5, 1, 3, {"1.3.6.1.2.1.1", "1.3.6.1.4.1.318"}));
  assert(r.vbs.size() == 1 + 3 && r.vbs[0].first == O("1.3.6.1.2.1.1.1.0"));
  assert(r.vbs[1].first == O("1.3.6.1.4.1.318.1.1.1.2.2.1.0") && r.vbs[3].first == O("1.3.6.1.4.1.318.1.1.1.2.2.3.0"));
  r = ask(*a, request(1, "public", 0xA5, 0, 50, {"1.3.6.1.4.1.318.1.1.1.2.2.10"}));
  assert(r.vbs.size() == 2 && r.vbs[0].first == O("1.3.6.1.4.1.318.1.1.1.2.2.10.0") && r.vbs[1].second.first == 0x82);

  // --- SET is refused
  r = ask(*a, request(1, "public", 0xA3, 0, 0, {"1.3.6.1.2.1.1.1.0"}));
  assert(r.err == 6 && r.idx == 1);
  r = ask(*a, request(0, "public", 0xA3, 0, 0, {"1.3.6.1.2.1.1.1.0"}));
  assert(r.err == 4);

  // --- SNMPv1 error mapping
  r = ask(*a, request(0, "public", 0xA0, 0, 0, {"1.3.6.1.2.1.1.1.0", "1.3.6.1.4.1.999.0"}));
  assert(r.err == 2 && r.idx == 2 && r.vbs.size() == 2);
  r = ask(*a, request(0, "public", 0xA1, 0, 0, {"1.3.6.1.4.1.318.1.1.1.2.2.10.0"}));
  assert(r.err == 2 && r.idx == 1);

  // --- truncated or corrupted input never crashes and is either dropped or answered
  q = request(1, "public", 0xA5, 0, 5, {"1.3.6.1.2.1.1.1.0", "1.3.6.1.4.1.318"});
  for (size_t len = 0; len < q.size(); len++) assert(!a->handle(q.data(), len, out) || true);
  srand(1);
  for (int i = 0; i < 20000; i++) {
    Bytes m = q;
    m[rand() % m.size()] = rand();
    a->handle(m.data(), m.size(), out);
  }
  delete a;
  puts("snmp: all tests passed");
  return 0;
}
