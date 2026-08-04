#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h> // Include for bool type
#include <stdint.h>

#define CONFIG_VERSION 1

// Define maximum lengths for string fields
#define DEVICE_NAME_MAX_LEN 32
#define WIFI_SSID_MAX_LEN 32
#define WIFI_PASSWORD_MAX_LEN 64
#define TIMEZONE_MAX_LEN 32
#define MAX_SCHEDULES 8

/**
 * @brief Defines a single schedule entry.
 * Uses a bitmask for days of the week.
 */
typedef struct {
  bool enabled;
  uint8_t day_mask; // Bit 0:Sun, 1:Mon, ..., 6:Sat
  uint8_t start_hour;
  uint8_t start_minute;
  uint8_t end_hour;
  uint8_t end_minute;
} schedule_t;

/**
 * @brief Defines the entire device configuration structure.
 * This is what will be saved to and loaded from persistent storage.
 */
typedef struct {
  uint32_t version;

  // Device Settings
  char name[DEVICE_NAME_MAX_LEN];
  char wifi_ssid[WIFI_SSID_MAX_LEN];
  char wifi_password[WIFI_PASSWORD_MAX_LEN];
  char timezone[TIMEZONE_MAX_LEN];
  uint16_t max_on_time_minutes;

  // Schedule Settings
  uint8_t schedule_count;
  schedule_t schedules[MAX_SCHEDULES];
} app_config_t;

/**
 * @brief Loads the configuration from persistent storage (or sets defaults).
 * This should be called once at startup.
 */
void config_load(void);

/**
 * @brief Saves the current configuration to persistent storage.
 * @return 0 on success, or a negative error code on failure.
 */
int config_save(void);

/**
 * @brief Prints the entire current configuration to the console for debugging.
 */
void config_dump(void);

/**
 * @brief Resets the configuration to factory defaults.
 */
void config_factory_reset(void);

/**
 * @brief Gets a read-only pointer to the current configuration.
 * Use this for functions that only need to read settings.
 * @return const app_config_t* A pointer to the global config struct.
 */
const app_config_t *config_get(void);

/**
 * @brief Gets a mutable (writeable) pointer to the current configuration.
 * Use this for functions that need to modify settings before calling
 * config_save().
 * @return app_config_t* A pointer to the global config struct.
 */
app_config_t *config_get_editable(void);

#endif /* CONFIG_H */
