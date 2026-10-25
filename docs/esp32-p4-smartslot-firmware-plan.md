# ESP32-P4 SmartSlot firmware implementation plan (ESPHome external component)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** An ESPHome external component `smartslot` that runs NUT's `apcmicrolink` driver unmodified against the UPS serial port, serves a NUT `upsd` on TCP 3493, and exposes UPS values as ESPHome sensors, text sensors and buttons.

**Architecture:** ESPHome (ESP-IDF framework) owns Ethernet, OTA, logging, API and the YAML config. The component starts two FreeRTOS tasks from `setup()`: `nutdrv` (the vendored driver behind a ~35-function NUT shim) and `upsd` (select()-based line-protocol server over lwIP). The driver state table (`dstate`) is the only shared data and is mutex-protected; `upsd` and the ESPHome buttons hand INSTCMD/SET to the driver task through a queue because the driver is single-threaded. `update()` walks `dstate` and publishes to the configured entities. All shared C code sits on a 12-function OS layer (`os.h`) with an ESP-IDF implementation and a pthreads one, so the shim, protocol handler, server and driver task are tested with plain `gcc` + Unity on the host; the UART and Ethernet are tested on a real ESP32-P4 board against the SMT750 before any PCB exists.

**Tech Stack:** ESPHome 2026.10.0 (pip, in `~/.venvs/esphome`), ESP-IDF via PlatformIO as ESPHome installs it, C11 (shim, server, driver) + C++ (component glue), Unity (vendored, MIT), NUT master snapshot of 2026-10-25 (GPL-2), git.

**Spec:** `docs/esp32-p4-smartslot-design.md` (moved from `notes/esp32-p4-smartslot-design.md` in Task 1) plus `docs/esp32-p4-smartslot-esphome-option.md` (the ESPHome decision, YAML sketch and trade-offs). The hardware (KiCad) half is a separate plan, `docs/esp32-p4-smartslot-hardware-plan.md`, in the same repo under `hardware/`.

## Global Constraints

