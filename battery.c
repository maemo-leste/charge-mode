/* -*- linux-c -*-
 *
 * Distribute under LGPL v2 or (at your option) later version.
 *
 * Copyright (C) 2017 Pavel Machek <pavel@ucw.cz>
 *
 * Thanks to Sam Lantinga and his SDL2.
 * Thanks to Marek Belisko.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <alloca.h>
#include <math.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdbool.h>
#include <dirent.h>
#include <unistd.h>


#include "battery.h"

#define max(a, b) \
  ({ __typeof__ (a) _a = (a); \
     __typeof__ (b) _b = (b);  \
     _a > _b ? _a : _b; })

#define min(a, b) \
  ({ __typeof__ (a) _a = (a); \
     __typeof__ (b) _b = (b);  \
     _a < _b ? _a : _b; })

static const char *sys_class_power_supply_path = "/sys/class/power_supply";

static int
open_power_file(const char *base, const char *node, const char *key)
{
	const size_t pathlen = strlen(base) + strlen(node) + strlen(key) + 3;
	char *path = (char *) alloca(pathlen);
	if (path == NULL) {
		return -1;  /* oh well. */
	}

	snprintf(path, pathlen, "%s/%s/%s", base, node, key);
	return open(path, O_RDONLY);
}

static bool
read_power_file(const char *base, const char *node, const char *key,
                char *buf, size_t buflen)
{
	ssize_t br = 0;
	const int fd = open_power_file(base, node, key);
	if (fd == -1) {
		return false;
	}
	br = read(fd, buf, buflen-1);
	close(fd);
	if (br < 0) {
		return false;
	}
	buf[br] = '\0';             /* null-terminate the string. */
	return true;
}

static bool
int_string(char *str, int *val)
{
	char *endptr = NULL;
	*val = (int) strtol(str, &endptr, 0);
	return ((*str != '\0') && (*endptr == '\0'));
}

/* calculate remaining fuel level (in %) of a LiIon battery assuming
 * a standard chemistry model
 *    The first reference found on the web seems to be a forum post
 *    by "SilverFox" from 04-16-2008. It appears to be attributed to Sanyo.
 *    http://www.candlepowerforums.com/vb/showthread.php?115871-Li-Ion-State-of-Charge-and-Voltage-Measurements#post2440539
 *
 * @mV: voltage measured outside the battery
 * @mA: current flowing out of the battery
 * @mOhm: assumed series resitance of the battery
 *
 * returns value between 0 and 100
 */
static inline int fuel_level_LiIon(int mV, int mA, int mOhm)
{
	int u;

	/* internal battery voltage is higher than measured when discharging */
	mV += (mOhm * mA) /1000;

	/* apply first part of formula */
	u = 3870000 - (14523 * (37835 - 10 * mV));

	/* use linear approx. below 3.756V => 50.00% assuming 3.3V => 0% */
	if (u < 0) {
		int a = (((mV - 3300)*1000) / 456) * 5000;
		return max(a / 100000, 0);
	}

	/* apply second part of formula **/
	int percent = min((int)(1966 + sqrt(u))/100, 100);
    if (percent < 0)
		percent = 0;
    else if (percent > 100)
		percent = 100;
	return percent;
}

double battery_estimate(struct battery_info *i)
{
	int mA = 100;

	if (!isnan(i->current))
		mA = i->current * 1000;

	return fuel_level_LiIon(i->voltage * 1000, mA, 150) / 100.;
}

