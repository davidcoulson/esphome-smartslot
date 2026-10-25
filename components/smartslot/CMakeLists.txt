# SPDX-License-Identifier: GPL-2.0-or-later
# ESPHome does not copy sub-directories of an external component, so the C sources (vendored NUT driver,
# shim, upsd) are built as an ESP-IDF component registered from __init__.py (add_idf_component, path=this dir).
# The C++ glue (smartslot.cpp, platforms) is compiled by ESPHome itself.
file(GLOB SMARTSLOT_C_SRCS nut/*.c shim/*.c upsd/*.c)
idf_component_register(
    SRCS ${SMARTSLOT_C_SRCS}
    INCLUDE_DIRS shim/include nut upsd
    REQUIRES driver esp_driver_uart esp_driver_gpio esp_timer esp_hw_support lwip freertos log)
# The vendored NUT sources are not ours to clean up; keep their warnings out of the build log.
set_source_files_properties(${SMARTSLOT_C_SRCS} PROPERTIES COMPILE_FLAGS "-Wno-error")
