/* SPDX-License-Identifier: GPL-2.0 */
#ifndef HUNTER_STATE_H
#define HUNTER_STATE_H
#include <linux/ktime.h>
#include <linux/pm.h>
#include "hunter_wire.h"

/* Lifecycle flags are protected by the existing common WMI mutex. */
struct hunter_state {
	bool matched;
	bool removing;
	bool legacy_led_registered;
	bool stopped;
	bool suspended;
};
#endif
