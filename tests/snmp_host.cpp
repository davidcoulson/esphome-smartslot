// SPDX-License-Identifier: GPL-2.0-or-later
// Host SNMP agent with a small demo table, for trying snmpwalk/snmpget without hardware: make snmp-serve [PORT=1161]
// [TRAPPORT=1162]. SNMPv3 users (engine ID 0x8000013e03020000000001), all with password "authpass123" and, where
// they have privacy, "privpass123": noauth, authsha, privmd5 (MD5+AES), privsha256, privsha512.
// With a trap port, sends coldStart and the traps for OL -> OB LB to 127.0.0.1 as v2c (community public) and as v3
// from privsha256, then serves.
#include <arpa/inet.h>
#include <cstdlib>
#include <unistd.h>

#include "snmp_core.h"

using namespace snmp;

int main(int argc, char **argv) {
  Agent a;
  auto add = [&](const char *s, Type t) {
    Oid o;
    parse_oid(s, o);
    return a.add(o, t);
  };
  a.set(add("1.3.6.1.2.1.1.1.0", STRING), std::string("host test agent"));
  Oid so;
  parse_oid("1.3.6.1.4.1.318.1.3.2", so);
  a.set(add("1.3.6.1.2.1.1.2.0", OBJECT_ID), so);
  a.set(add("1.3.6.1.2.1.1.3.0", TIMETICKS), (int64_t) 12345);
  a.set(add("1.3.6.1.4.1.318.1.1.1.1.1.1.0", STRING), std::string("Smart-UPS 750"));
  a.set(add("1.3.6.1.4.1.318.1.1.1.2.1.1.0", INTEGER), (int64_t) 2);
  a.set(add("1.3.6.1.4.1.318.1.1.1.2.2.1.0", GAUGE), (int64_t) 100);
  a.set(add("1.3.6.1.4.1.318.1.1.1.2.2.3.0", TIMETICKS), (int64_t) 3600 * 100);
  a.set(add("1.3.6.1.4.1.318.1.1.1.3.2.1.0", GAUGE), (int64_t) 230);
  a.set_engine({0x80, 0x00, 0x01, 0x3e, 0x03, 0x02, 0x00, 0x00, 0x00, 0x00, 0x01}, 1, 0x1234);
  a.add_user("noauth", AUTH_NONE, "", PRIV_NONE, "");
  a.add_user("authsha", AUTH_SHA, "authpass123", PRIV_NONE, "");
  a.add_user("privmd5", AUTH_MD5, "authpass123", PRIV_AES, "privpass123");
  a.add_user("privsha256", AUTH_SHA256, "authpass123", PRIV_AES, "privpass123");
  a.add_user("privsha512", AUTH_SHA512, "authpass123", PRIV_AES, "privpass123");
  int port = argc > 1 ? atoi(argv[1]) : 1161;
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (argc > 2) {
    int tfd = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in to = {};
    to.sin_family = AF_INET;
    to.sin_port = htons(atoi(argv[2]));
    to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    auto send = [&](const Oid &trap, const std::vector<std::pair<Oid, Entry>> &vars) {
      for (const Bytes &b : {a.trap_v2c("public", 100, trap, vars), a.trap_v3(*a.user("privsha256"), 100, trap, vars)})
        sendto(tfd, b.data(), b.size(), 0, (sockaddr *) &to, sizeof to);
    };
    send({1, 3, 6, 1, 6, 3, 1, 1, 5, 1}, {});
    for (auto &t : status_traps("OL", "OB LB")) {
      Entry e;
      e.type = STRING;
      e.str = t.text;
      send({1, 3, 6, 1, 4, 1, 318, 0, t.id}, {{{1, 3, 6, 1, 4, 1, 318, 2, 3, 3, 0}, e}});
    }
    close(tfd);
  }
  sockaddr_in sa = {};
  sa.sin_family = AF_INET;
  sa.sin_port = htons(port);
  sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (bind(fd, (sockaddr *) &sa, sizeof sa) < 0) return perror("bind"), 1;
  while (true) {
    uint8_t buf[1500];
    sockaddr_in from;
    socklen_t fl = sizeof from;
    int n = recvfrom(fd, buf, sizeof buf, 0, (sockaddr *) &from, &fl);
    Bytes out;
    if (n > 0 && a.handle(buf, n, out)) sendto(fd, out.data(), out.size(), 0, (sockaddr *) &from, fl);
  }
}
