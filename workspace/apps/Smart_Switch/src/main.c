#include "config.h"
#include "factory_mode_button.h"
#include "relay.h"
#include "scheduler.h"
#include "time_sync.h"
#include "web/http_server.h"
#include "wifi.h"
#include "wifi_manager.h"

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(app_main, CONFIG_LOG_DEFAULT_LEVEL);

// --- Forward Declarations ---
static int init_core_peripherals(void);
static void run_provisioning_mode(void);
static void run_station_mode(void);

// ===================================================================================
// Main Application
// ===================================================================================

int main(void) {
  LOG_INF("\n=====================================");
  LOG_INF("      TV Scheduler Starting");
  LOG_INF("=====================================");

  if (init_core_peripherals() != 0) {
    LOG_ERR("FATAL: Core peripheral initialization failed. Halting.");
    return -1;
  }

  factory_reset_monitor_thread();

  if (wifi_manager_start() != 0) {
    LOG_ERR("FATAL: WiFi manager failed to start. Halting.");
    return -1;
  }

  // Based on the network mode, enter the appropriate main loop.
  if (wifi_manager_is_ap_mode()) {
    run_provisioning_mode();
  } else {
    run_station_mode();
  }

  // This part of the code should be unreachable
  LOG_ERR("FATAL: Main application loop exited unexpectedly. Halting.");
  return -1;
}

// ===================================================================================
// Helper Functions & Threads
// ===================================================================================

/**
 * @brief Initializes essential, non-network-related hardware.
 * @return 0 on success, negative error code on failure.
 */
static int init_core_peripherals(void) {
  LOG_INF("Initializing core peripherals...");
  config_load();

  if (relay_init() != 0) {
    LOG_ERR("Relay initialization failed.");
    return -EIO;
  }
  if (relay_set(false) != 0) {
    LOG_ERR("Failed to set initial relay state to OFF.");
    return -EIO;
  }
  LOG_INF("Core peripherals initialized.");
  return 0;
}

/**
 * @brief The main application loop when in Access Point (AP) mode.
 * Serves the web page for configuration and does nothing else.
 */
static void run_provisioning_mode(void) {
  LOG_INF("Entering Provisioning (AP) Mode.");
  http_server_start_serving();

  while (1) {
    k_sleep(K_SECONDS(60));
    LOG_DBG("Provisioning mode heartbeat.");
  }
}

/**
 * @brief The main application loop when in Station (client) mode.
 * Synchronizes time and runs the scheduler.
 */
static void run_station_mode(void) {
  const app_config_t *cfg = config_get();

  LOG_INF("Entering Station (Client) Mode.");
  http_server_start_serving();

  // Robust Time Sync Loop: Keep trying until time is valid.
  while (!time_is_valid()) {
    LOG_WRN("System time is not yet valid. Attempting to synchronize...");
    if (time_sync() != 0) {
      LOG_ERR("Time synchronization failed. Retrying in 1 minute...");
      k_sleep(K_MINUTES(1));
    } else {
      LOG_INF("Time synchronization successful.");
    }
  }
  time_print(); // Print the valid time once.

  // Main Scheduler Loop
  LOG_INF("Starting main scheduler loop...");
  while (1) {
    bool should_block =
        scheduler_should_block_any(cfg->schedules, cfg->schedule_count);
    if (relay_set(should_block) == 0) {
      LOG_INF("Relay state set to: %s", should_block ? "BLOCKED" : "ALLOWED");
    } else {
      LOG_ERR("Failed to set relay state!");
    }
    k_sleep(K_MINUTES(1));
  }
}

