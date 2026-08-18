#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "config.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zephyr/sys/clock.h>

/*
 * Returns true if the current time falls inside this schedule.
 */
bool scheduler_should_block(const schedule_t *schedule);

/*
 * Returns true if ANY enabled schedule matches.
 */
bool scheduler_should_block_any(const schedule_t *schedules, size_t count);

void scheduler_set_manual_override(bool block_state);
void scheduler_set_temporary_allow(uint32_t duration_minutes);
void scheduler_clear_override(void);
void scheduler_trigger_evaluation(void);
bool scheduler_evaluate_state(const schedule_t *schedules, size_t count);
void scheduler_wait_for_next_evaluation(k_timeout_t max_delay_seconds);

#endif
