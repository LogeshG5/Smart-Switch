#include "time_sync.h"
#include "config.h"
#include <errno.h>
#include <stdlib.h>
#include <time.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h> // Include Zephyr's logging header
#include <zephyr/net/sntp.h>

// Register this file as a log module
LOG_MODULE_REGISTER(time_sync_module, CONFIG_LOG_DEFAULT_LEVEL);

// --- Configuration ---
#define SNTP_TIMEOUT_MS 5000  // Timeout for a single SNTP request
#define SYNC_RETRIES 3        // Number of retry attempts per server
#define RETRY_BACKOFF_MS 2000 // Time to wait between retries

// Define a list of NTP servers for fallback capability
static const char *sntp_servers[] = {
    "time.google.com", // Primary
    "pool.ntp.org",    // Secondary
};

int update_system_timezone(const char *tz_string) {
  // Set environmental timezone variable (e.g. "EST5EDT" or "IST-5:30")
  setenv("TZ", tz_string, 1);

  // Apply changes to POSIX time libraries
  tzset();

  LOG_INF("System timezone updated to: %s", tz_string);
  return 0;
}

// --- Public API ---

/**
 * @brief Attempts to synchronize the system time using SNTP with retries and
 * fallbacks.
 * @return 0 on success, or a negative error code if all attempts fail.
 */
int time_sync(void) {
  int ret = -1;

  // Loop through all configured NTP servers
  for (int i = 0; i < ARRAY_SIZE(sntp_servers); i++) {
    const char *server = sntp_servers[i];
    LOG_INF("Attempting time synchronization with %s...", server);

    // Retry the synchronization several times for the current server
    for (int j = 0; j < SYNC_RETRIES; j++) {
      struct sntp_time ts;
      ret = sntp_simple(server, SNTP_TIMEOUT_MS, &ts);

      if (ret == 0) {
        // SUCCESS: Time was received from the SNTP server
        struct timespec tp = {
            .tv_sec = ts.seconds,
            .tv_nsec = ((uint64_t)ts.fraction * 1000000000ULL) >> 32,
        };

        ret = clock_settime(CLOCK_REALTIME, &tp);
        if (ret != 0) {
          LOG_ERR("Failed to set system clock (error %d, errno %d)", ret,
                  errno);
          return ret; // This is a critical system error, no point in retrying
        }

        const app_config_t *cfg = config_get();

        update_system_timezone(cfg->timezone);

        LOG_INF("Time synchronized successfully via %s", server);
        return 0; // Exit function on first success
      }

      LOG_WRN("SNTP failed on attempt %d/%d with %s (error %d). Retrying in "
              "%dms...",
              j + 1, SYNC_RETRIES, server, ret, RETRY_BACKOFF_MS);
      k_msleep(RETRY_BACKOFF_MS);
    }
  }

  // If we exit the loops, all attempts on all servers have failed.
  LOG_ERR("All SNTP attempts failed. Unable to synchronize time.");
  return ret; // Return the last error code from sntp_simple
}

/**
 * @brief Checks if the system time appears to be valid (i.e., has been set).
 */
bool time_is_valid(void) {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);

  // A simple check to see if the time is after ~Nov 2023.
  // This indicates the clock has been set from its Unix epoch default of 0.
  return ts.tv_sec > 1700000000;
}

/**
 * @brief Prints the current system time to the console.
 */
void time_print(void) {
  struct timespec ts;
  struct tm tm;
  char buffer[64];

  clock_gettime(CLOCK_REALTIME, &ts);
  time_t now = ts.tv_sec;
  localtime_r(&now, &tm); // Use re-entrant version for thread safety

  strftime(buffer, sizeof(buffer), "%A %Y-%m-%d %H:%M:%S %Z", &tm);
  LOG_INF("Current time: %s", buffer);
}