- Licence GPL-2.0-or-later; every new file carries an SPDX header. The vendored NUT files are byte-identical to the snapshot and verified by `SHA256SUMS`; any change is a patch file under `components/smartslot/nut/patches/` applied by a script, never hand-edited.
- NUT source: the files already downloaded to `/tmp/claude-1000/-home-dev-dev-claude-code-container/95eba882-4d0b-487f-9d71-2b58e96dac46/scratchpad/nut/` on 2026-10-25 (`apcmicrolink.c` 4323 lines, `apcmicrolink.h`, `apcmicrolink-maps.c/.h`, `apc_common.c/.h`); if that directory is gone, fetch the same six files from `https://raw.githubusercontent.com/networkupstools/nut/master/drivers/`. `NUT_VERSION` records the upstream commit hash (`git ls-remote https://github.com/networkupstools/nut master`).
- `WITH_USB` is never defined (that removes the HID fallback and `apcmicrolink-usb.h`).
- The NUT sources `#include "config.h"`, `"main.h"` etc. unqualified, so the shim headers that carry those NUT names live in the same directory as the vendored `.c` files (`components/smartslot/nut/`); the quoted include finds them without build flags. Shim-only headers (`nutshim.h`, `os.h`, `nutdrv.h`, `nutproto.h`, `upsd.h`) are `extern "C"`-guarded.
- Driver options: `cmdsrc` default `smartslot1`, `baudrate` default `9600`; UART 8N1; poll every `poll_interval` seconds (driver-owned, default 2).
- Microlink probe: 9600 baud, send `0xFD`, read for 1.5 s, success = ≥ 8 bytes and first byte ≠ `0x00`.
- UART_SEL (SmartSlot pin 11) default low; configurable (`uart_sel_level`); never floats.
- One NUT UPS named `ups`; at most 4 clients; no default user; reads never need login; `INSTCMD`, `SET VAR` and `LOGIN` need the YAML `nut.username`/`nut.password`, else `ERR ACCESS-DENIED`; `STARTTLS` → `ERR FEATURE-NOT-CONFIGURED`.
- No Wi-Fi, SNMP, TLS or mDNS in the component; OTA, web server, API and logger are ESPHome's and the user's YAML decides them.
- YAML schema (`smartslot:`): `tx_pin` (GPIO20), `rx_pin` (GPIO21), `uart_sel_pin` (GPIO53), `uart_sel_level` (0), `uart_num` (1), `cmdsrc` (`smartslot1`), `baud_rate` (9600), `update_interval` (10s), `nut: {port: 3493, username: "", password: ""}`. Ethernet is the stock `ethernet:` block with `type: IP101`, MDC 31, MDIO 52, power 51, `clk: {mode: CLK_EXT_IN, pin: GPIO50}`, `phy_addr: 1` (the pin map of the user's working `gps-ntp-p4` node). Task 8 verifies GPIO 47, 48, 53, 54 are free on the bench board and changes the example YAML if not.
- Entities: `sensor`/`text_sensor` platforms take `variable: <nut name>` (e.g. `battery.charge`); `button` takes `command: <nut instcmd>` (e.g. `beeper.toggle`). No fixed per-variable keys.
- Component status: `status_set_warning()` while `nutdrv_state() != NUTDRV_RUNNING || !dstate_is_dataok()`, cleared otherwise, so ESPHome's `status_led:` shows UPS health.
- Toolchain: `python3 -m venv ~/.venvs/esphome && ~/.venvs/esphome/bin/pip install esphome==2026.10.0`; `esphome compile example/smartslot-bench.yaml` (first run downloads the P4 toolchain, minutes); host tests `make -C tests` with system `gcc`.
- Every task that adds a `.c` under `shim/` or `upsd/`, or a `tests/test_*.c`, appends it to `SRCS` in `tests/Makefile` and adds the `RUN_TEST` lines to `tests/test_main.c`; the Files block does not repeat this.
- Commit as `David Coulson <23066302+davidcoulson@users.noreply.github.com>`, no attribution lines.

## Review Focus

1. A client line longer than the 512-byte buffer, or a line with no terminator, must not overflow or wedge the server: the connection is closed with `ERR INVALID-ARGUMENT`. Test in Task 6.
2. A variable value containing `"` or `\` (e.g. `ups.firmware` strings) must reach the client escaped the way NUT does, or `upsc` mis-parses the whole LIST. Test in Task 5.
3. The UPS unplugged mid-run: `upsd` must answer `ERR DATA-STALE` within one poll interval, not serve old numbers; the sensors stop publishing and the component status goes to warning. Test in Task 3 (dataok/datastale), Task 7 (stale wiring) and Task 8 (no publish while stale).
4. A fifth client must be refused without disturbing the four live ones. Test in Task 6.
5. `fatalx` from the driver (e.g. descriptor read fails after the probe passed) must mark data stale and reboot the card after 5 s, never hang with stale data served. Test in Task 3 (host: `fatalx` calls a replaceable `nutshim_fatal_hook`).

---

### Task 1: Repository, component skeleton, first ESPHome compile, host test runner

**Files:**
- Create: `/home/dev/dev/esphome-smartslot/` (git init, GitHub `davidcoulson/esphome-smartslot`, private until the user says otherwise)
- Create: `LICENSE` (GPL-2.0-or-later text), `README.md` (what, status, licence, the YAML sketch from the esphome-option doc), `.gitignore` (`.esphome/`, `tests/build/`, `*.o`, `__pycache__/`)
- Move from `/home/dev/dev/claude-code-container/notes/`: `esp32-p4-smartslot-design.md`, `esp32-p4-smartslot-esphome-option.md`, `esp32-p4-smartslot-hardware-plan.md`, `esp32-p4-smartslot-bom.md`, `esp32-p4-smartslot-bom-digikey.md` (if present), `smartslot-*.svg` → `docs/`; this plan → `docs/esp32-p4-smartslot-firmware-plan.md`. Fix the SVG path references in the design doc. Leave copies in the notes folder untouched (the user's container repo).
- Create: `components/smartslot/__init__.py`, `components/smartslot/smartslot.h`, `components/smartslot/smartslot.cpp`, `components/smartslot/shim/include/os.h`, `components/smartslot/shim/os_esp.c`
- Create: `example/smartslot-bench.yaml`, `example/secrets.yaml.example`
- Create: `tests/Makefile`, `tests/test_main.c`, `tests/os_posix.c`, `tests/unity/{unity.c,unity.h,unity_internals.h}` (Unity 2.6.x from ThrowTheSwitch, MIT, with its licence file)

**Interfaces:**
- Produces `os.h` (used by every later task):
```c
typedef struct os_mutex *os_mutex_t;  os_mutex_t os_mutex_create(void); void os_mutex_lock(os_mutex_t); void os_mutex_unlock(os_mutex_t);
typedef struct os_sem   *os_sem_t;    os_sem_t os_sem_create(void); void os_sem_give(os_sem_t); bool os_sem_take(os_sem_t, uint32_t timeout_ms);   /* false on timeout */
typedef struct os_queue *os_queue_t;  os_queue_t os_queue_create(size_t depth); bool os_queue_send(os_queue_t, void *item, uint32_t timeout_ms); bool os_queue_receive(os_queue_t, void **item, uint32_t timeout_ms);
int  os_task_start(const char *name, void (*fn)(void *), void *arg, size_t stack_bytes, int prio);   /* 0 on success */
void os_sleep_ms(uint32_t ms);  uint64_t os_now_ms(void);  void os_wdt_reset(void);  void os_restart(void) __attribute__((noreturn));
enum { OS_LOG_ERROR, OS_LOG_WARN, OS_LOG_INFO, OS_LOG_DEBUG };  void os_log(int level, const char *tag, const char *fmt, ...);
```
  `os_esp.c`: FreeRTOS mutex/semaphore/queue, `xTaskCreatePinnedToCore`, `esp_task_wdt_reset` (only if the task subscribed; `nutdrv` subscribes in Task 7), `esp_restart`, `esp_log_write` at the matching level. `os_posix.c`: pthreads, `sem_t`, a mutex+condvar ring queue, `nanosleep`, `clock_gettime(CLOCK_MONOTONIC)`, `exit(1)` for restart, `fprintf(stderr)` for log.
- Produces `__init__.py`: `CONFIG_SCHEMA` with the keys and defaults from Global Constraints (pins via `pins.gpio_output_pin_schema` / input for `rx_pin`, `nut` as a nested schema), `to_code` that creates `smartslot::SmartSlot` (a `PollingComponent`) and calls `set_pins(tx, rx, sel, sel_level, uart_num)`, `set_driver(cmdsrc, baud)`, `set_nut(port, user, pass)`. `smartslot.h` declares the class with those setters, `setup()`, `update()`, `dump_config()`, `get_setup_priority()` returning `setup_priority::AFTER_CONNECTION` (bodies in Task 8; in this task `setup()` only logs the version and calls one C function `os_now_ms()` to prove the C sources compile and link).
- Produces `example/smartslot-bench.yaml`: `esp32: {board: esp32-p4-evboard, variant: esp32p4, flash_size: 16MB}` with `engineering_sample: true` commented and a note to set it per `esptool chip_id`; `ethernet:` per Global Constraints; `external_components: - source: {type: local, path: ../components}`; `smartslot:` with the defaults written out and `cmdsrc: rj45` (bench cable); `api:`, `ota: {platform: esphome}`, `logger: {level: INFO}`, `web_server:`, `status_led: {pin: GPIO54}`; one sensor (`battery.charge`), one text_sensor (`ups.status`), one button (`beeper.toggle`) (these fail validation until Task 8; keep them commented until then).
- Produces `tests/Makefile`: `CFLAGS = -std=gnu11 -Wall -Wextra -g -I../components/smartslot/shim/include -I../components/smartslot/nut -I../components/smartslot/upsd -Iunity`; sources: `unity/unity.c os_posix.c test_main.c` plus the shim/upsd/test files later tasks add (they append to `SRCS`); target `run` (default) builds `build/tests` and runs it; `-lpthread`.

- [ ] **Step 1: Install ESPHome into a venv and check the version**

Run: `python3 -m venv ~/.venvs/esphome && ~/.venvs/esphome/bin/pip install -q esphome==2026.10.0 && ~/.venvs/esphome/bin/esphome version`
Expected: `Version: 2026.10.0`.

- [ ] **Step 2: Create the repo, licence, docs move, component skeleton, example YAML, test runner**

- [ ] **Step 3: Compile the example and run the empty host test suite**

Run: `cd /home/dev/dev/esphome-smartslot && ~/.venvs/esphome/bin/esphome compile example/smartslot-bench.yaml 2>&1 | tail -20`
Expected: `Successfully created esp32 image.` and, earlier in the log, a `Compiling .../src/esphome/components/smartslot/shim/os_esp.c.o` line (proves PlatformIO compiles the component's `.c` files; if it does not, add `cg.add_platformio_option("build_src_filter", ["+<*>"])` in `to_code` and re-run).
Run: `make -C tests`
Expected: `0 Tests 0 Failures 0 Ignored` then `OK`.

- [ ] **Step 4: Commit**

```bash
git add -A && git commit -m "Repo skeleton: smartslot component stub, OS layer, example YAML, host test runner"
```

### Task 2: Vendor the NUT driver and make it compile

**Files:**
- Create: `components/smartslot/nut/{apcmicrolink.c,apcmicrolink.h,apcmicrolink-maps.c,apcmicrolink-maps.h,apc_common.c,apc_common.h}` (copied verbatim), `components/smartslot/nut/NUT_VERSION`, `components/smartslot/nut/SHA256SUMS`, `components/smartslot/nut/patches/README.md` (one line: "no patches yet")
- Create, beside them in `components/smartslot/nut/`: `config.h`, `main.h`, `common.h`, `dstate.h`, `extstate.h`, `serial.h`, `timehead.h`, `nut_stdint.h`, `nut-pragmas-covered-switch-default.h`, `nut-pragmas-covered-switch-default-end.h` as declarations only (bodies come in Tasks 3 and 4). `config.h` defines `PACKAGE_VERSION "esphome-smartslot"`, `HAVE_STDINT_H 1`, `HAVE_SYS_TIME_H 1` and nothing that enables USB.
- Modify: `tests/Makefile` (nothing: the test build never compiles `nut/*.c`; it only uses the headers)

**Interfaces:**
**Interfaces:**
- Produces (copied from NUT headers, exact names): `const char *dstate_setinfo(const char *var, const char *fmt, ...)`, `const char *dstate_getinfo(const char *var)`, `void dstate_addcmd(const char *cmdname)`, `int dstate_setflags(const char *var, int flags)`, `int dstate_setaux(const char *var, long aux)`, `void dstate_dataok(void)`, `void dstate_datastale(void)`, `void status_init(void)`, `void status_set(const char *buf)`, `void status_commit(void)`, `void alarm_init(void)`, `void alarm_set(const char *buf)`, `void alarm_commit(void)`; `ST_FLAG_RW`, `ST_FLAG_STRING`, `ST_FLAG_NUMBER`, `ST_FLAG_IMMUTABLE` with NUT's values from `extstate.h`; `STAT_INSTCMD_HANDLED 0 / UNKNOWN 1 / INVALID 2 / FAILED 3`, `STAT_SET_HANDLED 0 / UNKNOWN 1 / INVALID 2 / FAILED 3`; `struct ups_handler { int (*setvar)(const char *, const char *); int (*instcmd)(const char *, const char *); }; extern struct ups_handler upsh;` `typedef int TYPE_FD; #define ERROR_FD (-1); #define VALID_FD(fd) ((fd) >= 0)`; `extern char *device_path; extern TYPE_FD upsfd, extrafd; extern int exit_flag; extern time_t poll_interval; extern int nut_debug_level; void set_exit_flag(int sig); #define EF_EXIT_FAILURE -1`; `void addvar(int vartype, const char *name, const char *desc)` with `VAR_FLAG 1, VAR_VALUE 2, VAR_SENSITIVE 4`, `char *getval(const char *var)`, `int testvar(const char *var)`; `void upsdebugx(int level, const char *fmt, ...)`, `void upslogx(int priority, const char *fmt, ...)`, `void upsdebug_hex(int level, const char *msg, const void *buf, size_t len)`, `void fatalx(int status, const char *fmt, ...) __attribute__((noreturn))`, `LOG_ERR 3, LOG_WARNING 4, LOG_NOTICE 5, LOG_INFO 6, LOG_DEBUG 7`; `typedef struct upsdrv_info_s { const char *name, *version, *authors; int status; struct upsdrv_info_s *subdrv_info[2]; } upsdrv_info_t; DRV_EXPERIMENTAL 0, DRV_BETA 1, DRV_STABLE 2`; serial.h per Task 4. Driver entry points (defined by the vendored file, used by Task 7): `upsdrv_makevartable`, `upsdrv_initups`, `upsdrv_initinfo`, `upsdrv_updateinfo`, `upsdrv_shutdown`, `upsdrv_cleanup`.

- [ ] **Step 1: Copy the six files and write `NUT_VERSION`**

Run: `cd components/smartslot/nut && sha256sum apcmicrolink.c apcmicrolink.h apcmicrolink-maps.c apcmicrolink-maps.h apc_common.c apc_common.h > SHA256SUMS && sha256sum -c SHA256SUMS`
Expected: six `OK` lines; `NUT_VERSION` contains `networkupstools/nut master <40-hex commit> fetched 2026-10-25`.

- [ ] **Step 2: Write the header set so the ESPHome compile reaches the link step**

Any symbol the compiler still reports missing after this header set is added to the matching header (and to this task's Interfaces list in the commit message); the link errors are expected and are the input to Tasks 3 and 4.

Run: `~/.venvs/esphome/bin/esphome compile example/smartslot-bench.yaml 2>&1 | grep -E "error:|undefined reference" | sort -u | head -60`
Expected: no `error:` lines; only `undefined reference to` lines, all for names in the Interfaces block above or in Task 4.

- [ ] **Step 3: Commit**

```bash
git add components/smartslot/nut && git commit -m "Vendor NUT apcmicrolink (master 2026-10-25) and NUT shim headers"
```

### Task 3: nutshim dstate, status/alarm, logging, vars (host-tested)

**Files:**
- Create: `components/smartslot/shim/dstate.c`, `components/smartslot/shim/nutlog.c`, `components/smartslot/shim/vars.c`, `components/smartslot/shim/include/nutshim.h`
- Test: `tests/test_dstate.c`

**Interfaces:**
- Consumes: Task 2 headers.
- Produces (shim-only additions in `nutshim.h`, used by Tasks 5 and 7):
  - `void nutshim_init(void)` (creates the dstate mutex; idempotent)
  - `bool dstate_is_dataok(void)`
  - `typedef void (*dstate_var_fn)(const char *name, const char *value, int flags, long aux, void *ctx); void dstate_foreach(dstate_var_fn fn, void *ctx);` (mutex held across the walk, names in insertion order)
  - `typedef void (*dstate_cmd_fn)(const char *name, void *ctx); void dstate_foreach_cmd(dstate_cmd_fn fn, void *ctx);`
  - `int dstate_getflags(const char *var)` (−1 when absent), `long dstate_getaux(const char *var)` (−1 when absent)
  - `void nutshim_setvar_table(const char *const *kv, size_t n)` (flat `name,value,...` list that `getval`/`testvar` read; `addvar` records only the description)
  - `extern void (*nutshim_fatal_hook)(int status)` (default: log, `dstate_datastale()`, `os_sleep_ms(5000)`, `os_restart()` (which is `exit(status)` on the host))
  - Logging: `upsdebugx(level, …)` prints when `level <= nut_debug_level` via `os_log(OS_LOG_DEBUG, "nut", …)`; `upslogx(prio, …)` maps `LOG_ERR`→`OS_LOG_ERROR`, `LOG_WARNING`→`OS_LOG_WARN`, else `OS_LOG_INFO`; `upsdebug_hex` prints `msg: xx xx …` on one line.
  - Limits: 400 variables, 32 commands, name ≤ 64, value ≤ 256 (the SMX3000 descriptor in the design produces ~250 variables; values beyond the limit are truncated and logged once).

- [ ] **Step 1: Write the failing tests** (`TEST_CASE("…")` below is shorthand: each becomes `void test_<slug>(void)` plus a `RUN_TEST` line in `tests/test_main.c`)

```c
TEST_CASE("setinfo then getinfo round-trips and formats", "[dstate]") {
    nutshim_init();
    dstate_setinfo("ups.load", "%d", 42);
    TEST_ASSERT_EQUAL_STRING("42", dstate_getinfo("ups.load"));
    TEST_ASSERT_NULL(dstate_getinfo("nope"));
}
TEST_CASE("flags and aux default to 0 and are settable", "[dstate]") {
    dstate_setinfo("ups.delay.shutdown", "20");
    TEST_ASSERT_EQUAL(0, dstate_getflags("ups.delay.shutdown"));
    dstate_setflags("ups.delay.shutdown", ST_FLAG_RW | ST_FLAG_STRING);
    dstate_setaux("ups.delay.shutdown", 3);
    TEST_ASSERT_EQUAL(ST_FLAG_RW | ST_FLAG_STRING, dstate_getflags("ups.delay.shutdown"));
    TEST_ASSERT_EQUAL(3, dstate_getaux("ups.delay.shutdown"));
    TEST_ASSERT_EQUAL(-1, dstate_getflags("nope"));
}
TEST_CASE("status builder joins tokens and commits to ups.status", "[dstate]") {
    status_init(); status_set("OL"); status_set("CHRG"); status_commit();
    TEST_ASSERT_EQUAL_STRING("OL CHRG", dstate_getinfo("ups.status"));
    alarm_init(); alarm_commit();
    TEST_ASSERT_NULL(dstate_getinfo("ups.alarm"));          /* no alarms → variable removed, as NUT does */
    alarm_init(); alarm_set("Replace battery!"); alarm_commit();
    TEST_ASSERT_EQUAL_STRING("Replace battery!", dstate_getinfo("ups.alarm"));
}
TEST_CASE("dataok and datastale toggle", "[dstate]") {
    TEST_ASSERT_FALSE(dstate_is_dataok());
    dstate_dataok(); TEST_ASSERT_TRUE(dstate_is_dataok());
    dstate_datastale(); TEST_ASSERT_FALSE(dstate_is_dataok());
}
TEST_CASE("foreach walks vars in insertion order and cmds", "[dstate]") {
    /* fresh table: nutshim_reset_for_test(); add a, b; addcmd x → collect names into a buffer, expect "a,b," and "x," */
}
TEST_CASE("getval and testvar read the table; addvar does not add values", "[vars]") {
    static const char *const kv[] = {"cmdsrc", "smartslot1", "baudrate", "9600"};
    nutshim_setvar_table(kv, 4);
    addvar(VAR_VALUE, "hid_fallback", "x");
    TEST_ASSERT_EQUAL_STRING("smartslot1", getval("cmdsrc"));
    TEST_ASSERT_TRUE(testvar("baudrate"));
    TEST_ASSERT_FALSE(testvar("hid_fallback"));
    TEST_ASSERT_NULL(getval("hid_fallback"));
}
TEST_CASE("fatalx marks data stale and calls the hook", "[log]") {
    /* set nutshim_fatal_hook to a function that records status and longjmps back; call fatalx(EF_EXIT_FAILURE, "boom"); expect dataok false and status recorded */
}
```

- [ ] **Step 2: Run to verify they fail**

Run: `make -C tests 2>&1 | tail -5`
Expected: link fails with `undefined reference to 'dstate_setinfo'` (and friends).

- [ ] **Step 3: Implement `dstate.c`, `nutlog.c`, `vars.c`**

Fixed-size arrays (no malloc after init), one `os_mutex_t`; `dstate_setinfo` with `vsnprintf` into a 256-byte stack buffer; `status_commit` writes the joined buffer to `ups.status` (NUT behaviour: `status_init` clears, `status_set` appends with a space); `alarm_commit` with an empty buffer deletes `ups.alarm` (add a static `dstate_delinfo(const char*)` for that). Add `void nutshim_reset_for_test(void)` compiled only when `ESP_PLATFORM` is not defined.

- [ ] **Step 4: Run tests**

Run: `make -C tests`
Expected: `7 Tests 0 Failures 0 Ignored` / `OK`.

- [ ] **Step 5: Commit**

```bash
git add components/smartslot/shim tests && git commit -m "nutshim: dstate table, status/alarm builders, logging, driver vars"
```

### Task 4: nutshim serial layer on the ESP-IDF UART driver

**Files:**
- Create: `components/smartslot/shim/serial_esp.c` (whole file under `#ifdef ESP_PLATFORM`), `tests/serial_host.c` (a loopback pair so Tasks 5–7 host tests can link)
- Modify: `components/smartslot/nut/serial.h`, `components/smartslot/shim/include/nutshim.h`, `tests/Makefile` (add `serial_host.c`, `test_serial.c`)

