// SPDX-License-Identifier: GPL-2.0-or-later
// Host SNMP agent with a small demo table, for trying snmpwalk/snmpget without hardware: make snmp-serve [PORT=1161]
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
  int port = argc > 1 ? atoi(argv[1]) : 1161;
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
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
