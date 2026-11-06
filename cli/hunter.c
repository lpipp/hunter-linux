// SPDX-License-Identifier: GPL-2.0
/* hunter: query performance mode and fans, switch lighting. */
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define SYSFS "/sys/bus/platform/devices/huawei-wmi/"

static const char usage[] =
	"Usage: hunter mode\n"
	"       hunter fans\n"
	"       hunter light {logo|keyboard|wind} off\n"
	"       hunter light {logo|keyboard|wind} on\n"
	"       hunter light {logo|keyboard|wind} RRGGBB\n"
	"       hunter light {logo|keyboard|wind} EFFECT SPEED RRGGBB[,RRGGBB...]\n"
	"       hunter watch -o FILE [-n SECONDS]\n"
	"\n"
	"mode prints 0 (balanced), 1 (performance) or 2 (quiet).\n"
	"fans prints the fan1 and fan2 speeds in RPM.\n"
	"watch rewrites FILE with the mode and fan speeds until killed.\n"
	"EFFECT is 0-2 for logo and 0-7 otherwise. SPEED is 0 for effect 0\n"
	"and 1-5 for the others. Palettes hold up to 16 colours.\n";

static int fail(const char *what)
{
	fprintf(stderr, "hunter: %s: %s\n", what, strerror(errno));
	return errno == EINVAL ? 2 : 1;
}

/* Read a node, replacing the trailing newline with a terminator. */
static int read_node(const char *node, char *buf, size_t len)
{
	FILE *f = fopen(node, "r");
	size_t n;

	if (!f)
		return -1;
	n = fread(buf, 1, len - 1, f);
	buf[n] = '\0';
	if (ferror(f)) {
		fclose(f);
		return -1;
	}
	fclose(f);
	if (n && buf[n - 1] == '\n')
		buf[n - 1] = '\0';
	return 0;
}

static int query(const char *node)
{
	char buf[64];

	if (read_node(node, buf, sizeof(buf)))
		return fail(node);
	puts(buf);
	return 0;
}

static int read_mode(const char *buf)
{
	char *end;
	long v = strtol(buf, &end, 10);

	return *end || v < 0 || v > 2 ? -1 : (int)v;
}

static int read_fans(const char *buf, int rpm[2])
{
	char *end;

	rpm[0] = strtol(buf, &end, 10);
	if (end == buf)
		return -1;
	rpm[1] = strtol(end, &end, 10);
	if (*end)
		return -1;
	return 0;
}

static const char *mode_name(int mode)
{
	static const char *const names[] = { "balanced", "performance", "quiet" };

	return mode >= 0 ? names[mode] : NULL;
}

/* Rewrite path atomically on every tick so readers never see a partial file.
 * A failed query keeps the previous values and only sets "stale".
 */
static int watch(const char *path, unsigned int interval)
{
	char tmp[4096], buf[128];
	int mode = -1, rpm[2] = { -1, -1 }, stale = 0;

	snprintf(tmp, sizeof(tmp), "%s.%ld", path, (long)getpid());
	for (;;) {
		const char *name;
		FILE *f;

		stale = 0;
		if (read_node(SYSFS "hunter/mode", buf, sizeof(buf)) ||
		    (mode = read_mode(buf)) < 0)
			stale = 1;
		if (read_node(SYSFS "hunter/fans", buf, sizeof(buf)) ||
		    read_fans(buf, rpm))
			stale = 1;

		name = mode_name(mode);
		if (name)
			snprintf(buf, sizeof(buf), "\"%s\"", name);
		else
			snprintf(buf, sizeof(buf), "null");

		f = fopen(tmp, "w");
		if (!f)
			return fail(path);
		fprintf(f, "{\"mode\":%d,\"mode_name\":%s,\"fan1\":%d,\"fan2\":%d,"
			"\"stale\":%s}\n", mode, buf, rpm[0], rpm[1],
			stale ? "true" : "false");
		fclose(f);
		if (rename(tmp, path))
			return fail(path);

		sleep(interval);
	}
}

static int light(const char *zone, int argc, char **argv)
{
	char node[128], value[160];
	FILE *f;

	if (argc == 1 && !strcmp(argv[0], "on"))
		snprintf(value, sizeof(value), "0 0 00AAFF");
	else if (argc == 1 && strcmp(argv[0], "off"))
		snprintf(value, sizeof(value), "0 0 %s", argv[0]);
	else if (argc == 1)
		snprintf(value, sizeof(value), "off");
	else if (argc == 3)
		snprintf(value, sizeof(value), "%s %s %s", argv[0], argv[1], argv[2]);
	else
		return fputs(usage, stderr), 2;

	snprintf(node, sizeof(node), SYSFS "hunter_light/%s", zone);
	f = fopen(node, "w");
	if (!f)
		return fail(node);
	/* Unbuffered, so the driver's error is reported by fputs itself. */
	setbuf(f, NULL);
	if (fputs(value, f) == EOF) {
		int ret = fail(node);

		fclose(f);
		return ret;
	}
	fclose(f);
	return 0;
}

int main(int argc, char **argv)
{
	if (argc >= 2 && !strcmp(argv[1], "watch")) {
		const char *path = NULL;
		unsigned int interval = 2;
		int i;

		for (i = 2; i < argc; i++) {
			char *num;

			if (i + 1 == argc)
				break;
			if (!strcmp(argv[i], "-o"))
				path = argv[++i];
			else if (!strcmp(argv[i], "-n")) {
				long n = strtol(argv[++i], &num, 10);

				if (*num || n < 1 || n > 3600)
					break;
				interval = n;
			} else
				break;
		}
		if (i != argc || !path)
			return fputs(usage, stderr), 2;
		signal(SIGTERM, SIG_DFL);
		return watch(path, interval);
	}
	if (argc == 2 && !strcmp(argv[1], "mode"))
		return query(SYSFS "hunter/mode");
	if (argc == 2 && !strcmp(argv[1], "fans"))
		return query(SYSFS "hunter/fans");
	if (argc >= 4 && !strcmp(argv[1], "light") &&
	    (!strcmp(argv[2], "logo") || !strcmp(argv[2], "keyboard") ||
	     !strcmp(argv[2], "wind")))
		return light(argv[2], argc - 3, argv + 3);
	if (argc == 2 && !strcmp(argv[1], "--help"))
		return fputs(usage, stdout), 0;

	fputs(usage, stderr);
	return 2;
}
