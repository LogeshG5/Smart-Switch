#include "relay.h"
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h> // Include Zephyr's logging header

// Register this file as a log module
LOG_MODULE_REGISTER(relay_driver, CONFIG_LOG_DEFAULT_LEVEL);

// --- Hardware and State Definition ---

// Get the relay specification from the devicetree
static const struct gpio_dt_spec relay =
    GPIO_DT_SPEC_GET(DT_ALIAS(tvrelay), gpios);

// The number of times to retry a GPIO set operation if it fails
#define RELAY_SET_RETRIES 3

// The internal state of the relay. This is the "source of truth" for the
// software.
static bool relay_state;

// --- Driver Implementation ---

/**
 * @brief Initializes the relay GPIO pin.
 * This is the most critical function. If it fails, the device cannot operate.
 */
int relay_init(void) {
  if (!gpio_is_ready_dt(&relay)) {
    // This is a fatal, non-recoverable error. The DT is misconfigured or the
    // hardware is missing.
    LOG_ERR("Relay GPIO device not ready: %s", relay.port->name);
    return -ENODEV;
  }

  int ret = gpio_pin_configure_dt(&relay, GPIO_OUTPUT_INACTIVE);
  if (ret != 0) {
    LOG_ERR("Failed to configure relay GPIO pin %d (error %d)", relay.pin, ret);
    return ret;
  }

  // Successfully configured. The relay is physically OFF.
  relay_state = false;
  LOG_INF("Relay initialized successfully, state: OFF");
  return 0;
}

/**
 * @brief Turns the relay ON with retry logic.
 * This is now an internal helper function.
 */
static int relay_on_internal(void) {
  int ret = -1;
  for (int i = 0; i < RELAY_SET_RETRIES; i++) {
    ret = gpio_pin_set_dt(&relay, 1);
    if (ret == 0) {
      // SUCCESS: Update state and exit loop
      relay_state = true;
      return 0;
    }
    LOG_WRN("Failed to set relay ON on attempt %d/%d (error %d). Retrying...",
            i + 1, RELAY_SET_RETRIES, ret);
    k_msleep(10); // Small delay before retrying
  }

  // If we exit the loop, all retries have failed.
  LOG_ERR("FATAL: Failed to set relay ON after %d attempts.",
          RELAY_SET_RETRIES);
  relay_state = false; // Failsafe: assume it's off if we can't control it.
  return ret;
}

/**
 * @brief Turns the relay OFF with retry logic.
 * This is now an internal helper function.
 */
static int relay_off_internal(void) {
  int ret = -1;
  for (int i = 0; i < RELAY_SET_RETRIES; i++) {
    ret = gpio_pin_set_dt(&relay, 0);
    if (ret == 0) {
      // SUCCESS: Update state and exit loop
      relay_state = false;
      return 0;
    }
    LOG_WRN("Failed to set relay OFF on attempt %d/%d (error %d). Retrying...",
            i + 1, RELAY_SET_RETRIES, ret);
    k_msleep(10); // Small delay before retrying
  }

  // If we exit the loop, all retries have failed.
  LOG_ERR("FATAL: Failed to set relay OFF after %d attempts.",
          RELAY_SET_RETRIES);
  // The actual state is unknown, but we must assume the worst-case (it may be
  // stuck on). The internal state is now out of sync with hardware.
  return ret;
}

/**
 * @brief Sets the relay to a desired state (ON or OFF).
 * This is the primary public function to control the relay.
 */
int relay_set(bool on) {
  // State-change guard: only perform an action if the requested state is
  // different from the current state.
  if (on == relay_state) {
    return 0; // Already in the desired state, do nothing.
  }

  LOG_INF("Relay state changing to: %s", on ? "ON" : "OFF");

  if (on) {
    return relay_on_internal();
  } else {
    return relay_off_internal();
  }
}

int relay_off() { return relay_set(false); }

int relay_on() { return relay_set(true); }

/**
 * @brief Returns the last known successful state of the relay.
 */
bool relay_is_on(void) { return relay_state; }