**Interfaces:**
- Consumes: `nutshim_serial_config(int uart_num, int tx_gpio, int rx_gpio)` is set by the component before the driver task starts (declared in `nutshim.h`, stored in statics; the host ignores it).
- Produces (NUT names): `TYPE_FD ser_open(const char *port)` (ignores `port`, `uart_driver_install(uart_num, 1024, 0, 0, NULL, 0)`, 8N1, `uart_set_pin(tx, rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE)`, returns `uart_num`), `int ser_close(TYPE_FD fd, const char *port)` (`uart_driver_delete`), `int ser_set_speed(TYPE_FD fd, const char *port, speed_t speed)` (maps `B1200`…`B115200` to integer baud via a switch; unknown → `fatalx`), `int ser_set_dtr(TYPE_FD fd, int state)` (returns 0, does nothing), `ssize_t ser_send_buf(TYPE_FD fd, const void *buf, size_t len)` (`uart_write_bytes` then `uart_wait_tx_done` 100 ms), `ssize_t ser_get_char(TYPE_FD fd, void *ch, time_t d_sec, useconds_t d_usec)` (`uart_read_bytes` of 1 byte with that timeout; returns 1, 0 on timeout), `int ser_flush_io(TYPE_FD fd)` (`uart_flush_input` + `uart_wait_tx_done`), `void ser_comm_fail(const char *fmt, ...)` (counts, logs every call at `LOG_WARNING`, calls `dstate_datastale()` after 3 consecutive failures), `void ser_comm_good(void)` (resets the counter, logs once on recovery). `speed_t` and the `B*` constants come from the shim's `serial.h` on target (`<termios.h>` is not available there) and from `<termios.h>` on the host.
- Also in `nutshim.h`: `bool microlink_probe(void)` implements the Global Constraints probe using `ser_open`/`ser_set_speed(B9600)`/`ser_send_buf`/`ser_get_char` and `ser_close`; `void uartsel_init(int gpio, int level)` drives the pin as push-pull output at `level` before anything touches the UART (host: records the level in `uartsel_host_level` for the test).
- `tests/serial_host.c` also exports `void serial_host_queue_rx(const uint8_t *buf, size_t n)` to preload the reply the probe will read.

