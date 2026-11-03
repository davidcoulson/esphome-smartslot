// SPDX-License-Identifier: GPL-2.0-or-later
// Host tests for the SNMP BER codec, OID ordering, the v1/v2c and v3 (USM) request handler and traps. Build: make snmp
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
  uint8_t tag = 0;
  int64_t reqid = -1, err = -1, idx = -1;
  std::vector<std::pair<Oid, std::pair<uint8_t, Bytes>>> vbs;
};
static Resp parse_pdu(const uint8_t *p, const uint8_t *end) {
  Resp r;
  const uint8_t *c;
  size_t cl;
  assert(ber_read(&p, end, &r.tag, &c, &cl));
  p = c; end = c + cl;
  uint8_t t;
  assert(ber_read(&p, end, &t, &c, &cl) && ber_read_int(c, cl, &r.reqid));
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
static Resp parse(const Bytes &b) {
  const uint8_t *p = b.data(), *end = p + b.size(), *c;
  size_t cl;
  uint8_t t;
  int64_t v;
  assert(ber_read(&p, end, &t, &c, &cl) && t == 0x30);
  p = c; end = c + cl;
  assert(ber_read(&p, end, &t, &c, &cl) && ber_read_int(c, cl, &v));
  assert(ber_read(&p, end, &t, &c, &cl) && t == 4);
  Resp r = parse_pdu(p, end);
  assert(r.tag == 0xA2 && r.reqid == 42);
  return r;
}
// A plaintext (noAuthNoPriv or authNoPriv) SNMPv3 message: returns its PDU and the engine ID it names.
static Resp parse_v3(const Bytes &b, Bytes *engine = nullptr) {
  const uint8_t *p = b.data(), *end = p + b.size(), *c;
  size_t cl;
  uint8_t t;
  int64_t v;
  assert(ber_read(&p, end, &t, &c, &cl) && t == 0x30);
  p = c; end = c + cl;
  assert(ber_read(&p, end, &t, &c, &cl) && ber_read_int(c, cl, &v) && v == 3);
  assert(ber_read(&p, end, &t, &c, &cl) && t == 0x30);  // msgGlobalData
  assert(ber_read(&p, end, &t, &c, &cl) && t == 4);     // msgSecurityParameters
  const uint8_t *q = c, *qe = c + cl;
  assert(ber_read(&q, qe, &t, &c, &cl) && t == 0x30);
  q = c; qe = c + cl;
  assert(ber_read(&q, qe, &t, &c, &cl) && t == 4);
  if (engine) *engine = Bytes(c, c + cl);
  assert(ber_read(&p, end, &t, &c, &cl) && t == 0x30);  // ScopedPDU
  p = c; end = c + cl;
  assert(ber_read(&p, end, &t, &c, &cl) && t == 4);
  assert(ber_read(&p, end, &t, &c, &cl) && t == 4);
  return parse_pdu(p, end);
}
static Bytes get_pdu(const char *oid) {
  Bytes vb = ber_oid(O(oid));
  cat(vb, Bytes{0x05, 0x00});
  Bytes pd = ber_int(2, 42);
  cat(pd, ber_int(2, 0));
  cat(pd, ber_int(2, 0));
  cat(pd, ber_tlv(0x30, ber_tlv(0x30, vb)));
  return ber_tlv(0xA0, pd);
}
static std::string hex(const Bytes &b) {
  std::string s;
  char h[3];
  for (auto x : b) snprintf(h, sizeof h, "%02x", x), s += h;
  return s;
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

  // --- SNMPv3 USM
  // RFC 3414 A.3.1 / A.3.2 key localization vectors
  Bytes eid2 = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2};
  assert(hex(localize_key(AUTH_MD5, "maplesyrup", eid2)) == "526f5eed9fcce26f8964c2930787d82b");
  assert(hex(localize_key(AUTH_SHA, "maplesyrup", eid2)) == "6695febc9288e36282235fc7151f128497b38f3f");
  // RFC 7860 lengths
  assert(localize_key(AUTH_SHA512, "maplesyrup", eid2).size() == 64 && mac_len(AUTH_SHA256) == 24);

  Bytes eid = {0x80, 0x00, 0x01, 0x3e, 0x03, 0x02, 0, 0, 0, 0, 0x01};
  a = make();
  a->set_engine(eid, 5, 1);
  assert(a->add_user("noauth", AUTH_NONE, "", PRIV_NONE, ""));
  assert(a->add_user("auth", AUTH_SHA256, "authpass123", PRIV_NONE, ""));
  assert(a->add_user("priv", AUTH_SHA, "authpass123", PRIV_AES, "privpass123"));
  assert(!a->add_user("bad", AUTH_NONE, "", PRIV_AES, "privpass123"));  // priv without auth
  // a "manager" with the same engine view builds requests; it holds the same localized keys
  Agent m;
  m.set_engine(eid, 5, 99);
  m.add_user("auth", AUTH_SHA256, "authpass123", PRIV_NONE, "");
  assert(m.user("auth")->auth_key == a->user("auth")->auth_key);

  // noAuthNoPriv and authNoPriv GETs are answered at the same level
  q = m.v3_message(7, 0, "noauth", nullptr, get_pdu("1.3.6.1.2.1.1.1.0"), true);
  assert(a->handle(q.data(), q.size(), out));
  r = parse_v3(out);
  assert(r.tag == 0xA2 && r.reqid == 42 && r.err == 0 && r.vbs[0].second.second == Bytes({'h', 'e', 'l', 'l', 'o'}));
  q = m.v3_message(8, 1, "auth", m.user("auth"), get_pdu("1.3.6.1.2.1.1.1.0"), true);
  assert(a->handle(q.data(), q.size(), out));
  r = parse_v3(out);
  assert(r.tag == 0xA2 && r.vbs[0].second.first == 4);

  auto report = [&](const Bytes &rq, uint32_t stat) {
    Bytes o;
    assert(a->handle(rq.data(), rq.size(), o));
    Bytes e;
    Resp rp = parse_v3(o, &e);
    assert(rp.tag == 0xA8 && e == eid && rp.vbs.size() == 1);
    assert((rp.vbs[0].first == Oid{1, 3, 6, 1, 6, 3, 15, 1, 1, stat, 0}));
  };
  // discovery: an empty engine ID gets unknownEngineIDs carrying ours
  Agent disc;
  q = disc.v3_message(1, 0, "", nullptr, get_pdu("1.3.6.1.2.1.1.1.0"), true);
  report(q, 4);
  report(m.v3_message(2, 0, "nobody", nullptr, get_pdu("1.3.6.1.2.1.1.1.0"), true), 3);  // unknownUserNames
  report(m.v3_message(3, 0, "auth", nullptr, get_pdu("1.3.6.1.2.1.1.1.0"), true), 1);    // level below the user's
  // a tampered byte in an authenticated message: wrongDigests
  q = m.v3_message(4, 1, "auth", m.user("auth"), get_pdu("1.3.6.1.2.1.1.1.0"), true);
  q[q.size() - 3] ^= 1;  // last OID subid
  report(q, 5);
  // the wrong password: wrongDigests
  Agent wrong;
  wrong.set_engine(eid, 5, 0);
  wrong.add_user("auth", AUTH_SHA256, "wrongpass123", PRIV_NONE, "");
  report(wrong.v3_message(5, 1, "auth", wrong.user("auth"), get_pdu("1.3.6.1.2.1.1.1.0"), true), 5);
  // a stale boots count: notInTimeWindows, sent authenticated
  Agent old;
  old.set_engine(eid, 4, 0);
  old.add_user("auth", AUTH_SHA256, "authpass123", PRIV_NONE, "");
  report(old.v3_message(6, 1, "auth", old.user("auth"), get_pdu("1.3.6.1.2.1.1.1.0"), true), 2);
  // not reportable: silently dropped
  q = m.v3_message(2, 0, "nobody", nullptr, get_pdu("1.3.6.1.2.1.1.1.0"), false);
  assert(!a->handle(q.data(), q.size(), out));
  // truncated and corrupted v3 messages never crash
  q = m.v3_message(8, 1, "auth", m.user("auth"), get_pdu("1.3.6.1.2.1.1.1.0"), true);
  for (size_t len = 0; len < q.size(); len++) a->handle(q.data(), len, out);
  for (int i = 0; i < 20000; i++) {
    Bytes mm = q;
    mm[rand() % mm.size()] = rand();
    a->handle(mm.data(), mm.size(), out);
  }
  // v2c: false drops v1 and v2c
  a->v2c = false;
  q = request(1, "public", 0xA0, 0, 0, {"1.3.6.1.2.1.1.1.0"});
  assert(!a->handle(q.data(), q.size(), out));
  delete a;

  // --- traps
  auto ev = status_traps("OL", "OB LB");
  assert(ev.size() == 2 && ev[0].id == 5 && ev[1].id == 7);
  ev = status_traps("OB LB", "OL CHRG");
  assert(ev.size() == 2 && ev[0].id == 9 && ev[1].id == 11);
  assert(status_traps("OL", "OL CHRG").empty() && status_traps("OL OFF", "OL").empty());
  assert(status_traps("OL", "OL OVER RB").size() == 2 && status_traps("OLB", "OL").empty());  // whole tokens only
  Agent t;
  Entry txt;
  txt.type = STRING;
  txt.str = "UPS: On battery power.";
  Bytes tr = t.trap_v2c("public", 100, O("1.3.6.1.4.1.318.0.5"), {{O("1.3.6.1.4.1.318.2.3.3.0"), txt}});
  const uint8_t *tp = tr.data(), *te = tp + tr.size(), *tc;
  size_t tl;
  uint8_t tt;
  assert(ber_read(&tp, te, &tt, &tc, &tl) && tt == 0x30);
  tp = tc; te = tc + tl;
  assert(ber_read(&tp, te, &tt, &tc, &tl) && ber_read(&tp, te, &tt, &tc, &tl) && tt == 4);
  r = parse_pdu(tp, te);
  assert(r.tag == 0xA7 && r.vbs.size() == 3);
  assert((r.vbs[0].first == Oid{1, 3, 6, 1, 2, 1, 1, 3, 0}) && r.vbs[0].second.first == 0x43);
  Bytes trap_oid = ber_oid(O("1.3.6.1.4.1.318.0.5"));
  assert((r.vbs[1].first == Oid{1, 3, 6, 1, 6, 3, 1, 1, 4, 1, 0}) && r.vbs[1].second.first == 6 &&
         r.vbs[1].second.second == Bytes(trap_oid.begin() + 2, trap_oid.end()));
  assert(r.vbs[2].second.first == 4 && r.vbs[2].second.second.size() == strlen("UPS: On battery power."));
  puts("snmp: all tests passed");
  return 0;
}
