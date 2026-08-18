#include "scheduler.h"
#include "time_sync.h" // Needed for the time_is_valid() check
#include <stdbool.h>
#include <time.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(scheduler, CONFIG_LOG_DEFAULT_LEVEL);

// --- CENTRALIZED OPERATIONAL STATE VARIABLES ---
static bool override_active = false;
static bool override_state =
    false; // true = Block (Relay ON), false = Allow (Relay OFF)
static int64_t temp_allow_expiry_ms = 0; // Expiry uptime stamp in milliseconds

// Define a kernel semaphore to synchronize immediate web/button updates with
// the scheduler thread
static K_SEM_DEFINE(scheduler_sem, 0, 1);

/*------------------------------------------------------------------
 * Helpers
 *-----------------------------------------------------------------*/

/**
 * @brief Checks if a schedule is active for a given day of the week using a
 * bitmask.
 * @param schedule The schedule to check.
 * @param weekday The day of the week (0=Sun, 1=Mon, ..., 6=Sat), matching
 * tm_wday.
 * @return True if the day's corresponding bit is set in the mask, false
 * otherwise.
 */
static bool is_day_enabled(const schedule_t *schedule, int weekday) {
  if (weekday < 0 || weekday > 6) {
    LOG_ERR("Invalid weekday index received: %d", weekday);
    return false;
  }
  return (schedule->day_mask & (1 << weekday)) != 0;
}

/**
 * @brief Converts an hour and minute into a total number of minutes since
 * midnight.
 */
static int minutes_since_midnight(int hour, int minute) {
  return (hour * 60) + minute;
}

/*------------------------------------------------------------------
 * Core Scheduler Logic
 *-----------------------------------------------------------------*/

/**
 * @brief Determines if the device should be blocked (i.e., ON) based on a
 * single schedule.
 */
bool scheduler_should_block(const schedule_t *schedule) {
  if (schedule == NULL) {
    return false;
  }
  if (!schedule->enabled) {
    LOG_DBG("Schedule is disabled.");
    return false;
  }

  // CRITICAL: Do not run standard scheduling calculations if time is invalid
  if (!time_is_valid()) {
    LOG_WRN("Cannot evaluate schedule, system time is not valid/synchronized.");
    return false;
  }

  // Get current time information
  struct timespec ts;
  struct tm tm;
  clock_gettime(CLOCK_REALTIME, &ts);
  localtime_r(&ts.tv_sec, &tm);

  // Check if today is an active day for this schedule
  if (!is_day_enabled(schedule, tm.tm_wday)) {
    LOG_DBG("Day %d is not enabled for this schedule (mask: %u).", tm.tm_wday,
            schedule->day_mask);
    return false;
  }

  // Check if current time falls within the scheduled block (with overnight
  // support)
  const int current_minutes = minutes_since_midnight(tm.tm_hour, tm.tm_min);
  const int start_minutes =
      minutes_since_midnight(schedule->start_hour, schedule->start_minute);
  const int end_minutes =
      minutes_since_midnight(schedule->end_hour, schedule->end_minute);

  bool is_active;
  if (start_minutes <= end_minutes) {
    // --- Normal Case: Schedule does NOT cross midnight (e.g., 09:00 - 17:00)
    // ---
    is_active =
        (current_minutes >= start_minutes) && (current_minutes < end_minutes);
    LOG_DBG("Normal schedule check: start=%d, end=%d, current=%d. Active: %s",
            start_minutes, end_minutes, current_minutes,
            is_active ? "yes" : "no");
  } else {
    // --- Overnight Case: Schedule CROSSES midnight (e.g., 22:00 - 02:00) ---
    is_active =
        (current_minutes >= start_minutes) || (current_minutes < end_minutes);
    LOG_DBG(
        "Overnight schedule check: start=%d, end=%d, current=%d. Active: %s",
        start_minutes, end_minutes, current_minutes, is_active ? "yes" : "no");
  }

  return is_active;
}

/**
 * @brief Checks if any schedule in an array requires the device to be blocked.
 */
bool scheduler_should_block_any(const schedule_t *schedules, size_t count) {
  if (schedules == NULL || count == 0) {
    return false;
  }

  LOG_DBG("Evaluating %u schedule(s)...", (uint32_t)count);
  for (size_t i = 0; i < count; i++) {
    if (scheduler_should_block(&schedules[i])) {
      LOG_INF("Schedule %u is active. Device will be blocked.",
              (uint32_t)i + 1);
      return true; // If any schedule matches, we block.
    }
  }
  LOG_DBG("No active schedules found.");
  return false;
}

// ===================================================================================
// PUBLIC API FOR MANUAL OVERRIDES & TIME COUNTDOWNS
// ===================================================================================

