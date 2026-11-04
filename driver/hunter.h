/* SPDX-License-Identifier: GPL-2.0 */
/* HONOR Hunter lighting packets, shared by the driver and the host tests. */
#ifndef HUNTER_H
#define HUNTER_H

#ifdef __KERNEL__
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#else
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
typedef uint8_t u8;
#endif

enum hunter_zone {
	HUNTER_LOGO = 0xa0,
	HUNTER_KEYBOARD = 0xb0,
	HUNTER_WIND = 0xd0,
};

struct hunter_light {
	bool off;
	u8 effect;
	u8 speed;
	u8 colours;
	u8 rgb[16][3];
};

/* Palette size per effect: '1' one, '4' four, 'b' one or four, 'a' 1-16. */
static inline const char *hunter_rules(unsigned int zone)
{
	switch (zone) {
	case HUNTER_LOGO:	return "11a";
	case HUNTER_KEYBOARD:	return "bba114aa";
	case HUNTER_WIND:	return "11a411aa";
	}
	return "";
}

static inline int hunter_hex(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -EINVAL;
}

/* Accepts "off" or "<effect> <speed> RRGGBB[,RRGGBB...]". */
static inline int hunter_light_parse(unsigned int zone, const char *buf,
				     size_t count, struct hunter_light *l)
{
	const char *rules = hunter_rules(zone);
	size_t pos, i;
	int x;

	memset(l, 0, sizeof(*l));
	if (count && buf[count - 1] == '\n')
		count--;
	if (count == 3 && !memcmp(buf, "off", 3)) {
		l->off = true;
		return 0;
	}
	if (count < 10 || buf[1] != ' ' || buf[3] != ' ')
		return -EINVAL;
	l->effect = buf[0] - '0';
	l->speed = buf[2] - '0';
	if (l->effect >= strlen(rules) || l->speed > 5 ||
	    (l->effect == 0) != (l->speed == 0))
		return -EINVAL;

	for (pos = 4;; pos += 7) {
		if (count - pos < 6 || l->colours == 16)
			return -EINVAL;
		for (i = 0; i < 6; i++) {
			x = hunter_hex(buf[pos + i]);
			if (x < 0)
				return -EINVAL;
			l->rgb[l->colours][i / 2] = l->rgb[l->colours][i / 2] << 4 | x;
		}
		l->colours++;
		if (pos + 6 == count)
			break;
		if (buf[pos + 6] != ',')
			return -EINVAL;
	}

	switch (rules[l->effect]) {
	case '1': return l->colours == 1 ? 0 : -EINVAL;
	case '4': return l->colours == 4 ? 0 : -EINVAL;
	case 'b': return l->colours == 1 || l->colours == 4 ? 0 : -EINVAL;
	}
	return 0;
}

static inline unsigned int hunter_light_period(unsigned int zone,
					       const struct hunter_light *l)
{
	static const unsigned short slow[] = { 0, 1000, 1950, 3840, 5700, 7600 };
	static const unsigned short cycle[] = { 0, 200, 400, 600, 1200, 2000 };
	static const unsigned short other[] = { 0, 300, 500, 1000, 1500, 2000 };
	unsigned int period;

	if (zone == HUNTER_LOGO || l->effect == 1)
		period = slow[l->speed];
	else if (l->effect == 2)
		period = cycle[l->speed];
	else
		period = other[l->speed];
	/* The wind zone rejects periods below 300 ms. */
	return zone == HUNTER_WIND && period < 300 ? 300 : period;
}

static inline unsigned int hunter_light_packets(unsigned int zone,
						const struct hunter_light *l)
{
	if (l->off)
		return zone == HUNTER_WIND ? 4 : 1;
	/* Keyboard effects 0, 1, 3 and 4 are sent once per keyboard region. */
	return zone == HUNTER_KEYBOARD && l->effect < 5 && l->effect != 2 ? 4 : 1;
}

static inline void hunter_light_encode(unsigned int zone,
				       const struct hunter_light *l,
				       unsigned int n, u8 out[64])
{
	static const u8 wind_off[4][2] = { { 0, 19 }, { 20, 35 }, { 36, 51 }, { 52, 69 } };
	unsigned int period;

	memset(out, 0, 64);
	out[0] = 6;
	out[1] = 5;
	out[2] = zone;

	/* Off packets are copied from the Windows software. */
	if (l->off) {
		out[8] = 1;
		if (zone == HUNTER_KEYBOARD) {
			out[3] = 2;
			out[5] = 3;
			out[6] = 0x03;
			out[7] = 0xe8;
			out[8] = 4;
		} else if (zone == HUNTER_WIND) {
			out[4] = wind_off[n][0];
			out[5] = wind_off[n][1];
			out[6] = 0x01;
			out[7] = 0x2c;
		}
		return;
	}

	period = hunter_light_period(zone, l);
	out[3] = l->effect;
	out[6] = period >> 8;
	out[7] = period & 0xff;
	if (hunter_light_packets(zone, l) == 4) {
		out[4] = n;
		out[5] = n;
		out[8] = 1;
		memcpy(out + 9, l->rgb[l->colours == 4 ? n : 0], 3);
		return;
	}
	out[5] = zone == HUNTER_WIND ? 69 : zone == HUNTER_KEYBOARD ? 3 : 0;
	out[8] = l->colours;
	memcpy(out + 9, l->rgb, 3 * l->colours);
}

#endif
