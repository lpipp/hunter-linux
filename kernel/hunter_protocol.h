/* SPDX-License-Identifier: GPL-2.0 */
#ifndef HUNTER_PROTOCOL_H
#define HUNTER_PROTOCOL_H
#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
#endif

enum hunter_operation {
	HUNTER_GET_MODE,
	HUNTER_GET_FAN0,
	HUNTER_GET_FAN1,
};
enum hunter_output_format {
	HUNTER_FORMAT_NONE,
	HUNTER_FORMAT_PACKAGE,
	HUNTER_FORMAT_FLAT260,
};
/* Internal records shared with the existing legacy driver; no user ABI. */
struct hunter_attempt {
	int error;
	u32 status;
	u32 acpi_status;
	unsigned int format;
	u64 duration_ns;
	u8 reserved[4];
	u8 payload[256];
};
struct hunter_request {
	unsigned int attempts;
	struct hunter_attempt trace[2];
};
#endif