- [ ] **Step 1: Write the host loopback tests (`tests/test_serial.c`)**

```c
TEST_CASE("serial loopback: send_buf bytes come back from get_char; timeout returns 0", "[serial]") {
    TYPE_FD fd = ser_open("loop");
    TEST_ASSERT_TRUE(VALID_FD(fd));
    TEST_ASSERT_EQUAL(3, ser_send_buf(fd, "\xfd\x00\x01", 3));
    unsigned char c; TEST_ASSERT_EQUAL(1, ser_get_char(fd, &c, 0, 1000)); TEST_ASSERT_EQUAL_HEX8(0xfd, c);
    ser_flush_io(fd);
    TEST_ASSERT_EQUAL(0, ser_get_char(fd, &c, 0, 1000));
    ser_close(fd, "loop");
}
TEST_CASE("microlink_probe: 8 bytes not starting 0x00 passes; 0x00-led fails; 7 bytes fails", "[serial]") { /* three cases via serial_host_queue_rx */ }
TEST_CASE("uartsel_init records the requested level", "[serial]") { uartsel_init(53, 1); TEST_ASSERT_EQUAL(1, uartsel_host_level); }
```

- [ ] **Step 2: Run to verify failure** — Run: `make -C tests` Expected: `undefined reference to 'ser_open'`.

