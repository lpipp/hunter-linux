// SPDX-License-Identifier: GPL-2.0
/* hunter: query performance mode and fans, switch lighting. */
#define _POSIX_C_SOURCE 200809L
#include <dirent.h>
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
	"       hunter watch -o FILE [-n SECONDS] [-s NAME -S N]\n"
	"\n"
	"mode prints 0 (balanced), 1 (performance) or 2 (quiet).\n"
	"fans prints the fan1 and fan2 speeds in RPM.\n"
	"watch rewrites FILE with the mode and fan speeds until killed.\n"
	"With -s and -S, processes whose name starts with NAME get\n"
	"SIGRTMIN+N whenever the content changes. SIGHUP forces a requery.\n"
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

/* Sleep for seconds. Signals are blocked except here: SIGHUP returns at once so
 * the caller can query again, while SIGTERM and SIGINT stop it. Returns 1 to stop.
 */
static int wait_tick(unsigned int seconds)
{
	struct timespec deadline, now, left;
	sigset_t set;
	int sig;

	sigemptyset(&set);
	sigaddset(&set, SIGHUP);
	sigaddset(&set, SIGTERM);
	sigaddset(&set, SIGINT);

	clock_gettime(CLOCK_MONOTONIC, &deadline);
	deadline.tv_sec += seconds;
	for (;;) {
		clock_gettime(CLOCK_MONOTONIC, &now);
		left.tv_sec = deadline.tv_sec - now.tv_sec;
		left.tv_nsec = deadline.tv_nsec - now.tv_nsec;
		if (left.tv_nsec < 0) {
			left.tv_sec--;
			left.tv_nsec += 1000000000L;
		}
		if (left.tv_sec < 0)
			return 0;

		/* A timeout or an unrelated interruption just means another tick. */
		sig = sigtimedwait(&set, NULL, &left);
		if (sig < 0) {
			if (errno == EAGAIN || errno == EINTR)
				return 0;
			return 1;
		}
		if (sig == SIGTERM || sig == SIGINT)
			return 1;
		if (sig == SIGHUP)
			return 0;
	}
}

/* Look for processes whose executable name starts with prefix and send them
 * signum. Only our own user can signal them, so no privileges are involved.
 */
static void notify(const char *prefix, int signum)
{
	DIR *dir = opendir("/proc");
	struct dirent *entry;

	if (!dir)
		return;
	while ((entry = readdir(dir))) {
		char path[64], comm[64];
		FILE *f;
		pid_t pid;

		if (entry->d_name[0] < '1' || entry->d_name[0] > '9')
			continue;
		pid = atoi(entry->d_name);
		if (pid == getpid())
			continue;
		snprintf(path, sizeof(path), "/proc/%d/comm", pid);
		f = fopen(path, "r");
		if (!f)
			continue;
		if (fgets(comm, sizeof(comm), f) && !strncmp(comm, prefix, strlen(prefix)))
			kill(pid, signum);
		fclose(f);
	}
	closedir(dir);
}

/* Query both nodes, then rewrite path via rename so readers never see a
 * partial file. A failed query keeps the previous values and sets "stale".
 * SIGHUP interrupts the wait and repeats the query at once.
 *
 * When the content changes and marker is set, every process whose name starts
 * with marker gets SIGRTMIN+signum, so Waybar can re-read the file.
 */
static int watch(const char *path, unsigned int interval, const char *marker,
		 int signum)
{
	char tmp[4096], buf[128], name[32], line[192], last[192] = "";
	int mode = -1, rpm[2] = { -1, -1 }, stale = 0;

	snprintf(tmp, sizeof(tmp), "%s.%ld", path, (long)getpid());
	for (;;) {
		FILE *f;

		stale = 0;
		if (read_node(SYSFS "hunter/mode", buf, sizeof(buf)) ||
		    (mode = read_mode(buf)) < 0)
			stale = 1;
		if (read_node(SYSFS "hunter/fans", buf, sizeof(buf)) ||
		    read_fans(buf, rpm))
			stale = 1;

		if (mode_name(mode))
			snprintf(name, sizeof(name), "\"%s\"", mode_name(mode));
		else
			snprintf(name, sizeof(name), "null");
		snprintf(line, sizeof(line),
			 "{\"mode\":%d,\"mode_name\":%s,\"fan1\":%d,\"fan2\":%d,"
			 "\"stale\":%s}\n", mode, name, rpm[0], rpm[1],
			 stale ? "true" : "false");

		f = fopen(tmp, "w");
		if (!f)
			return fail(path);
		fputs(line, f);
		fclose(f);
		if (rename(tmp, path))
			return fail(path);

		if (marker && signum && strcmp(last, line))
			notify(marker, SIGRTMIN + signum);
		snprintf(last, sizeof(last), "%s", line);

		if (wait_tick(interval))
			break;
	}
	return 0;
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
		const char *path = NULL, *marker = NULL;
		unsigned int interval = 2;
		int signum = 0, i;
		sigset_t set;

		/* Block before the first query so nothing races with the handler. */
		sigemptyset(&set);
		sigaddset(&set, SIGHUP);
		sigaddset(&set, SIGTERM);
		sigaddset(&set, SIGINT);
		sigprocmask(SIG_BLOCK, &set, NULL);

		for (i = 2; i < argc; i++) {
			char *num;

			if (i + 1 == argc)
				break;
			if (!strcmp(argv[i], "-o"))
				path = argv[++i];
			else if (!strcmp(argv[i], "-s"))
				marker = argv[++i];
			else if (!strcmp(argv[i], "-n")) {
				long n = strtol(argv[++i], &num, 10);

				if (*num || n < 1 || n > 3600)
					break;
				interval = n;
			} else if (!strcmp(argv[i], "-S")) {
				long n = strtol(argv[++i], &num, 10);

				if (*num || n < 1 || n > 30)
					break;
				signum = n;
			} else
				break;
		}
		if (i != argc || !path)
			return fputs(usage, stderr), 2;
		return watch(path, interval, marker, signum);
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
