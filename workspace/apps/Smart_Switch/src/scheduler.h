#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "config.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// typedef struct {
//
//   bool enabled;
//
//   /* Sunday = 0 ... Saturday = 6 */
//   bool weekdays[7];
//
//   uint8_t start_hour;
//   uint8_t start_minute;
//
//   uint8_t end_hour;
//   uint8_t end_minute;
//
// } schedule_t;

/*
 * Returns true if the current time falls inside this schedule.
 */
bool scheduler_should_block(const schedule_t *schedule);

/*
 * Returns true if ANY enabled schedule matches.
 */
bool scheduler_should_block_any(const schedule_t *schedules, size_t count);

#endif
