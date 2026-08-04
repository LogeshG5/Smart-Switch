#ifndef RELAY_H
#define RELAY_H

#include <stdbool.h>

/**
 * @brief Initialize the relay GPIO.
 *
 * @return 0 on success, negative error code otherwise.
 */
int relay_init(void);

/**
 * @brief Turn the relay ON.
 */
int relay_on(void);

/**
 * @brief Turn the relay OFF.
 */
int relay_off(void);

/**
 * @brief Set relay state.
 *
 * @param on true = ON, false = OFF
 */
int relay_set(bool on);

/**
 * @brief Get current relay state.
 *
 * @return true if relay is ON.
 */
bool relay_is_on(void);

#endif /* RELAY_H */
