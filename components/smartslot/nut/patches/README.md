Local changes to the vendored NUT driver, to carry over when it is re-synced:

- `apcmicrolink-maps.c`, `apcmicrolink.h`: BatterySystemError (`2:4.34`) bit 0 sets
  `ups.alarm` "Battery system - Disconnected", as apc_modbus does.
- `apcmicrolink.c` (`microlink_publish_descriptor_exports`): while that bit is set,
  `battery.charge` and `battery.runtime` are removed; the UPS reports 100 % and hours of
  runtime with no battery connected.
