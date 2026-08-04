#ifndef FACTORY_MODE_BUTTON_H
#define FACTORY_MODE_BUTTON_H

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>

#include "config.h"
// --- Button Configuration & Thread Definition ---
#define LONG_PRESS_DURATION_MS 5000
#define SW0_NODE DT_ALIAS(sw0)

void factory_reset_monitor_thread(void);
void button_monitor_thread_entry(void *p1, void *p2, void *p3);

#endif

