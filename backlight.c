#include "backlight.h"

#define _POSIX_SOURCE
#define _DEFAULT_SOURCE

#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/types.h>
#include <linux/limits.h>
#include <fcntl.h>

#include "log.h"

int open_brightness_file(int *max_bright)
{
    DIR *dir;
    struct dirent *entry;

    if ((dir = opendir(BACKLIGHT_SYSFS_PATH)) == NULL) {
        ERROR("Can not open dir %s", BACKLIGHT_SYSFS_PATH);
        return -1;
    }

    seekdir(dir, 2);
    entry = readdir(dir);

    if(entry == NULL) {
        ERROR("No backlight available");
        closedir(dir);
        return -1;
    }

    char buf[PATH_MAX];
    snprintf(buf, PATH_MAX, "%s%s%s", BACKLIGHT_SYSFS_PATH, entry->d_name, BACKLIGHT_MAX_BRIGHTNESS_FILE);
    int max_bright_fd = open(buf, O_RDONLY);
    if (max_bright_fd < 0) {
        ERROR("No max_brightness available at %s", buf);
        closedir(dir);
        return -1;
    }
    if(read(max_bright_fd, buf, PATH_MAX) <= 0 || (*max_bright = atoi(buf)) == 0) {
        ERROR("could not read max_brightness");
        close(max_bright_fd);
        return -1;
    }
    close(max_bright_fd);

    snprintf(buf, PATH_MAX, "%s%s%s", BACKLIGHT_SYSFS_PATH, entry->d_name, BACKLIGHT_BRIGHTNESS_FILE);

    closedir(dir);

    return open(buf, O_WRONLY);
}
