#pragma once

#define BACKLIGHT_SYSFS_PATH			"/sys/class/backlight/"
#define BACKLIGHT_BRIGHTNESS_FILE		"/brightness"
#define BACKLIGHT_MAX_BRIGHTNESS_FILE		"/max_brightness"

int open_brightness_file(int *max_bright);
