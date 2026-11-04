// SPDX-License-Identifier: GPL-2.0
#include <assert.h>
#include <stdio.h>
#include "../driver/hunter.h"

static int parse(unsigned int zone, const char *s, struct hunter_light *l)
{
	return hunter_light_parse(zone, s, strlen(s), l);
}

static void encode(unsigned int zone, const char *s, unsigned int n, u8 out[64])
{
	struct hunter_light l;

	assert(!parse(zone, s, &l));
	assert(n < hunter_light_packets(zone, &l));
	hunter_light_encode(zone, &l, n, out);
}

int main(void)
{
	struct hunter_light l;
	u8 p[64];

	/* Accepted and rejected inputs. */
	assert(!parse(HUNTER_LOGO, "off\n", &l) && l.off);
	assert(!parse(HUNTER_LOGO, "0 0 00aaFF\n", &l) && l.colours == 1);
	assert(!parse(HUNTER_LOGO, "2 5 FF0000,00FF00,0000FF", &l) && l.colours == 3);
	assert(!parse(HUNTER_KEYBOARD, "0 0 000001,000002,000003,000004", &l));
	assert(!parse(HUNTER_WIND, "3 1 000001,000002,000003,000004", &l));
	assert(parse(HUNTER_LOGO, "3 1 FF0000", &l) == -EINVAL);
	assert(parse(HUNTER_LOGO, "0 1 FF0000", &l) == -EINVAL);
	assert(parse(HUNTER_LOGO, "1 0 FF0000", &l) == -EINVAL);
	assert(parse(HUNTER_LOGO, "1 6 FF0000", &l) == -EINVAL);
	assert(parse(HUNTER_LOGO, "1 1 FF0000,00FF00", &l) == -EINVAL);
	assert(parse(HUNTER_LOGO, "1 1 FF00G0", &l) == -EINVAL);
	assert(parse(HUNTER_LOGO, "1 1 FF0000,", &l) == -EINVAL);
	assert(parse(HUNTER_KEYBOARD, "0 0 000001,000002", &l) == -EINVAL);
	assert(parse(HUNTER_KEYBOARD, "8 1 000001", &l) == -EINVAL);
	assert(parse(HUNTER_WIND, "3 1 000001", &l) == -EINVAL);
	assert(parse(HUNTER_WIND, "2 1 "
		     "000000,000000,000000,000000,000000,000000,000000,000000,"
		     "000000,000000,000000,000000,000000,000000,000000,000000,"
		     "000000", &l) == -EINVAL);
	assert(parse(HUNTER_LOGO, "on", &l) == -EINVAL);

	/* Solid colour: keyboard is sent per region. */
	encode(HUNTER_KEYBOARD, "0 0 000001,000002,000003,000004", 2, p);
	assert(p[0] == 6 && p[1] == 5 && p[2] == 0xb0 && p[3] == 0);
	assert(p[4] == 2 && p[5] == 2 && p[8] == 1 && p[11] == 3);
	encode(HUNTER_WIND, "0 0 112233", 0, p);
	assert(p[5] == 69 && p[6] == 0x01 && p[7] == 0x2c && p[9] == 0x11);

	/* Effects and periods. */
	encode(HUNTER_LOGO, "2 3 FF0000,00FF00", 0, p);
	assert(p[3] == 2 && (p[6] << 8 | p[7]) == 3840 && p[8] == 2 && p[13] == 0xff);
	encode(HUNTER_WIND, "2 1 FF0000", 0, p);
	assert((p[6] << 8 | p[7]) == 300);
	encode(HUNTER_KEYBOARD, "7 2 FF0000,00FF00", 0, p);
	assert(p[5] == 3 && (p[6] << 8 | p[7]) == 500 && p[8] == 2);

	/* Off packets. */
	encode(HUNTER_KEYBOARD, "off", 0, p);
	assert(p[3] == 2 && p[5] == 3 && p[6] == 3 && p[7] == 0xe8 && p[8] == 4);
	encode(HUNTER_WIND, "off", 3, p);
	assert(p[4] == 52 && p[5] == 69 && p[8] == 1 && p[9] == 0);

	puts("ok");
	return 0;
}