bool
battery_fill_info(struct battery_info *i)
{
	const char *base = sys_class_power_supply_path;
	struct dirent *dent;
	DIR *dirp;

	dirp = opendir(base);
	if (!dirp) {
		return false;
	}

	/* assume we're just plugged in. */
	i->state = NO_BATTERY;
	i->seconds = NAN;
	i->fraction = NAN;
	i->voltage = NAN;
	i->current = NAN;
	i->temperature = NAN;
	i->source = UNKOWN;

	while ((dent = readdir(dirp)) != NULL) {
		const char *name = dent->d_name;
		bool choose = false;
		char str[64];
		enum battery_state st;
		int secs = -1;
		int pct = -1;
		int vlt = -1;
		int cur = -999999999;
		int temp = -999999999;
		enum power_state type = UNKOWN;

		if ((strcmp(name, ".") == 0) || (strcmp(name, "..") == 0)) {
			continue;  /* skip these, of course. */
		}

		if (!strcmp(name, "rx51-battery")) {
		  /* Nokia N900 has rx51-battery and bq27200-0; both have type=Battery,
		     and unfortunately both refer to same battery.
		  */
			continue;
		}

		if (!read_power_file(base, name, "type", str, sizeof (str))) {
			continue;  /* Don't know _what_ we're looking at. Give up on it. */
		}
		if (strcmp(str, "Battery\n") == 0)
			type = BATTERY;
		else if (strcmp(str, "USB\n") == 0 )
			type = USB;
		if (type == UNKOWN)
			continue;


		/* if the scope is "device," it might be something like a PS4
		   controller reporting its own battery, and not something that powers
		   the system. Most system batteries don't list a scope at all; we
		   assume it's a system battery if not specified. */
		if (read_power_file(base, name, "scope", str, sizeof (str))) {
			if (strcmp(str, "device\n") == 0) {
				continue;  /* skip external devices with their own batteries. */
			}
		}

		if(type == BATTERY) {
			/* some drivers don't offer this, so if it's not explicitly reported assume it's present. */
			if (read_power_file(base, name, "present", str, sizeof (str)) && (strcmp(str, "0\n") == 0)) {
				st = NO_BATTERY;
			} else if (!read_power_file(base, name, "status", str, sizeof (str))) {
				st = UNKNOWN;  /* uh oh */
			} else if (strcmp(str, "Charging\n") == 0) {
				st = CHARGING;
			} else if (strcmp(str, "Discharging\n") == 0) {
				st = ON_BATTERY;
			} else if ((strcmp(str, "Full\n") == 0) || (strcmp(str, "Not charging\n") == 0)) {
				st = FULL;
			} else {
				st = UNKNOWN;  /* uh oh */
			}
			if (read_power_file(base, name, "capacity", str, sizeof (str))) {
				pct = atoi(str);
				pct = (pct > 100) ? 100 : pct; /* clamp between 0%, 100% */
			}

			if (read_power_file(base, name, "voltage_now", str, sizeof (str))) {
				vlt = atoi(str);
			}

			if (read_power_file(base, name, "current_now", str, sizeof (str))) {
				cur = atoi(str);
			}

			if (read_power_file(base, name, "temp", str, sizeof (str))) {
				temp = atoi(str);
			}

			if (read_power_file(base, name, "time_to_empty_now", str, sizeof (str))) {
				secs = atoi(str);
				secs = (secs <= 0) ? -1 : secs;  /* 0 == unknown */
			}

			/*
			* We pick the battery that claims to have the most minutes left.
			*  (failing a report of minutes, we'll take the highest percent.)
			*/

			if ((secs < 0) && (isnan(i->seconds))) {
			if ((pct < 0) && (isnan(i->fraction))) {
					choose = true;  /* at least we know there's a battery. */
				} else if (pct > i->fraction * 100) {
					choose = true;
				}
			} else if (secs > i->seconds) {
				choose = true;
			}

			if (choose) {
				if (secs != -1)
					i->seconds = secs;
				else
					i->seconds = NAN;
				if (pct != -1)
					i->fraction = pct/100.;
				else
					i->fraction = NAN;
				i->state = st;
				if (vlt != -1)
					i->voltage = vlt/1000000.;
				else
					i->voltage = NAN;
				if (cur != -999999999)
					i->current = cur/1000000.;
				else
					i->current = NAN;
				if (temp != -999999999)
					i->temperature = temp/10.;
				else
					i->temperature = NAN;

				if (isnan(i->fraction) || i->fraction > 100 || i->fraction < 0)
					i->fraction = battery_estimate(i);
			}
		}

		if (type == USB && read_power_file(base, name, "online", str, sizeof (str))) {
			if (strcmp(str, "0\n") == 0)
				i->source = BATTERY;
			else if (strcmp(str, "0\n") == 1)
				i->source = USB;
		}

	}

	closedir(dirp);
	return true;  /* don't look any further. */
}

char *battery_state_string(enum battery_state s)
{
	switch (s) {
	case NO_BATTERY: return "no battery";
	case UNKNOWN: return "unknown";
	case CHARGING: return "charging";
	case ON_BATTERY: return "on battery";
	case FULL: return "full";
	default: return "internal error";
	}
}

void battery_dump(struct battery_info *i)
{
	printf("Battery %.0f %%\n", i->fraction * 100);
	printf("Seconds %.0f\n", i->seconds);
	printf("State %d -- %s\n", i->state, battery_state_string(i->state));
	printf("Voltage %.2f V\n", i->voltage);
	printf("Current %.3f A\n", i->current);
	printf("Temperature  %.1f C\n", i->temperature);
}
