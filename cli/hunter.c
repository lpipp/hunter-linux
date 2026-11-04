// SPDX-License-Identifier: GPL-2.0
/* hunter: query performance mode and fans, switch lighting. */
#include <errno.h>
#include <stdio.h>
#include <string.h>

#define SYSFS "/sys/bus/platform/devices/huawei-wmi/"

static const char usage[] =
	"Usage: hunter mode\n"
	"       hunter fans\n"
	"       hunter light {logo|keyboard|wind} off\n"
	"       hunter light {logo|keyboard|wind} on\n"
	"       hunter light {logo|keyboard|wind} RRGGBB\n"
	"       hunter light {logo|keyboard|wind} EFFECT SPEED RRGGBB[,RRGGBB...]\n"
	"\n"
	"mode prints 0 (balanced), 1 (performance) or 2 (quiet).\n"
	"fans prints the fan1 and fan2 speeds in RPM.\n"
	"EFFECT is 0-2 for logo and 0-7 otherwise. SPEED is 0 for effect 0\n"
	"and 1-5 for the others. Palettes hold up to 16 colours.\n";

static int fail(const char *what)
{
	fprintf(stderr, "hunter: %s: %s\n", what, strerror(errno));
	return errno == EINVAL ? 2 : 1;
}

static int query(const char *node)
{
	char buf[64];
	size_t n;
	FILE *f = fopen(node, "r");

	if (!f)
		return fail(node);
	n = fread(buf, 1, sizeof(buf), f);
	if (ferror(f)) {
		fclose(f);
		return fail(node);
	}
	fclose(f);
	fwrite(buf, 1, n, stdout);
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
