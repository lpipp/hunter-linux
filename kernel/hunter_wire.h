/* SPDX-License-Identifier: GPL-2.0 */
#ifndef HUNTER_WIRE_H
#define HUNTER_WIRE_H
#ifdef __KERNEL__
#include <linux/errno.h>
#include <linux/string.h>
#else
#include <errno.h>
#include <string.h>
#endif
#include "hunter_protocol.h"

static inline int hunter_encode(unsigned int op, u8 input[64])
{
	memset(input, 0, 64);
	switch (op) {
	case HUNTER_GET_MODE:
		input[0] = 4; input[1] = 0x12; break;
	case HUNTER_GET_FAN0:
	case HUNTER_GET_FAN1:
		input[0] = 2; input[1] = 8;
		input[2] = op - HUNTER_GET_FAN0; break;
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}

/* Only the three documented firmware modes are valid CLI results. */
static inline int hunter_decode(unsigned int op, const u8 payload[256], int *value)
{
	if (payload[0])
		return -EREMOTEIO;
	switch (op) {
	case HUNTER_GET_MODE:
		if (payload[1] > 2)
			return -EPROTO;
		*value = payload[1]; break;
	case HUNTER_GET_FAN0:
	case HUNTER_GET_FAN1:
		*value = payload[1] | (payload[2] << 8); break;
	default: return -EOPNOTSUPP;
	}
	return 0;
}

/* Host-testable strict payload adapter. The ACPI wrapper validates types. */
static inline int hunter_adapt(unsigned int format, const u8 *reserved,
			      unsigned int rlen, const u8 *data,
			      unsigned int dlen, struct hunter_attempt *out)
{
	if (format == HUNTER_FORMAT_FLAT260) {
		if (!data || dlen != 260)
			return -EPROTO;
		reserved = data;
		data += 4;
	} else if (format == HUNTER_FORMAT_PACKAGE) {
		if (!reserved || rlen != 4 || !data || dlen != 256)
			return -EPROTO;
	} else {
		return -EPROTO;
	}
	memcpy(out->reserved, reserved, 4);
	memcpy(out->payload, data, 256);
	out->format = format;
	out->status = data[0];
	return 0;
}

/* Caller holds the common OEM transaction mutex across this entire function.
 * Raw callback owns/frees its ACPI allocation on every attempt.
 * Never retry a transport/format error. Hunter retries only status 1;
 * legacy callers keep their pre-existing nonzero-status retry policy.
 */
typedef int (*hunter_raw_fn)(void *, const u8 *, unsigned int,
			     struct hunter_attempt *);
static inline int hunter_transact(void *context, hunter_raw_fn raw,
		const u8 *input, unsigned int length, int legacy,
		struct hunter_request *result)
{
	unsigned int i;
	int err;
	for (i = 0; i < 2; i++) {
		struct hunter_attempt *attempt = &result->trace[i];
		attempt->status = 0xffffffffU;
		result->attempts++;
		err = raw(context, input, length, attempt);
		attempt->error = err;
		if (err)
			return err;
		if (!attempt->status)
			return 0;
		if (!legacy && attempt->status != 1)
			break;
	}
	return -EREMOTEIO;
}
#endif
