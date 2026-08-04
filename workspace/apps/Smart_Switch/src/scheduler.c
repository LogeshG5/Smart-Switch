#include "scheduler.h"
#include "time_sync.h" // Needed for the time_is_valid() check
#include <stdbool.h>
#include <time.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(scheduler, CONFIG_LOG_DEFAULT_LEVEL);

// --- Helpers ---

static bool is_day_enabled(const schedule_t *schedule, int weekday) {
  if (weekday < 0 || weekday > 6) {
    LOG_ERR("Invalid weekday index received: %d", weekday);
    return false;
  }
  return (schedule->day_mask & (1 << weekday)) != 0;
}

static int minutes_since_midnight(int hour, int minute) {
  return (hour * 60) + minute;
}

// --- Core Scheduler Logic ---

bool scheduler_should_block(const schedule_t *schedule) {
  // 1. Basic validation and safety checks
  if (schedule == NULL) {
    return false;
  }
  if (!schedule->enabled) {
    LOG_DBG("Schedule is disabled.");
    return false;
  }
  // CRITICAL: Do not run scheduler if system time is not synchronized.
  if (!time_is_valid()) {
    LOG_WRN("Cannot evaluate schedule, system time is not valid/synchronized.");
    return false;
  }

  // 2. Get current time information
  struct timespec ts;
  struct tm tm;
  clock_gettime(CLOCK_REALTIME, &ts);
  localtime_r(&ts.tv_sec, &tm);

  // 3. Check if the schedule is active for the current day of the week
  if (!is_day_enabled(schedule, tm.tm_wday)) {
    LOG_DBG("Day %d is not enabled for this schedule (mask: %u).", tm.tm_wday,
            schedule->day_mask);
    return false;
  }

  // 4. Check if the current time falls within the scheduled block (with
  // overnight support)
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
    // The block is active if the current time is after the start time OR before
    // the end time.
    is_active =
        (current_minutes >= start_minutes) || (current_minutes < end_minutes);
    LOG_DBG(
        "Overnight schedule check: start=%d, end=%d, current=%d. Active: %s",
        start_minutes, end_minutes, current_minutes, is_active ? "yes" : "no");
  }

  return is_active;
}

bool scheduler_should_block_any(const schedule_t *schedules, size_t count) {
  if (schedules == NULL || count == 0) {
    return false;
  }

  LOG_DBG("Evaluating %d schedule(s)...", count);
  for (size_t i = 0; i < count; i++) {
    if (scheduler_should_block(&schedules[i])) {
      LOG_INF("Schedule %d is active. Device will be blocked.", i + 1);
      return true; // If any schedule matches, we block.
    }
  }

  LOG_DBG("No active schedules found.");
  return false; // No schedules matched.
}
