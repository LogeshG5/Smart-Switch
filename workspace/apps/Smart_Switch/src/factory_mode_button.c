#include "factory_mode_button.h"

LOG_MODULE_REGISTER(factory_mode_button, CONFIG_LOG_DEFAULT_LEVEL);

static const struct gpio_dt_spec button =
    GPIO_DT_SPEC_GET_OR(SW0_NODE, gpios, {0});
K_THREAD_STACK_DEFINE(button_stack_area, 1024);
struct k_thread button_thread_data;

void factory_reset_monitor_thread(void) {
  if (button.port) {
    k_thread_create(&button_thread_data, button_stack_area,
                    K_THREAD_STACK_SIZEOF(button_stack_area),
                    button_monitor_thread_entry, NULL, NULL, NULL, 5, 0,
                    K_NO_WAIT);
    LOG_INF("Button monitor thread started.");
  } else {
    LOG_WRN("No boot button alias found; long-press reset is disabled.");
  }
}

/**

 * @brief This thread runs in the background to detect a long press on the boot
 * button.
 */
void button_monitor_thread_entry(void *p1, void *p2, void *p3) {
  if (!device_is_ready(button.port)) {
    LOG_ERR("Button device is not ready.");
    return;
  }
  if (gpio_pin_configure_dt(&button, GPIO_INPUT) != 0) {
    LOG_ERR("Failed to configure button GPIO.");
    return;
  }

  LOG_INF("Button monitor active. Hold button for %d seconds to enter AP mode.",
          LONG_PRESS_DURATION_MS / 1000);
  int64_t press_start_time = 0;
  bool is_pressed = false;

  while (1) {
    if (gpio_pin_get_dt(&button) > 0) { // Button is pressed
      if (!is_pressed) {
        press_start_time = k_uptime_get();
        is_pressed = true;
      } else {
        int64_t elapsed = k_uptime_get() - press_start_time;
        if (elapsed >= LONG_PRESS_DURATION_MS) {
          LOG_WRN("Long press detected! Clearing WiFi config and rebooting...");
          app_config_t *cfg = config_get_editable();
          memset(cfg->wifi_ssid, 0, sizeof(cfg->wifi_ssid));
          memset(cfg->wifi_password, 0, sizeof(cfg->wifi_password));
          config_save();
          k_sleep(K_MSEC(200));
          sys_reboot(SYS_REBOOT_COLD);
        }
      }
    } else { // Button is not pressed
      is_pressed = false;
    }
    k_msleep(100);
  }
}
