/**
 * @file hardware/gpio.h — host stub of the Pico SDK GPIO API.
 * @brief No-op declarations so headers that call gpio_* compile on the host.
 *
 * The native test build has no silicon. `SensorHelpers.h` defines an inline
 * `gpioInitForRole( )` that calls the Pico SDK's `gpio_init`, `gpio_pull_up`,
 * `gpio_set_dir`, … — non-dependent names the compiler must resolve when it
 * parses the header, even though the sensor-metadata tests never call that
 * function. These stubs give it something to resolve. They do nothing, and no
 * host test should depend on their effects; the real symbols come from the
 * Pico SDK on the firmware build, whose include path never sees this file.
 */
#pragma once

#include <cstdint>

enum gpio_dir { GPIO_IN = 0, GPIO_OUT = 1 };

inline void gpio_init(uint32_t) { }
inline void gpio_pull_up(uint32_t) { }
inline void gpio_pull_down(uint32_t) { }
inline void gpio_set_dir(uint32_t, int) { }
inline void gpio_put(uint32_t, int) { }