- [ ] **Step 3: Implement `serial_esp.c`, `serial_host.c`, `microlink_probe`, `uartsel_init`**

Host version: a 1 KiB ring buffer; `ser_send_buf` appends to it; `ser_get_char` pops with a sleep-poll bounded by the timeout; `serial_host_queue_rx` appends. The probe's 1.5 s read window is a loop of `ser_get_char(fd, &c, 0, 100000)` until 1500 ms elapsed by `os_now_ms()`.

- [ ] **Step 4: Run tests and the target compile** — Run: `make -C tests` Expected: `9 Tests 0 Failures`. Then `~/.venvs/esphome/bin/esphome compile example/smartslot-bench.yaml 2>&1 | tail -3` Expected: `Successfully created esp32 image.` (the only remaining undefined symbols were serial; the firmware now links with the vendored driver).

- [ ] **Step 5: Commit** — `git add -A && git commit -m "nutshim: UART serial layer, Microlink probe, UART_SEL"`.

### Task 5: NUT line protocol handler (pure, host-tested)

**Files:**
- Create: `components/smartslot/upsd/nutproto.h`, `components/smartslot/upsd/nutproto.c`
- Test: `tests/test_nutproto.c`

**Interfaces:**
- Consumes: `dstate_getinfo`, `dstate_foreach`, `dstate_foreach_cmd`, `dstate_getflags`, `dstate_getaux`, `dstate_is_dataok` from Task 3.
- Produces:
```c
typedef struct {
    bool (*check_user)(const char *user, const char *pass);   /* false when no user configured */
    int  (*instcmd)(const char *cmd, const char *param);       /* STAT_INSTCMD_*; param may be NULL */
    int  (*setvar)(const char *var, const char *val);          /* STAT_SET_* */
    int  (*send)(void *ctx, const char *buf, size_t len);      /* 0 ok, -1 drop client */
    const char *version;                                       /* for VER */
    int  (*numlogins)(void);
} nutproto_hooks;
typedef struct { char user[33]; char pass[65]; bool logged_in; const nutproto_hooks *h; void *ctx; } nutproto_session;
void nutproto_session_init(nutproto_session *s, const nutproto_hooks *h, void *ctx);
int  nutproto_handle(nutproto_session *s, char *line);   /* line without CR/LF, modified in place; returns 0 = keep, 1 = close */
```
  Replies (exact text, each ending `\n`): `VER` → `<version>`; `NETVER` → `1.3`; `LIST UPS` → `BEGIN LIST UPS`, `UPS ups "<device.model or 'SmartSlot UPS'>"`, `END LIST UPS`; `LIST VAR ups` → `BEGIN LIST VAR ups`, one `VAR ups <name> "<value>"` per variable, `END LIST VAR ups`; `LIST RW ups` → same shape with `RW` for variables with `ST_FLAG_RW`; `LIST CMD ups` → `CMD ups <name>`; `LIST ENUM ups <var>` / `LIST RANGE ups <var>` → empty BEGIN/END pair; `GET VAR ups <name>` → `VAR ups <name> "<value>"`; `GET TYPE ups <name>` → `TYPE ups <name> RW STRING:<aux>` / `RW NUMBER` / `STRING:<aux>` / `NUMBER` by flags; `GET DESC ups <name>` → `DESC ups <name> "Description unavailable"`; `GET CMDDESC ups <cmd>` → `CMDDESC ups <cmd> "Description unavailable"`; `GET UPSDESC ups` → `UPSDESC ups "<device.model>"`; `GET NUMLOGINS ups` → `NUMLOGINS ups <n>`; `USERNAME <u>` / `PASSWORD <p>` → `OK`; `LOGIN ups` → `OK` and `logged_in = true` when `check_user` passes, else `ERR ACCESS-DENIED`; `LOGOUT` → `OK Goodbye` and return 1; `INSTCMD ups <cmd> [param]` → `OK` for HANDLED, `ERR CMD-NOT-SUPPORTED` for UNKNOWN, `ERR INVALID-ARGUMENT` for INVALID, `ERR INSTCMD-FAILED` for FAILED; `SET VAR ups <var> "<value>"` → `OK` / `ERR VAR-NOT-SUPPORTED` / `ERR INVALID-VALUE` / `ERR SET-FAILED`; `STARTTLS` → `ERR FEATURE-NOT-CONFIGURED`; any other first word → `ERR UNKNOWN-COMMAND`; wrong UPS name → `ERR UNKNOWN-UPS`; missing args → `ERR INVALID-ARGUMENT`; unknown variable → `ERR VAR-NOT-SUPPORTED`; `LIST VAR/RW`, `GET VAR`, `INSTCMD`, `SET` while `!dstate_is_dataok()` → `ERR DATA-STALE`. Values are quoted with `"` and `\` escaped by a backslash; a quoted `SET` value is unescaped the same way. Tokens split on spaces, honouring double quotes (NUT's `parseconf` rules: quotes may wrap any argument).

- [ ] **Step 1: Write the failing tests** (capture `send` into a string; fake hooks with a `fake_user`/`fake_pass` pair and recording `instcmd`/`setvar`)

```c
TEST_CASE("LIST VAR emits begin, vars, end with escaping", "[proto]") {
    reset_state(); dstate_setinfo("ups.model", "Smart-UPS 750"); dstate_setinfo("ups.firmware", "UPS 09.3 \"ID1008\"\\x"); dstate_dataok();
    handle("LIST VAR ups");
    TEST_ASSERT_EQUAL_STRING("BEGIN LIST VAR ups\nVAR ups ups.model \"Smart-UPS 750\"\nVAR ups ups.firmware \"UPS 09.3 \\\"ID1008\\\"\\\\x\"\nEND LIST VAR ups\n", out);
}
TEST_CASE("GET VAR, TYPE, DESC and unknown var", "[proto]") { /* RW STRING:8 for a var with ST_FLAG_RW|ST_FLAG_STRING aux 8; ERR VAR-NOT-SUPPORTED */ }
TEST_CASE("data stale answers ERR DATA-STALE but LIST UPS still works", "[proto]") { /* dstate_datastale(); LIST VAR ups → "ERR DATA-STALE\n"; LIST UPS → begin/UPS/end */ }
TEST_CASE("INSTCMD needs a configured, matching user", "[proto]") {
    /* no user: USERNAME a, PASSWORD b, INSTCMD ups beeper.toggle → ERR ACCESS-DENIED, instcmd not called
       user set: wrong password → ERR ACCESS-DENIED; right → OK and instcmd called with ("beeper.toggle", NULL)
       INSTCMD ups load.off.delay 30 → param "30" */
}
TEST_CASE("INSTCMD result codes map to NUT errors", "[proto]") { /* UNKNOWN→ERR CMD-NOT-SUPPORTED, INVALID→ERR INVALID-ARGUMENT, FAILED→ERR INSTCMD-FAILED */ }
TEST_CASE("SET VAR unquotes and maps results", "[proto]") { /* SET VAR ups ups.delay.shutdown "20" → setvar("ups.delay.shutdown","20"), OK */ }
TEST_CASE("LOGIN, LOGOUT, NUMLOGINS, STARTTLS, unknown, unknown ups", "[proto]") {
    /* LOGIN ups without user → ERR ACCESS-DENIED; with user → OK, logged_in; LOGOUT → "OK Goodbye\n" and returns 1;
       STARTTLS → ERR FEATURE-NOT-CONFIGURED; FOO → ERR UNKNOWN-COMMAND; GET VAR other x → ERR UNKNOWN-UPS; GET → ERR INVALID-ARGUMENT */
}
TEST_CASE("VER and NETVER", "[proto]") { /* VER → "<version>\n"; NETVER → "1.3\n" */ }
```

- [ ] **Step 2: Run to verify failure** — Expected: `undefined reference to 'nutproto_handle'`.
- [ ] **Step 3: Implement `nutproto.c`** — tokenizer into ≤ 8 argv with quote handling (one static function), then a `strcmp` dispatch on `argv[0]`; output through a small `reply(s, fmt, ...)` helper that `vsnprintf`s into a 320-byte buffer and calls `send`; `LIST VAR` streams one line per `dstate_foreach` callback.
- [ ] **Step 4: Run tests** — Expected: `17 Tests 0 Failures`.
- [ ] **Step 5: Commit** — `git commit -am "upsd: NUT line protocol handler with host tests"`.

### Task 6: upsd TCP server

**Files:**
- Create: `components/smartslot/upsd/upsd.h`, `components/smartslot/upsd/upsd.c`
- Test: `tests/test_upsd.c` (on the host the server runs under a pthread via `os_task_start` and the test connects with a real TCP socket to 127.0.0.1)

**Interfaces:**
- Consumes: `nutproto_*` (Task 5).
- Produces: `int upsd_start(const nutproto_hooks *hooks, uint16_t port)` (0 on success; `os_task_start("upsd", …, 6144, 5)`; `port` 3493 in production, 0 = ephemeral for tests, actual port readable via `uint16_t upsd_port(void)`); `int upsd_num_logins(void)` (count of sessions with `logged_in`, for the `numlogins` hook); `void upsd_stop(void)` (tests only).
  Server: one listening socket, `select()` over listener + up to `UPSD_MAX_CLIENTS 4` clients, per-client 512-byte line buffer; CR/LF or LF terminates a line; a full buffer without terminator → send `ERR INVALID-ARGUMENT\n` and close; a fifth connection is accepted and closed at once (no greeting); `nutproto_handle` returning 1 or `send` returning −1 closes the client; `SO_KEEPALIVE` on, no idle timeout.

- [ ] **Step 1: Write the failing tests**

```c
TEST_CASE("upsd answers NETVER over TCP", "[upsd]") { /* start on port 0, connect, send "NETVER\n", expect "1.3\n" */ }
TEST_CASE("upsd closes the fifth client and keeps the first four", "[upsd]") { /* 4 connects answer NETVER; 5th read returns 0 (EOF); first 4 still answer */ }
TEST_CASE("upsd rejects an overlong line", "[upsd]") { /* send 600 'A's with no newline → read "ERR INVALID-ARGUMENT\n" then EOF */ }
TEST_CASE("LOGOUT closes and NUMLOGINS counts logins", "[upsd]") { /* with fake user: USERNAME/PASSWORD/LOGIN ups → OK; GET NUMLOGINS ups → "NUMLOGINS ups 1"; LOGOUT → "OK Goodbye" then EOF; NUMLOGINS from another client → 0 */ }
```

- [ ] **Step 2: Run to verify failure** — Expected: `undefined reference to 'upsd_start'`.
- [ ] **Step 3: Implement `upsd.c`** — plain POSIX sockets (`lwip/sockets.h` under `#ifdef ESP_PLATFORM`, else `sys/socket.h`).
- [ ] **Step 4: Run tests** — Expected: `21 Tests 0 Failures`.
- [ ] **Step 5: Commit** — `git commit -am "upsd: select()-based TCP server, 4 clients, line framing"`.