/**
 * @brief Checks if a manual or temporary override is currently active.
 * @param state Pointer to write the active override state to (true = Block,
 * false = Allow).
 * @return true if an override is active, false if the system should follow
 * standard schedules.
 */
bool scheduler_get_override_state(bool *state) {
  if (temp_allow_expiry_ms > 0) {
    if (k_uptime_get() < temp_allow_expiry_ms) {
      *state = false; // Forced ALLOW state (Relay OFF / appliance running)
      return true;
    } else {
      // Temporary allow period has expired
      LOG_INF("Temporary allow countdown has expired. Returning control to "
              "schedule plans.");
      temp_allow_expiry_ms = 0;
      override_active = false;
    }
  }

  if (override_active) {
    *state = override_state;
    return true;
  }

  return false;
}

/**
 * @brief Sets a persistent manual override state.
 * @param block_state true to force block (Appliance OFF / Relay ON), false to
 * force allow (Appliance ON / Relay OFF).
 */
void scheduler_set_manual_override(bool block_state) {
  override_active = true;
  override_state = block_state;
  temp_allow_expiry_ms = 0; // Clear any active countdown timers

  LOG_INF("Manual override set to %s. Triggering immediate thread evaluation.",
          block_state ? "FORCED BLOCK" : "FORCED ALLOW");
  scheduler_trigger_evaluation();
}

/**
 * @brief Activates a temporary allow countdown.
 * @param duration_minutes Time duration in minutes to bypass schedules.
 */
void scheduler_set_temporary_allow(uint32_t duration_minutes) {
  temp_allow_expiry_ms =
      k_uptime_get() + ((int64_t)duration_minutes * 60 * 1000);
  override_active = true;
  override_state = false; // false = Allow (Relay OFF)

  LOG_INF("Temporary allow countdown activated for %u minute(s). Triggering "
          "evaluation.",
          duration_minutes);
  scheduler_trigger_evaluation();
}

/**
 * @brief Clears all active overrides and returns control to standard schedules.
 */
void scheduler_clear_override(void) {
  override_active = false;
  temp_allow_expiry_ms = 0;

  LOG_INF("Manual overrides cleared. System returning to automatic schedule.");
  scheduler_trigger_evaluation();
}

// ===================================================================================
// CENTRAL DECISION ENGINE (UNIFIED OVERRIDE & SCHEDULER ORCHESTRATION)
// ===================================================================================

/**
 * @brief Centralized state coordinator for the appliance.
 *        Resolves all competing variables (manual commands, timers, and offline
 * status) in one place.
 *
 * Evaluation Sequence (Strict Priority):
 * 1. Active Manual Overrides: Direct ON/OFF commands and active temporary allow
 * countdowns.
 * 2. Time Validity: If offline/unsynced, defaults to a safe ALLOWED state.
 * 3. Scheduled blocks: Evaluated only if there are no active overrides and time
 * is valid.
 *
 * @param schedules Pointer to the array of schedule definitions.
 * @param count Number of schedules in the array.
 * @return true if the device should block (Relay ON), false if allowed (Relay
 * OFF).
 */
bool scheduler_evaluate_state(const schedule_t *schedules, size_t count) {
  bool override_val = false;

  // 1. Highest Priority: Manual/Timed Overrides (Checked locally and natively!)
  if (scheduler_get_override_state(&override_val)) {
    LOG_DBG("Decision: Override active. Forcing block state to: %s",
            override_val ? "BLOCKED (Relay ON)" : "ALLOWED (Relay OFF)");
    return override_val;
  }

  // 2. Medium Priority: Network Time Sync Safety Gate
  if (!time_is_valid()) {
    LOG_WRN("Decision: System time invalid/unsynced. Defaulting to safe "
            "ALLOWED (OFF) state.");
    return false;
  }

  // 3. Normal Priority: Standard Scheduled Rules
  bool scheduled_block = scheduler_should_block_any(schedules, count);
  LOG_DBG("Decision: Following scheduled plans. Block state: %s",
          scheduled_block ? "BLOCKED (Relay ON)" : "ALLOWED (Relay OFF)");
  return scheduled_block;
}

/**
 * @brief Signals the central coordinator thread that a configuration state has
 * changed. Wakes the background loop up immediately to apply modifications.
 */
void scheduler_trigger_evaluation(void) { k_sem_give(&scheduler_sem); }

/**
 * @brief Thread sleep handler that waits for either the next periodic
 * evaluation window or an immediate state-change wake-up signal.
 */
void scheduler_wait_for_next_evaluation(k_timeout_t max_delay_seconds) {
  k_sem_take(&scheduler_sem, (max_delay_seconds));
}
