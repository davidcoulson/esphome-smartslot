// SPDX-License-Identifier: GPL-2.0-or-later
#include "snmp_core.h"

#include <cstdlib>
#include <algorithm>
#include <cstring>

namespace snmp {

static const size_t MAX_RESPONSE = 1400;  // stay under a 1500-byte MTU
static const size_t MAX_VARBINDS = 64;
static const int64_t MAX_REPETITIONS = 50;

enum : uint8_t {
  T_INT = 0x02, T_STR = 0x04, T_NULL = 0x05, T_OID = 0x06, T_SEQ = 0x30,
  T_COUNTER = 0x41, T_GAUGE = 0x42, T_TICKS = 0x43,
  T_NOSUCHOBJECT = 0x80, T_NOSUCHINSTANCE = 0x81, T_ENDOFMIB = 0x82,
  P_GET = 0xA0, P_GETNEXT = 0xA1, P_RESPONSE = 0xA2, P_SET = 0xA3, P_GETBULK = 0xA5, P_TRAP2 = 0xA7, P_REPORT = 0xA8,
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

struct Pdu {
  uint8_t type{0};
  int64_t reqid{0}, f1{0}, f2{0};
  std::vector<Oid> req;
};

struct Result {
  int err{E_NONE};
  size_t idx{0};
  std::vector<Vb> vbs;
};

static void cat(Bytes &a, const Bytes &b) { a.insert(a.end(), b.begin(), b.end()); }

static Bytes encode_vb(const Vb &v) {
  Bytes c = ber_oid(v.oid);
  cat(c, v.val);
  return ber_tlv(T_SEQ, c);
}

static const Bytes NULLV = {T_NULL, 0};
static const Bytes NOSUCHOBJECT = {T_NOSUCHOBJECT, 0};
static const Bytes NOSUCHINSTANCE = {T_NOSUCHINSTANCE, 0};
static const Bytes ENDOFMIB = {T_ENDOFMIB, 0};

static bool read_int(const uint8_t **p, const uint8_t *end, int64_t *v) {
  const uint8_t *c;
  size_t cl;
  uint8_t tag;
  return ber_read(p, end, &tag, &c, &cl) && tag == T_INT && ber_read_int(c, cl, v);
}

// One PDU TLV at [p, end): request-id, two integer fields (error status/index, or non-repeaters/max-repetitions), varbinds.
static bool parse_pdu(const uint8_t *p, const uint8_t *end, Pdu &d) {
  const uint8_t *c;
  size_t cl;
  uint8_t tag;
  if (!ber_read(&p, end, &d.type, &c, &cl)) return false;
  p = c;
  end = c + cl;
  if (!read_int(&p, end, &d.reqid) || !read_int(&p, end, &d.f1) || !read_int(&p, end, &d.f2)) return false;
  if (!ber_read(&p, end, &tag, &c, &cl) || tag != T_SEQ) return false;
  p = c;
  end = c + cl;
  while (p < end) {
    const uint8_t *vb, *e2;
    size_t vbl;
    if (!ber_read(&p, end, &tag, &vb, &vbl) || tag != T_SEQ || d.req.size() >= MAX_VARBINDS) return false;
    e2 = vb + vbl;
    Oid o;
    if (!ber_read(&vb, e2, &tag, &c, &cl) || tag != T_OID || !ber_read_oid(c, cl, &o)) return false;
    d.req.push_back(o);
  }
  return true;
}

// ScopedPDU: contextEngineID, contextName, PDU.
static bool parse_scoped(const uint8_t *s, const uint8_t *se, std::string *ctx_name, Pdu &d) {
  const uint8_t *c;
  size_t cl;
  uint8_t tag;
  if (!ber_read(&s, se, &tag, &c, &cl) || tag != T_SEQ) return false;
  s = c;
  se = c + cl;
  if (!ber_read(&s, se, &tag, &c, &cl) || tag != T_STR) return false;
  if (!ber_read(&s, se, &tag, &c, &cl) || tag != T_STR) return false;
  ctx_name->assign((const char *) c, cl);
  return parse_pdu(s, se, d);
}

static Bytes pdu_bytes(uint8_t tag, int64_t reqid, int err, size_t idx, const std::vector<Vb> &vbs) {
  Bytes v;
  for (auto &vb : vbs) cat(v, encode_vb(vb));
  Bytes pd = ber_int(T_INT, reqid);
  cat(pd, ber_int(T_INT, err));
  cat(pd, ber_int(T_INT, (int64_t) idx));
  cat(pd, ber_tlv(T_SEQ, v));
  return ber_tlv(tag, pd);
}

static std::vector<Vb> echo(const std::vector<Oid> &req) {
  std::vector<Vb> r;
  for (auto &o : req) r.push_back({o, NULLV});
  return r;
}

Result Agent::process_(const Pdu &d, bool v1, size_t budget) {
  Result r;
  const std::vector<Oid> &req = d.req;
  std::lock_guard<std::mutex> l(mu_);
  // Next valid entry strictly after oid, or nullptr.
  auto next = [&](const Oid &oid) -> const Entry * {
    for (auto it = table_.upper_bound(oid); it != table_.end(); ++it)
      if (it->second.valid) return &it->second;
    return nullptr;
  };
  if (d.type == P_SET) {
    r.err = v1 ? E_READONLY : E_NOACCESS;
    r.idx = req.empty() ? 0 : 1;
    r.vbs = echo(req);
  } else if (d.type == P_GET) {
    for (size_t i = 0; i < req.size(); i++) {
      const Oid &o = req[i];
      auto it = table_.find(o);
      if (it != table_.end() && it->second.valid) {
        r.vbs.push_back({o, encode_value(it->second)});
        continue;
      }
      if (v1) {
        r.err = E_NOSUCHNAME;
        r.idx = i + 1;
        r.vbs = echo(req);
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
      r.vbs.push_back({o, inst ? NOSUCHINSTANCE : NOSUCHOBJECT});
    }
  } else if (d.type == P_GETNEXT) {
    for (size_t i = 0; i < req.size(); i++) {
      const Entry *e = next(req[i]);
      if (e) {
        r.vbs.push_back({e->oid, encode_value(*e)});
      } else if (v1) {
        r.err = E_NOSUCHNAME;
        r.idx = i + 1;
        r.vbs = echo(req);
        break;
      } else {
        r.vbs.push_back({req[i], ENDOFMIB});
      }
    }
  } else {  // GETBULK: f1 = non-repeaters, f2 = max-repetitions
    size_t nr = d.f1 < 0 ? 0 : (size_t) d.f1 > req.size() ? req.size() : (size_t) d.f1;
    int64_t mr = d.f2 < 0 ? 0 : d.f2 > MAX_REPETITIONS ? MAX_REPETITIONS : d.f2;
    size_t size = 0;
    for (size_t i = 0; i < nr; i++) {
      const Entry *e = next(req[i]);
      r.vbs.push_back(e ? Vb{e->oid, encode_value(*e)} : Vb{req[i], ENDOFMIB});
    }
    std::vector<Oid> cur(req.begin() + nr, req.end());
    for (int64_t rep = 0; rep < mr && !cur.empty(); rep++) {
      bool any = false;
      for (auto &o : cur) {
        const Entry *e = next(o);  // an exhausted repeater keeps its last OID and yields endOfMibView again
        Vb vb = e ? Vb{e->oid, encode_value(*e)} : Vb{o, ENDOFMIB};
        size += encode_vb(vb).size();
        if (size > budget) return r;  // return fewer rows rather than tooBig
        r.vbs.push_back(vb);
        if (e) {
          o = e->oid;
          any = true;
        }
      }
      if (!any) break;
    }
  }
  return r;
}

bool Agent::handle(const uint8_t *in, size_t n, Bytes &out) {
  const uint8_t *p = in, *end = in + n, *c;
  size_t cl;
  uint8_t tag;
  if (!ber_read(&p, end, &tag, &c, &cl) || tag != T_SEQ) return false;
  p = c;
  end = c + cl;
  int64_t version;
  if (!read_int(&p, end, &version)) return false;
  if (version == 3) return this->handle_v3_(in, end - in, p, end, out);
  if ((version != 0 && version != 1) || !this->v2c) return false;
  if (!ber_read(&p, end, &tag, &c, &cl) || tag != T_STR) return false;
  if (std::string((const char *) c, cl) != community) return false;
  Pdu d;
  if (!parse_pdu(p, end, d)) return false;
  if (d.type != P_GET && d.type != P_GETNEXT && d.type != P_SET && !(d.type == P_GETBULK && version == 1)) return false;
  Result r = this->process_(d, version == 0, MAX_RESPONSE - 100);
  auto build = [&]() {
    Bytes m = ber_int(T_INT, version);
    cat(m, ber_tlv(T_STR, Bytes(community.begin(), community.end())));
    cat(m, pdu_bytes(P_RESPONSE, d.reqid, r.err, r.idx, r.vbs));
    out = ber_tlv(T_SEQ, m);
  };
  build();
  if (out.size() > MAX_RESPONSE) {
    // v1 echoes the request (RFC 1157 4.1.2); v2c sends no varbinds (RFC 3416 4.2.1)
    r = Result{E_TOOBIG, 0, version == 0 ? echo(d.req) : std::vector<Vb>{}};
    build();
  }
  return out.size() <= MAX_RESPONSE;
}

// ---- SNMPv3 / USM (RFC 3412, RFC 3414, RFC 3826 AES, RFC 7860 SHA-2) ----

static const uint32_t TIME_WINDOW = 150;  // seconds, RFC 3414 3.2 step 7

static void be32(uint8_t *p, uint32_t v) {
  for (int i = 0; i < 4; i++) p[i] = (uint8_t) (v >> (24 - 8 * i));
}

// AES-128 in 128-bit CFB mode, built on single-block encryption. in and out may be the same buffer.
static bool aes_cfb(bool enc, const uint8_t *key, const uint8_t iv0[16], const uint8_t *in, size_t n, uint8_t *out) {
  uint8_t iv[16], ks[16];
  memcpy(iv, iv0, 16);
  for (size_t i = 0; i < n; i += 16) {
    if (!crypto_aes128_block(key, iv, ks)) return false;
    size_t m = std::min<size_t>(16, n - i);
    for (size_t j = 0; j < m; j++) {
      uint8_t x = in[i + j];
      out[i + j] = x ^ ks[j];
      iv[j] = enc ? out[i + j] : x;  // the ciphertext feeds the next block
    }
  }
  return true;
}

Bytes localize_key(Auth a, const std::string &password, const Bytes &engine_id) {
  size_t h = hash_len(a);
  if (h == 0) return {};
  Bytes ku(h), kul(h);
  if (!crypto_hash_repeated(a, (const uint8_t *) password.data(), password.size(), 1048576, ku.data())) return {};
  Bytes buf = ku;
  cat(buf, engine_id);
  cat(buf, ku);
  if (!crypto_hash(a, buf.data(), buf.size(), kul.data())) return {};
  return kul;
}

void Agent::set_engine(const Bytes &engine_id, uint32_t boots, uint64_t salt_seed) {
  engine_id_ = engine_id;
  boots_ = boots;
  salt_ = salt_seed;
  t0_ = std::chrono::steady_clock::now();
}

uint32_t Agent::engine_time() const {
  return (uint32_t) std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - t0_).count();
}

bool Agent::add_user(const std::string &name, Auth auth, const std::string &auth_pw, Priv priv,
                     const std::string &priv_pw) {
  User u;
  u.name = name;
  u.auth = auth;
  u.priv = priv;
  if (priv != PRIV_NONE && auth == AUTH_NONE) return false;
  if (auth != AUTH_NONE && (u.auth_key = localize_key(auth, auth_pw, engine_id_)).empty()) return false;
  if (priv == PRIV_AES) {
    u.priv_key = localize_key(auth, priv_pw, engine_id_);  // priv keys are localized with the auth hash
    if (u.priv_key.size() < 16) return false;
    u.priv_key.resize(16);
  }
  users_.push_back(u);
  return true;
}

const User *Agent::user(const std::string &name) const {
  for (auto &u : users_)
    if (u.name == name) return &u;
  return nullptr;
}

Bytes Agent::v3_message(int64_t msg_id, int level, const std::string &user_name, const User *u, const Bytes &pdu,
                        bool reportable) {
  if (level > 0 && (u == nullptr || u->level() < level)) return {};
  Bytes scoped = ber_tlv(T_STR, engine_id_);
  cat(scoped, ber_tlv(T_STR, {}));
  cat(scoped, pdu);
  scoped = ber_tlv(T_SEQ, scoped);
  uint32_t b = boots_, t = engine_time();
  Bytes priv_params, data = scoped;
  if (level == 2) {
    uint64_t s = salt_++;
    uint8_t iv[16];
    be32(iv, b);
    be32(iv + 4, t);
    be32(iv + 8, (uint32_t) (s >> 32));
    be32(iv + 12, (uint32_t) s);
    priv_params.assign(iv + 8, iv + 16);
    Bytes enc(scoped.size());
    if (!aes_cfb(true, u->priv_key.data(), iv, scoped.data(), scoped.size(), enc.data())) return {};
    data = ber_tlv(T_STR, enc);
  }
  size_t ml = level ? mac_len(u->auth) : 0;
  Bytes head = ber_tlv(T_STR, engine_id_);
  cat(head, ber_int(T_INT, b));
  cat(head, ber_int(T_INT, t));
  cat(head, ber_tlv(T_STR, Bytes(user_name.begin(), user_name.end())));
  Bytes inner = head;
  cat(inner, ber_tlv(T_STR, Bytes(ml, 0)));
  cat(inner, ber_tlv(T_STR, priv_params));
  Bytes sp_seq = ber_tlv(T_SEQ, inner);
  Bytes sp = ber_tlv(T_STR, sp_seq);
  Bytes gd = ber_int(T_INT, msg_id);
  cat(gd, ber_int(T_INT, 1500));
  cat(gd, ber_tlv(T_STR, Bytes{(uint8_t) ((level == 2 ? 3 : level) | (reportable ? 4 : 0))}));
  cat(gd, ber_int(T_INT, 3));  // USM
  gd = ber_tlv(T_SEQ, gd);
  Bytes ver = ber_int(T_INT, 3);
  Bytes body = ver;
  cat(body, gd);
  cat(body, sp);
  cat(body, data);
  Bytes m = ber_tlv(T_SEQ, body);
  if (level > 0) {
    // msgAuthenticationParameters content: past the headers of the message, the security-parameter string and its
    // sequence, then the fields before it and its own two-byte header (ml < 128).
    size_t off = (m.size() - body.size()) + ver.size() + gd.size() + (sp.size() - sp_seq.size()) +
                 (sp_seq.size() - inner.size()) + head.size() + 2;
    uint8_t mac[64];
    if (!crypto_hmac(u->auth, u->auth_key.data(), u->auth_key.size(), m.data(), m.size(), mac)) return {};
    memcpy(&m[off], mac, ml);
  }
  return m;
}

Bytes Agent::report_(int64_t msg_id, int level, const std::string &user_name, const User *u, int64_t reqid,
                     const Oid &counter, uint32_t value) {
  return v3_message(msg_id, level, user_name, u, pdu_bytes(P_REPORT, reqid, 0, 0, {Vb{counter, ber_int(T_COUNTER, value)}}),
                    false);
}

static Oid usm_stat(uint32_t i) { return {1, 3, 6, 1, 6, 3, 15, 1, 1, i, 0}; }

bool Agent::handle_v3_(const uint8_t *msg, size_t n, const uint8_t *p, const uint8_t *end, Bytes &out) {
  const uint8_t *c, *q, *qe;
  size_t cl;
  uint8_t tag;
  int64_t msg_id, max_size, model;
  // msgGlobalData
  if (!ber_read(&p, end, &tag, &c, &cl) || tag != T_SEQ) return false;
  q = c;
  qe = c + cl;
  if (!read_int(&q, qe, &msg_id) || !read_int(&q, qe, &max_size)) return false;
  if (!ber_read(&q, qe, &tag, &c, &cl) || tag != T_STR || cl != 1) return false;
  uint8_t flags = c[0];
  if (!read_int(&q, qe, &model) || model != 3) return false;
  if (msg_id < 0 || msg_id > INT32_MAX || max_size < 484 || (flags & 3) == 2) return false;  // priv needs auth
  int level = (flags & 2) ? 2 : (flags & 1);
  bool reportable = flags & 4;
  // msgSecurityParameters: an OCTET STRING holding UsmSecurityParameters
  if (!ber_read(&p, end, &tag, &c, &cl) || tag != T_STR) return false;
  q = c;
  qe = c + cl;
  if (!ber_read(&q, qe, &tag, &c, &cl) || tag != T_SEQ) return false;
  q = c;
  qe = c + cl;
  const uint8_t *eid, *uname, *aparams, *pparams;
  size_t eidl, unamel, aparamsl, pparamsl;
  int64_t mboots, mtime;
  if (!ber_read(&q, qe, &tag, &eid, &eidl) || tag != T_STR || eidl > 32) return false;
  if (!read_int(&q, qe, &mboots) || !read_int(&q, qe, &mtime)) return false;
  if (!ber_read(&q, qe, &tag, &uname, &unamel) || tag != T_STR || unamel > 32) return false;
  if (!ber_read(&q, qe, &tag, &aparams, &aparamsl) || tag != T_STR) return false;
  if (!ber_read(&q, qe, &tag, &pparams, &pparamsl) || tag != T_STR) return false;
  std::string name((const char *) uname, unamel);
  // msgData: a plaintext ScopedPDU, or an OCTET STRING holding the encrypted one
  const uint8_t *data = p, *dc;
  size_t dl;
  if (!ber_read(&p, end, &tag, &dc, &dl)) return false;
  uint8_t data_tag = tag;

  Pdu d;
  std::string ctx;
  bool parsed = level < 2 && data_tag == T_SEQ && parse_scoped(data, p, &ctx, d);
  auto rep = [&](int lvl, const User *u, const Oid &counter, uint32_t &value) {
    value++;
    if (!reportable) return false;
    out = this->report_(msg_id, lvl, name, u, parsed ? d.reqid : 0, counter, value);
    return !out.empty();
  };

  if (eidl == 0 || Bytes(eid, eid + eidl) != engine_id_) return rep(0, nullptr, usm_stat(4), unknown_engines_);
  const User *u = this->user(name);
  if (u == nullptr) return rep(0, nullptr, usm_stat(3), unknown_users_);
  // No VACM: each user is served only at its configured level.
  if (level != u->level()) return rep(0, nullptr, usm_stat(1), unsupported_levels_);
  if (level > 0) {
    size_t ml = mac_len(u->auth);
    if (aparamsl != ml) return rep(0, nullptr, usm_stat(5), wrong_digests_);
    Bytes copy(msg, msg + n);
    memset(&copy[aparams - msg], 0, ml);
    uint8_t mac[64], diff = 0;
    if (!crypto_hmac(u->auth, u->auth_key.data(), u->auth_key.size(), copy.data(), copy.size(), mac)) return false;
    for (size_t i = 0; i < ml; i++) diff |= mac[i] ^ aparams[i];
    if (diff) return rep(0, nullptr, usm_stat(5), wrong_digests_);
    int64_t now = engine_time();
    // At the latched maximum, boots can no longer tell restarts apart, so nothing is in the window (RFC 3414 3.2 7a).
    if (boots_ >= 2147483647u || mboots != boots_ || mtime < now - TIME_WINDOW || mtime > now + TIME_WINDOW)
      return rep(1, u, usm_stat(2), not_in_window_);  // authenticated, so the manager can resynchronise
  }
  if (level == 2) {
    if (data_tag != T_STR || pparamsl != 8 || mboots < 0 || mtime < 0)
      return rep(0, nullptr, usm_stat(6), decryption_errors_);
    uint8_t iv[16];
    be32(iv, (uint32_t) mboots);
    be32(iv + 4, (uint32_t) mtime);
    memcpy(iv + 8, pparams, 8);
    Bytes plain(dl);
    if (!aes_cfb(false, u->priv_key.data(), iv, dc, dl, plain.data())) return false;
    parsed = parse_scoped(plain.data(), plain.data() + plain.size(), &ctx, d);
    if (!parsed) return rep(0, nullptr, usm_stat(6), decryption_errors_);
  }
  if (!parsed) return false;
  if (!ctx.empty()) return rep(level, u, {1, 3, 6, 1, 6, 3, 12, 1, 5, 0}, unknown_contexts_);
  if (d.type != P_GET && d.type != P_GETNEXT && d.type != P_SET && d.type != P_GETBULK) return false;
  size_t limit = std::min<size_t>(MAX_RESPONSE, (size_t) std::min<int64_t>(max_size, 65535));
  Result r = this->process_(d, false, limit - 250);  // room for the v3 header, user name and engine ID
  out = this->v3_message(msg_id, level, name, u, pdu_bytes(P_RESPONSE, d.reqid, r.err, r.idx, r.vbs), false);
  if (out.size() > limit)  // tooBig with no varbinds (RFC 3416 4.2.1), dropped if even that does not fit
    out = this->v3_message(msg_id, level, name, u, pdu_bytes(P_RESPONSE, d.reqid, E_TOOBIG, 0, {}), false);
  return !out.empty() && out.size() <= limit;
}

// ---- Notifications ----

Bytes Agent::trap_pdu_(uint32_t uptime_cs, const Oid &trap, const std::vector<std::pair<Oid, Entry>> &vars) {
  Entry up;
  up.type = TIMETICKS;
  up.num = uptime_cs;
  std::vector<Vb> vbs = {{{1, 3, 6, 1, 2, 1, 1, 3, 0}, encode_value(up)}, {{1, 3, 6, 1, 6, 3, 1, 1, 4, 1, 0}, ber_oid(trap)}};
  for (auto &v : vars) vbs.push_back({v.first, encode_value(v.second)});
  return pdu_bytes(P_TRAP2, trap_id_++, 0, 0, vbs);
}

Bytes Agent::trap_v2c(const std::string &comm, uint32_t uptime_cs, const Oid &trap,
                      const std::vector<std::pair<Oid, Entry>> &vars) {
  Bytes m = ber_int(T_INT, 1);
  cat(m, ber_tlv(T_STR, Bytes(comm.begin(), comm.end())));
  cat(m, trap_pdu_(uptime_cs, trap, vars));
  return ber_tlv(T_SEQ, m);
}

Bytes Agent::trap_v3(const User &u, uint32_t uptime_cs, const Oid &trap, const std::vector<std::pair<Oid, Entry>> &vars) {
  return v3_message(trap_id_++, u.level(), u.name, &u, trap_pdu_(uptime_cs, trap, vars), false);
}

static bool has_token(const std::string &s, const char *tok) {
  return (" " + s + " ").find(std::string(" ") + tok + " ") != std::string::npos;
}

std::vector<TrapEvent> status_traps(const std::string &prev, const std::string &cur) {
  static const struct {
    const char *tok;
    uint32_t on;
    const char *on_text;
    uint32_t off;
    const char *off_text;
  } T[] = {
      {"OB", 5, "UPS: On battery power.", 9, "UPS: Utility power restored."},
      {"LB", 7, "UPS: The battery power is too low to continue to support the load.", 11,
       "UPS: Returned from a low battery condition."},
      {"OVER", 2, "UPS: The load exceeds the UPS output capacity.", 33, "UPS: The overload condition has cleared."},
      {"RB", 17, "UPS: The battery needs to be replaced.", 36, "UPS: The battery has been replaced."},
      {"OFF", 12, "UPS: The UPS output is off.", 0, nullptr},
  };
  std::vector<TrapEvent> r;
  for (auto &t : T) {
    bool was = has_token(prev, t.tok), is = has_token(cur, t.tok);
    if (!was && is) r.push_back({t.on, t.on_text});
    if (was && !is && t.off) r.push_back({t.off, t.off_text});
  }
  return r;
}

}  // namespace snmp