### Task 7: Driver task and command queue

**Files:**
- Create: `components/smartslot/shim/include/nutdrv.h`, `components/smartslot/shim/nutdrv.c`
- Test: `tests/test_nutdrv.c` (links a stub `upsdrv_*` set from `test_apps/host/main/fake_upsdrv.c` instead of the vendored driver; the host app's CMake excludes `apcmicrolink`)

**Interfaces:**
- Consumes: Task 3 shim, Task 4 serial/probe/uartsel, `upsdrv_*` entry points, `upsh`.
- Produces:
```c
typedef struct { const char *cmdsrc; uint32_t baudrate; int uartsel_level; } nutdrv_config;
int  nutdrv_start(const nutdrv_config *cfg);                         /* 0 on success; os_task_start("nutdrv", …, 8192, 6) */
int  nutdrv_instcmd(const char *cmd, const char *param, uint32_t timeout_ms);   /* runs upsh.instcmd on the driver task; STAT_INSTCMD_FAILED on timeout or no driver */
int  nutdrv_setvar(const char *var, const char *val, uint32_t timeout_ms);      /* likewise via upsh.setvar; STAT_SET_FAILED */
typedef enum { NUTDRV_NO_UPS, NUTDRV_STARTING, NUTDRV_RUNNING } nutdrv_state_t;
nutdrv_state_t nutdrv_state(void);
```
  Task loop: `uartsel_init(cfg->uartsel_level)`; build the var table `{"cmdsrc", cfg->cmdsrc, "baudrate", "<baud>"}`; `upsdrv_makevartable()`; loop: while `!microlink_probe()` → state `NO_UPS`, `dstate_datastale()`, wait 30 s; then `device_path = "smartslot"`, `upsdrv_initups(); upsdrv_initinfo();` state `RUNNING`; then forever: `upsdrv_updateinfo();` drain the request queue (each request is `{type, cmd/var, param, result, done_semaphore}`); `os_wdt_reset()`; `os_sleep_ms(poll_interval * 1000)`. If `upsdrv_updateinfo` leaves data stale for 5 consecutive polls, `upsdrv_cleanup()`, `ser_close`, back to the probe loop (that is the unplug-and-replug path; `fatalx` is the reboot path).

- [ ] **Step 1: Write the failing tests** (fake driver: `upsdrv_initinfo` sets `ups.status OL`; `upsdrv_updateinfo` sets `dstate_dataok()` unless `fake_stale` is set; `instcmd` records and returns `STAT_INSTCMD_HANDLED` for `beeper.toggle`)

```c
TEST_CASE("nutdrv waits in NO_UPS until the probe passes, then runs", "[nutdrv]") { /* no queued reply → state NO_UPS after 200 ms and dataok false; queue 8 bytes 0x01.. for the probe → RUNNING within 3 s, ups.status "OL" */ }
TEST_CASE("nutdrv_instcmd runs on the driver task and returns its status", "[nutdrv]") { /* HANDLED for beeper.toggle, UNKNOWN for "nope"; recorded call came from the nutdrv task (compare xTaskGetCurrentTaskHandle in the fake) */ }
TEST_CASE("five stale polls return to NO_UPS", "[nutdrv]") { /* fake_stale = true; with poll_interval 0 expect NO_UPS within 2 s */ }
```
  (On linux `poll_interval` is forced to 0 by the fake so the test runs in seconds; the 30 s probe retry is `CONFIG`-overridable to 1 s for the host app.)

- [ ] **Step 2: Run to verify failure** — Expected: `undefined reference to 'nutdrv_start'`.
- [ ] **Step 3: Implement `nutdrv.c`** — one `os_queue_t` of 4 request pointers, each request carrying an `os_sem_t` the caller waits on.
- [ ] **Step 4: Run tests** — Expected: `24 Tests 0 Failures`.
- [ ] **Step 5: Commit** — `git commit -am "nutdrv: driver task, Microlink probe gate, INSTCMD/SET queue"`.

### Task 8: ESPHome component glue and entity platforms

**Files:**
- Modify: `components/smartslot/__init__.py`, `components/smartslot/smartslot.h`, `components/smartslot/smartslot.cpp`
- Create: `components/smartslot/sensor.py`, `components/smartslot/text_sensor.py`, `components/smartslot/button.py`, `components/smartslot/smartslot_button.h`
- Modify: `example/smartslot-bench.yaml` (uncomment the entities), `README.md` (YAML reference)
- Test: `tests/test_publish.c` (the publish decision is a pure C helper so it is host-tested)

**Interfaces:**
- Consumes: `nutshim_init`, `nutshim_serial_config`, `uartsel_init`, `nutdrv_start/instcmd/setvar/state`, `dstate_getinfo`, `dstate_is_dataok`, `upsd_start`, `upsd_num_logins`, `nutproto_hooks`.
- Produces (C, in `nutshim.h`, host-tested): `bool nutshim_value_for_publish(const char *var, char *buf, size_t len)` — returns false (publish nothing) when `nutdrv_state() != NUTDRV_RUNNING`, `!dstate_is_dataok()` or the variable is absent; else copies the value and returns true.
- Produces (C++): `class SmartSlot : public PollingComponent` with `set_pins/set_driver/set_nut` (Task 1), `register_sensor(sensor::Sensor *s, const std::string &var)`, `register_text_sensor(text_sensor::TextSensor *t, const std::string &var)`, `int instcmd(const std::string &cmd)` (→ `nutdrv_instcmd(cmd, NULL, 5000)`). `setup()`: `nutshim_init(); nutshim_serial_config(…); uartsel_init(sel_pin, sel_level); nutdrv_start(&cfg); upsd_start(&hooks_, nut_port_)` where `hooks_.check_user` compares against the YAML user/pass and returns false when the username is empty, `hooks_.instcmd/setvar` call `nutdrv_*` with 5000 ms, `hooks_.version = "esphome-smartslot upsd " SMARTSLOT_VERSION`, `hooks_.numlogins = upsd_num_logins`. `update()`: for each registered entity `nutshim_value_for_publish` → `publish_state(parse_number(value))` / `publish_state(value)`; `status_set_warning()` or `status_clear_warning()` per Global Constraints. `dump_config()` prints pins, cmdsrc, baud, NUT port, whether a user is set (never the password), driver state.
- `SmartSlotButton : button::Button, Parented<SmartSlot>` with `set_command(std::string)`; `press_action()` calls `parent_->instcmd(command_)` and logs `ESP_LOGW` when the result is not `STAT_INSTCMD_HANDLED`.
- Python: `sensor.py` schema `sensor.sensor_schema().extend({cv.GenerateID(CONF_SMARTSLOT_ID): cv.use_id(SmartSlot), cv.Required("variable"): cv.string})`, `to_code` → `cg.add(parent.register_sensor(var, config["variable"]))`; `text_sensor.py` likewise; `button.py` with `cv.Required("command")`.

- [ ] **Step 1: Write the failing host test (`tests/test_publish.c`)** — three cases: running + dataok + present → true with the value; datastale → false; absent → false. The fake driver from Task 7 drives the state.
- [ ] **Step 2: Run to verify failure** — Expected: `undefined reference to 'nutshim_value_for_publish'`.
- [ ] **Step 3: Implement `nutshim_value_for_publish`, the C++ component, the three platforms; uncomment the example entities**
- [ ] **Step 4: Verify the pins on the bench board** — the bench board is whichever ESP32-P4 board with an IP101 PHY the user hands over (a Waveshare ESP32-P4-ETH like `gps-ntp-p4`, or the Espressif ESP32-P4-Function-EV-Board). Read its pinout and confirm GPIO 47, 48, 53, 54 are on a header and unused by PHY, SD, LCD or camera; if not, change the example YAML (and this plan's Global Constraints) in the same commit.
- [ ] **Step 5: Host tests and compile** — Run: `make -C tests` Expected: `27 Tests 0 Failures`. Run: `~/.venvs/esphome/bin/esphome compile example/smartslot-bench.yaml 2>&1 | tail -3` Expected: `Successfully created esp32 image.`
- [ ] **Step 6: Commit** — `git add -A && git commit -m "smartslot component: setup wiring, sensor/text_sensor/button platforms"`.

### Task 9: Bench test on a real ESP32-P4 board against the SMT750

**Files:**
- Create: `docs/bench.md` (the checklist below with the observed results filled in)

- [ ] **Step 1: Flash** — Run: `~/.venvs/esphome/bin/esphome run example/smartslot-bench.yaml --device /dev/ttyACM0` (USB-serial-JTAG). Expected: boot log shows `[smartslot]` dump_config, `ethernet` gets an IP, `nutdrv` logs `probe failed` every 30 s while the cable is off.

- [ ] **Step 2: Wire and run the checklist**

Wiring: board UART1 TX/RX → MAX3232 → APC 940-0625A cable → UPS front RJ45; example YAML has `cmdsrc: rj45`. Then from the Unraid host (`upsc` from the NUT package or the container's `nut-client`):

Run: `upsc ups@<card-ip>` Expected: ≥ 100 lines including `ups.status: OL`, `battery.charge:`, `device.model: Smart-UPS 750`.
Run: `upsc -l <card-ip>` Expected: `ups`.
Run: `upscmd -l ups@<card-ip>` Expected: the list from `dstate_addcmd` (`beeper.toggle`, `load.off`, `shutdown.return`, …).
Run: `upscmd -u <user> -p <pass> ups@<card-ip> beeper.toggle` with `nut.username/password` set in the YAML Expected: `OK`; the UPS beeps/stops. Press the ESPHome `beeper.toggle` button from the web UI: same.
Run: `upsrw ups@<card-ip>` Expected: the RW list (e.g. `ups.delay.shutdown`).
Home Assistant: adopt the node from the ESPHome integration. Expected: `Battery charge` and `UPS status` entities appear and update within `update_interval`; no NUT integration needed.
Unplug the serial cable 10 s: `upsc` → `Error: Data stale`; status LED blinks; HA entities stop updating (last value stays); replug: data returns within 60 s.
Leave it 24 h: no reboot (ESPHome `uptime` sensor from `debug:`/`uptime` platform in the example), no memory growth (`debug:` component's free heap sensor flat).

- [ ] **Step 3: Record the results in `docs/bench.md` and commit**

```bash
git add -A && git commit -m "Bench results: ESP32-P4 board + SMT750 over the front RJ45"
```

### Task 10: Codex adversarial review and tag

- [ ] **Step 1:** Run `node ~/.claude/plugins/cache/openai-codex/codex/*/scripts/codex-companion.mjs adversarial-review --wait --base <first commit> "NUT protocol correctness, concurrency between upsd and the driver task, UART edge cases"` and fix real findings (each fix with a test where the finding is testable).
- [ ] **Step 2:** `git tag v0.1.0-bench` and push `main` and the tag to `origin`.

## Self-review notes

- Spec coverage: goals 1–3 → Tasks 2–9; goals 4–7 are hardware (other plan); the ESPHome decision (esphome-option doc) → Tasks 1 and 8; anti-goal "driver unmodified" → Task 2 SHA256SUMS; probe/UART_SEL constraints → Tasks 4 and 7; `upsd` command list → Task 5 (every command the spec names has a reply defined); behaviour (boot sequence, LED, watchdog) → Tasks 7 and 8 (status warning drives ESPHome's `status_led`); testing section → host tests Tasks 3–8, bench Task 9.
- Type consistency: `os_*` names from Task 1 are used in Tasks 3–7; `nutproto_hooks`, `nutproto_session`, `nutdrv_config`, `nutdrv_state_t`, `dstate_foreach`/`dstate_foreach_cmd`, `nutshim_serial_config`, `nutshim_value_for_publish` are used with the same names where they appear.
- Review Focus items 1–5 each have a test: 1 and 4 in Task 6, 2 in Task 5, 3 in Tasks 3/7/8, 5 in Task 3.
- Left open on purpose: exact `LIST VAR` ordering is insertion order (NUT uses a tree, alphabetical); `upsc` sorts client-side, Home Assistant does not care. Test counts (9/17/21/24/27) are targets, not contracts: adjust the Expected line if a task adds a test.
- Dropped against the ESP-IDF version of this plan: Kconfig, NVS config store, esp_console commands, own Ethernet bring-up, own LED loop. ESPHome and the YAML provide them.

---

## Deviations found while executing (2026-10-25)

- ESPHome 2026.10 builds with native ESP-IDF, not PlatformIO, and copies only top-level files of an external component. The C tree (`nut/`, `shim/`, `upsd/`) is therefore an IDF component registered from `__init__.py` with `add_idf_component(path=...)` plus `components/smartslot/CMakeLists.txt`; `build_src_filter` is not used. New `.c` files are picked up by the glob but need a CMake reconfigure (touch `CMakeLists.txt`).
- `nutshim_value_for_publish` is declared in `nutdrv.h`, not `nutshim.h` (it needs `nutdrv_state`).
- The vendored `apc_common.c` calls `timegm()`, which newlib lacks: `shim/timegm.c` supplies it (host-tested against libc).
- Task 7: `nutdrv_start` takes the UART_SEL pin and calls `uartsel_init` itself; the vendored `upsdrv_cleanup` closes the serial port, so `nutdrv` never calls `ser_close`.
- Codex review additions: `PRIMARY`/`MASTER`/`FSD` in the NUT handler (the single configured user is the primary; FSD latches `FSD` into `ups.status` until restart), and publishing copies values under the dstate mutex.
- Task 8 step 4 (verify GPIO 47, 48, 53, 54 on the bench board) is NOT done: it needs the board in hand.
