/* SPDX-License-Identifier: GPL-2.0 */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <time.h>
#include "hunter_wire.h"

struct mock {
	int statuses[2];
	int errors[2];
	int calls;
};
static int raw(void *data, const u8 *input, unsigned int length,
	       struct hunter_attempt *trace)
{
	struct mock *mock = data;
	int i = mock->calls++;
	assert(i < 2 && length == 64 && input[0] == 4);
	trace->status = mock->statuses[i];
	return mock->errors[i];
}

static void vectors(void)
{
	u8 input[64], payload[256] = {}, flat[260] = {}, reserved[4] = {};
	struct hunter_attempt output = {};
	unsigned int op, i;
	int value = -99;
	for (op = 0; op < 3; op++) {
		assert(!hunter_encode(op, input));
		for (i = 3; i < 64; i++) assert(input[i] == 0);
	}
	assert(!hunter_encode(HUNTER_GET_MODE, input));
	assert(input[0] == 4 && input[1] == 0x12 && !input[2]);
	assert(!hunter_encode(HUNTER_GET_FAN0, input));
	assert(input[0] == 2 && input[1] == 8 && !input[2]);
	assert(!hunter_encode(HUNTER_GET_FAN1, input) && input[2] == 1);
	/* There is no SET or temperature opcode in this interface. */
	assert(hunter_encode(3, input) == -EOPNOTSUPP);
	assert(hunter_encode(5, input) == -EOPNOTSUPP);
	payload[1] = 0x34; payload[2] = 0x12;
	assert(!hunter_decode(HUNTER_GET_FAN0, payload, &value) && value == 4660);
	payload[1] = 0xff; payload[2] = 0xff;
	assert(!hunter_decode(HUNTER_GET_FAN1, payload, &value) && value == 65535);
	for (i = 0; i < 3; i++) {
		payload[1] = i;
		assert(!hunter_decode(HUNTER_GET_MODE, payload, &value) && value == (int)i);
	}
	payload[1] = 250;
	assert(hunter_decode(HUNTER_GET_MODE, payload, &value) == -EPROTO);
	payload[0] = 1; value = 123;
	assert(hunter_decode(HUNTER_GET_FAN0, payload, &value) == -EREMOTEIO && value == 123);
	assert(!hunter_adapt(HUNTER_FORMAT_PACKAGE, reserved, 4, payload, 256, &output));
	assert(output.status == 1 && !memcmp(output.payload, payload, 256));
	assert(!hunter_adapt(HUNTER_FORMAT_FLAT260, NULL, 0, flat, 260, &output));
	assert(hunter_adapt(HUNTER_FORMAT_PACKAGE, reserved, 3, payload, 256, &output) == -EPROTO);
	assert(hunter_adapt(HUNTER_FORMAT_PACKAGE, reserved, 4, payload, 255, &output) == -EPROTO);
	assert(hunter_adapt(HUNTER_FORMAT_PACKAGE, NULL, 4, payload, 256, &output) == -EPROTO);
	assert(hunter_adapt(HUNTER_FORMAT_FLAT260, NULL, 0, payload, 256, &output) == -EPROTO);
	assert(hunter_adapt(HUNTER_FORMAT_NONE, reserved, 4, payload, 256, &output) == -EPROTO);
}

static void retries(void)
{
	struct hunter_request result;
	struct mock mock;
	u8 input[64];
	assert(!hunter_encode(HUNTER_GET_MODE, input));
#define RUN(a, b, e1, e2, expected, count) do { \
	memset(&result, 0, sizeof(result)); \
	mock = (struct mock){ .statuses = {a, b}, .errors = {e1, e2} }; \
	assert(hunter_transact(&mock, raw, input, 64, 0, &result) == expected); \
	assert(mock.calls == count && result.attempts == count); \
} while (0)
	RUN(0, 1, 0, 0, 0, 1);
	RUN(1, 0, 0, 0, 0, 2);
	RUN(1, 1, 0, 0, -EREMOTEIO, 2);
	RUN(7, 0, 0, 0, -EREMOTEIO, 1);
	RUN(1, 0, -EIO, 0, -EIO, 1);
	RUN(1, 0, -EPROTO, 0, -EPROTO, 1);
	RUN(1, 0, 0, -EIO, -EIO, 2);
	/* Legacy preserves nonzero retry behavior, still bounded. */
	memset(&result, 0, sizeof(result));
	mock = (struct mock){ .statuses = {7, 0} };
	assert(!hunter_transact(&mock, raw, input, 64, 1, &result) && mock.calls == 2);
#undef RUN
}

static pthread_mutex_t common = PTHREAD_MUTEX_INITIALIZER;
static pthread_barrier_t barrier;
static int log_items[4], log_count;
struct concurrent { int id; int call; };
static int concurrent_raw(void *data, const u8 *input, unsigned int length,
			  struct hunter_attempt *trace)
{
	struct concurrent *c = data;
	struct timespec delay = { .tv_nsec = 1000000 };
	(void)input; (void)length;
	log_items[log_count++] = c->id;
	trace->status = c->call++ ? 0 : 1;
	nanosleep(&delay, NULL);
	return 0;
}
static void *run_thread(void *data)
{
	struct hunter_request result = {};
	u8 input[64] = {};
	pthread_barrier_wait(&barrier);
	pthread_mutex_lock(&common);
	assert(!hunter_transact(data, concurrent_raw, input, 64, 0, &result));
	pthread_mutex_unlock(&common);
	return NULL;
}
static void concurrent_test(void)
{
	pthread_t a, b;
	struct concurrent ca = {1, 0}, cb = {2, 0};
	pthread_barrier_init(&barrier, NULL, 2);
	pthread_create(&a, NULL, run_thread, &ca);
	pthread_create(&b, NULL, run_thread, &cb);
	pthread_join(a, NULL); pthread_join(b, NULL);
	assert(log_count == 4 && log_items[0] == log_items[1] &&
	       log_items[2] == log_items[3] && log_items[0] != log_items[2]);
	pthread_barrier_destroy(&barrier);
}
int main(void)
{
	vectors(); retries(); concurrent_test();
	puts("C read-only codec, output format, retry and transaction checks passed");
	return 0;
}
