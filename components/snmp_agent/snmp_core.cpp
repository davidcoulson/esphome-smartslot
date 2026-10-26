// SPDX-License-Identifier: GPL-2.0-or-later
#include "snmp_core.h"

#include <cstdlib>
#include <cstring>

namespace snmp {

static const size_t MAX_RESPONSE = 1400;  // stay under a 1500-byte MTU
static const size_t MAX_VARBINDS = 64;
static const int64_t MAX_REPETITIONS = 50;

enum : uint8_t {
  T_INT = 0x02, T_STR = 0x04, T_NULL = 0x05, T_OID = 0x06, T_SEQ = 0x30,
  T_GAUGE = 0x42, T_TICKS = 0x43,
  T_NOSUCHOBJECT = 0x80, T_NOSUCHINSTANCE = 0x81, T_ENDOFMIB = 0x82,
  P_GET = 0xA0, P_GETNEXT = 0xA1, P_RESPONSE = 0xA2, P_SET = 0xA3, P_GETBULK = 0xA5,
};
enum { E_NONE = 0, E_TOOBIG = 1, E_NOSUCHNAME = 2, E_READONLY = 4, E_NOACCESS = 6 };

bool parse_oid(const std::string &s, Oid &out) {
  out.clear();
  const char *p = s.c_str();
  if (*p == '.') p++;
  while (true) {
    if (*p < '0' || *p > '9') return false;
    char *e;
    unsigned long long v = strtoull(p, &e, 10);
    if (v > 0xFFFFFFFFull) return false;
    out.push_back((uint32_t) v);
    if (*e == 0) break;
    if (*e != '.') return false;
    p = e + 1;
  }
  return out.size() >= 2;
}

Bytes ber_tlv(uint8_t tag, const Bytes &c) {
  Bytes r{tag};
  size_t n = c.size();
  if (n < 0x80) {
    r.push_back((uint8_t) n);
  } else if (n < 0x100) {
    r.push_back(0x81);
    r.push_back((uint8_t) n);
  } else {
    r.push_back(0x82);
    r.push_back((uint8_t) (n >> 8));
    r.push_back((uint8_t) n);
  }
  r.insert(r.end(), c.begin(), c.end());
  return r;
}

Bytes ber_int(uint8_t tag, int64_t v) {
  Bytes c;
  // Emit bytes until the rest is pure sign extension of the last byte emitted.
  while (true) {
    c.insert(c.begin(), (uint8_t) (v & 0xFF));
    int64_t rest = v >> 8;  // arithmetic shift
    if ((rest == 0 && !(c[0] & 0x80)) || (rest == -1 && (c[0] & 0x80))) break;
    v = rest;
  }
  return ber_tlv(tag, c);
}

Bytes ber_oid(const Oid &oid) {
  Bytes c;
  auto sub = [&c](uint32_t v) {
    uint8_t tmp[5];
    int n = 0;
    do {
      tmp[n++] = v & 0x7F;
      v >>= 7;
    } while (v);
    while (n--) c.push_back(tmp[n] | (n ? 0x80 : 0));
  };
  sub(oid[0] * 40 + oid[1]);
  for (size_t i = 2; i < oid.size(); i++) sub(oid[i]);
  return ber_tlv(T_OID, c);
}

bool ber_read(const uint8_t **p, const uint8_t *end, uint8_t *tag, const uint8_t **c, size_t *clen) {
  const uint8_t *q = *p;
  if (end - q < 2) return false;
  *tag = *q++;
  size_t len = *q++;
  if (len & 0x80) {
    int nb = len & 0x7F;
    if (nb < 1 || nb > 2 || end - q < nb) return false;  // shortcut: 2-byte lengths only, packets are < 64 KiB
    len = 0;
    while (nb--) len = (len << 8) | *q++;
  }
  if ((size_t) (end - q) < len) return false;
  *c = q;
  *clen = len;
  *p = q + len;
  return true;
}

bool ber_read_int(const uint8_t *c, size_t n, int64_t *v) {
  if (n < 1 || n > 8) return false;
  int64_t r = (c[0] & 0x80) ? -1 : 0;
  for (size_t i = 0; i < n; i++) r = (int64_t) (((uint64_t) r << 8) | c[i]);
  *v = r;
  return true;
}

bool ber_read_oid(const uint8_t *c, size_t n, Oid *oid) {
  oid->clear();
  if (n < 1) return false;
  uint64_t v = 0;
  bool first = true;
  for (size_t i = 0; i < n; i++) {
    v = (v << 7) | (c[i] & 0x7F);
    if (v > 0xFFFFFFFFull * 80) return false;
    if (c[i] & 0x80) {
      if (i + 1 == n) return false;
      continue;
    }
    if (first) {
      uint32_t a = v < 80 ? (uint32_t) (v / 40) : 2;
      oid->push_back(a);
      oid->push_back((uint32_t) (v - a * 40));
      first = false;
    } else {
      if (v > 0xFFFFFFFFull) return false;
      oid->push_back((uint32_t) v);
    }
    v = 0;
    if (oid->size() > 128) return false;
  }
  return true;
}

Entry *Agent::add(const Oid &oid, Type type) {
  std::lock_guard<std::mutex> l(mu_);
  auto r = table_.emplace(oid, Entry{});
  if (r.second) {
    r.first->second.type = type;
    r.first->second.oid = oid;
  }
  return &r.first->second;
}
void Agent::set(Entry *e, int64_t v) {
  std::lock_guard<std::mutex> l(mu_);
  e->num = v;
  e->valid = true;
}
void Agent::set(Entry *e, const std::string &s) {
  std::lock_guard<std::mutex> l(mu_);
  e->str = s;
  e->valid = true;
}
void Agent::set(Entry *e, const Oid &o) {
  std::lock_guard<std::mutex> l(mu_);
  e->oid = o;
  e->valid = true;
}
void Agent::clear(Entry *e) {
  std::lock_guard<std::mutex> l(mu_);
  e->valid = false;
}

static Bytes encode_value(const Entry &e) {
  switch (e.type) {
    case INTEGER:
      return ber_int(T_INT, e.num < INT32_MIN ? INT32_MIN : e.num > INT32_MAX ? INT32_MAX : e.num);
    case GAUGE:
    case TIMETICKS:
      return ber_int(e.type == GAUGE ? T_GAUGE : T_TICKS, e.num < 0 ? 0 : e.num > UINT32_MAX ? UINT32_MAX : e.num);
    case OBJECT_ID:
      return ber_oid(e.oid);
    default:
      return ber_tlv(T_STR, Bytes(e.str.begin(), e.str.end()));
  }
}

struct Vb {
  Oid oid;
  Bytes val;  // complete value TLV
};

static Bytes encode_vb(const Vb &v) {
  Bytes c = ber_oid(v.oid);
  c.insert(c.end(), v.val.begin(), v.val.end());
  return ber_tlv(T_SEQ, c);
}

static const Bytes NULLV = {T_NULL, 0};
static const Bytes NOSUCHOBJECT = {T_NOSUCHOBJECT, 0};
static const Bytes NOSUCHINSTANCE = {T_NOSUCHINSTANCE, 0};
static const Bytes ENDOFMIB = {T_ENDOFMIB, 0};

bool Agent::handle(const uint8_t *in, size_t n, Bytes &out) {
  const uint8_t *p = in, *end = in + n, *c;
  size_t cl;
  uint8_t tag;
  if (!ber_read(&p, end, &tag, &c, &cl) || tag != T_SEQ) return false;
  p = c;
  end = c + cl;
  int64_t version, reqid, f1, f2;
  if (!ber_read(&p, end, &tag, &c, &cl) || tag != T_INT || !ber_read_int(c, cl, &version)) return false;
  if (version != 0 && version != 1) return false;
  if (!ber_read(&p, end, &tag, &c, &cl) || tag != T_STR) return false;
  if (std::string((const char *) c, cl) != community) return false;
  uint8_t pdu;
  if (!ber_read(&p, end, &pdu, &c, &cl)) return false;
  if (pdu != P_GET && pdu != P_GETNEXT && pdu != P_SET && !(pdu == P_GETBULK && version == 1)) return false;
  p = c;
  end = c + cl;
  if (!ber_read(&p, end, &tag, &c, &cl) || tag != T_INT || !ber_read_int(c, cl, &reqid)) return false;
  if (!ber_read(&p, end, &tag, &c, &cl) || tag != T_INT || !ber_read_int(c, cl, &f1)) return false;
  if (!ber_read(&p, end, &tag, &c, &cl) || tag != T_INT || !ber_read_int(c, cl, &f2)) return false;
  if (!ber_read(&p, end, &tag, &c, &cl) || tag != T_SEQ) return false;
  p = c;
  end = c + cl;
  std::vector<Oid> req;
  while (p < end) {
    const uint8_t *vb, *e2;
    size_t vbl;
    if (!ber_read(&p, end, &tag, &vb, &vbl) || tag != T_SEQ || req.size() >= MAX_VARBINDS) return false;
    e2 = vb + vbl;
    Oid o;
    if (!ber_read(&vb, e2, &tag, &c, &cl) || tag != T_OID || !ber_read_oid(c, cl, &o)) return false;
    req.push_back(o);
  }

  const bool v1 = version == 0;
  int err = E_NONE;
  size_t err_idx = 0;
  std::vector<Vb> res;
  auto echo = [&]() {
    res.clear();
    for (auto &o : req) res.push_back({o, NULLV});
  };

  {
    std::lock_guard<std::mutex> l(mu_);
    // Next valid entry strictly after oid, or nullptr.
    auto next = [&](const Oid &oid) -> const Entry * {
      for (auto it = table_.upper_bound(oid); it != table_.end(); ++it)
        if (it->second.valid) return &it->second;
      return nullptr;
    };
    if (pdu == P_SET) {
      err = v1 ? E_READONLY : E_NOACCESS;
      err_idx = req.empty() ? 0 : 1;
      echo();
    } else if (pdu == P_GET) {
      for (size_t i = 0; i < req.size(); i++) {
        const Oid &o = req[i];
        auto it = table_.find(o);
        if (it != table_.end() && it->second.valid) {
          res.push_back({o, encode_value(it->second)});
          continue;
        }
        if (v1) {
          err = E_NOSUCHNAME;
          err_idx = i + 1;
          echo();
          break;
        }
        // noSuchInstance if the object exists with other instances (sibling OIDs), else noSuchObject.
        bool inst = false;
        Oid parent(o.begin(), o.end() - 1);
        for (auto j = table_.lower_bound(parent); j != table_.end() && j->first.size() >= parent.size() &&
                                                  std::equal(parent.begin(), parent.end(), j->first.begin());
             ++j)
          if (j->first.size() == o.size()) {
            inst = true;
            break;
          } else if (j->first.size() > o.size() && std::equal(o.begin(), o.end(), j->first.begin())) {
            inst = false;  // o is itself a prefix of a deeper entry: an object/subtree, not an instance
            break;
          }
        res.push_back({o, inst ? NOSUCHINSTANCE : NOSUCHOBJECT});
      }
    } else if (pdu == P_GETNEXT) {
      for (size_t i = 0; i < req.size(); i++) {
        const Entry *e = next(req[i]);
        if (e) {
          res.push_back({e->oid, encode_value(*e)});
        } else if (v1) {
          err = E_NOSUCHNAME;
          err_idx = i + 1;
          echo();
          break;
        } else {
          res.push_back({req[i], ENDOFMIB});
        }
      }
    } else {  // GETBULK: f1 = non-repeaters, f2 = max-repetitions
      size_t nr = f1 < 0 ? 0 : (size_t) f1 > req.size() ? req.size() : (size_t) f1;
      int64_t mr = f2 < 0 ? 0 : f2 > MAX_REPETITIONS ? MAX_REPETITIONS : f2;
      size_t size = 0;
      for (size_t i = 0; i < nr; i++) {
        const Entry *e = next(req[i]);
        res.push_back(e ? Vb{e->oid, encode_value(*e)} : Vb{req[i], ENDOFMIB});
      }
      std::vector<Oid> cur(req.begin() + nr, req.end());
      for (int64_t r = 0; r < mr && !cur.empty(); r++) {
        bool any = false;
        for (auto &o : cur) {
          const Entry *e = next(o);  // an exhausted repeater keeps its last OID and yields endOfMibView again
          Vb vb = e ? Vb{e->oid, encode_value(*e)} : Vb{o, ENDOFMIB};
          size += encode_vb(vb).size();
          if (size > MAX_RESPONSE - 100) goto bulk_done;  // return fewer rows rather than tooBig
          res.push_back(vb);
          if (e) {
            o = e->oid;
            any = true;
          }
        }
        if (!any) break;
      }
    bulk_done:;
    }
  }

  auto build = [&]() {
    Bytes vbs;
    for (auto &v : res) {
      Bytes b = encode_vb(v);
      vbs.insert(vbs.end(), b.begin(), b.end());
    }
    Bytes pd = ber_int(T_INT, reqid);
    for (const Bytes &b : {ber_int(T_INT, err), ber_int(T_INT, err_idx), ber_tlv(T_SEQ, vbs)}) pd.insert(pd.end(), b.begin(), b.end());
    Bytes m = ber_int(T_INT, version);
    for (const Bytes &b : {ber_tlv(T_STR, Bytes(community.begin(), community.end())), ber_tlv(P_RESPONSE, pd)})
      m.insert(m.end(), b.begin(), b.end());
    out = ber_tlv(T_SEQ, m);
  };
  build();
  if (out.size() > MAX_RESPONSE) {
    err = E_TOOBIG;
    err_idx = 0;
    echo();
    build();
  }
  return true;
}

}  // namespace snmp
