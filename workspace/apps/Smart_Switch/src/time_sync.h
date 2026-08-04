#ifndef TIME_SYNC_H
#define TIME_SYNC_H

#include <stdbool.h>

/**
 * Synchronize system time using SNTP.
 *
 * Returns 0 on success.
 */
int time_sync(void);

/**
 * Print current local time.
 */
void time_print(void);

/**
 * Returns true if system time looks valid.
 */
bool time_is_valid(void);

#endif
