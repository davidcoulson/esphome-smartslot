/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifdef ESP_PLATFORM
#include "driver/gpio.h"
#include "driver/uart.h"
#include "nutshim.h"
#include "serial.h"

static int s_uart = 1, s_tx = 47, s_rx = 48;

void nutshim_serial_config(int uart_num, int tx_gpio, int rx_gpio) {
    s_uart = uart_num;
    s_tx = tx_gpio;
    s_rx = rx_gpio;
}

void uartsel_init(int gpio, int level) {
    gpio_reset_pin((gpio_num_t) gpio);
    gpio_set_level((gpio_num_t) gpio, level); /* level first so the pin never glitches the other way */
    gpio_set_direction((gpio_num_t) gpio, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t) gpio, level);
}

TYPE_FD_SER ser_open(const char *port) {
    (void) port;
    const uart_config_t cfg = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    if (uart_driver_install(s_uart, 1024, 0, 0, NULL, 0) != ESP_OK) return ERROR_FD;
    if (uart_param_config(s_uart, &cfg) != ESP_OK ||
        uart_set_pin(s_uart, s_tx, s_rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) {
        uart_driver_delete(s_uart);
        return ERROR_FD;
    }
    return s_uart;
}

int ser_close(TYPE_FD_SER fd, const char *port) {
    (void) port;
    return uart_driver_delete(fd) == ESP_OK ? 0 : -1;
}

int ser_set_speed(TYPE_FD_SER fd, const char *port, speed_t speed) {
    (void) port;
    switch (speed) {
        case B1200: case B2400: case B4800: case B9600: case B19200: case B38400: case B57600: case B115200:
            return uart_set_baudrate(fd, (uint32_t) speed) == ESP_OK ? 0 : -1;
        default:
            fatalx(EF_EXIT_FAILURE, "unsupported serial speed %u", (unsigned) speed);
    }
}

int ser_set_dtr(TYPE_FD_SER fd, int state) { (void) fd; (void) state; return 0; }

ssize_t ser_send_buf(TYPE_FD_SER fd, const void *buf, size_t len) {
    int n = uart_write_bytes(fd, buf, len);
    uart_wait_tx_done(fd, pdMS_TO_TICKS(100));
    return n;
}

ssize_t ser_get_char(TYPE_FD_SER fd, void *ch, time_t d_sec, useconds_t d_usec) {
    uint32_t ms = (uint32_t) d_sec * 1000U + d_usec / 1000U;
    TickType_t t = pdMS_TO_TICKS(ms);
    if (t == 0 && (d_sec || d_usec)) t = 1;
    return uart_read_bytes(fd, ch, 1, t) == 1 ? 1 : 0;
}

int ser_flush_io(TYPE_FD_SER fd) {
    uart_wait_tx_done(fd, pdMS_TO_TICKS(100));
    return uart_flush_input(fd) == ESP_OK ? 0 : -1;
}
#endif
