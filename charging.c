#include <SDL2/SDL.h>

#include <SDL2/SDL_stdinc.h>
#include <sys/ioctl.h>
#include <linux/rtc.h>
#include <fcntl.h>
#include <stdlib.h>
#include <time.h>
#include <stdbool.h>
#include <stdint.h>
#include <signal.h>
#include <time.h>

#include <unistd.h>
#include <GLES2/gl2.h>

#include "battery.h"
#include "draw.h"
#include "log.h"
#include "backlight.h"

#define CHARGING_SDL_VERSION "1.2"

#define SCREENTIME 5

#define RTC_DEVICE "/dev/rtc0"

#define CHECK_CREATE_SUCCESS(obj)                               \
    if (!obj) {                                                 \
        ERROR("failed to allocate object: %s", SDL_GetError()); \
        SDL_Quit();                                             \
        exit(-1);                                                \
    }

enum {
    EXIT_BOOT = 0,
    EXIT_SHUTDOWN = 1,
    EXIT_ALARM = 2
};

sig_atomic_t running = true;
sig_atomic_t retreason = EXIT_BOOT;

void int_handler(int dummy)
{
    running = false;
}

void alarm_handler(int dummy)
{
    running = false;
    retreason = EXIT_ALARM;
}

void usage(char* appname)
{
    printf("Usage: %s [-oeaw] \n\
    -o: prevent burn-in on OLED screens\n\
    -e: exit immediately if not charging\n\
    -a: exit on rtc alarm\n\
    -w: run in window\n\
    -t: use mock battery\n\
    -b: autoboot when battery is > 20%%\n",
        appname);
}

struct battery_device {
    double current;
    int is_charging;
    int percent;
};

int set_alarm_from_rtc(int rtc_fd)
{
    struct rtc_wkalrm wake;
    struct tm tm = { 0 };
    time_t alarm_time;

    if (ioctl(rtc_fd, RTC_WKALM_RD, &wake) < 0)
        return -1;

    if (wake.enabled != 1 || wake.time.tm_year == -1)
        return 1;

    tm.tm_sec = wake.time.tm_sec;
    tm.tm_min = wake.time.tm_min;
    tm.tm_hour = wake.time.tm_hour;
    tm.tm_mday = wake.time.tm_mday;
    tm.tm_mon = wake.time.tm_mon;
    tm.tm_year = wake.time.tm_year;
    tm.tm_isdst = -1;  /* assume the system knows better than the RTC */

    alarm_time = mktime(&tm);
    if (alarm_time == (time_t)-1)
        return -1;

    double delta = difftime(time(NULL), alarm_time);

    if (delta > 0)
        alarm((unsigned int)delta);

    return 0;
}

void update_bat_info(struct battery_device* dev, bool mock)
{
    if(!mock)
    {
        struct battery_info bat;
        LOG("INFO", "Reading Battery");
        if (battery_fill_info(&bat)) {
            dev->current = bat.current;
            if (!isfinite(bat.fraction) || bat.fraction <= 0) {
                dev->percent = 1;
                LOG("WARN", "Battery Percent out of range");
            } else {
                dev->percent = (int)(bat.fraction * 100.0);
            }
            LOG("INFO", "Battery Percent: %d", dev->percent);
            dev->is_charging = bat.source == USB;
        } else {
            LOG("WARN", "Could not read battery");
        }
    }
    else
    {
        static int state = 0;
        const int percents[] = {50, 90, 10, 0, 1, -1, 100};
        LOG("INFO", "mock percentage: %i", percents[state]);
        dev->is_charging = true;
        dev->current = -10;
        dev->percent = percents[state++];
        if(state > (sizeof(percents)/sizeof(percents[0]))-1)
            state = 0;

    }
}

struct config
{
    bool flag_oled:1;
    bool flag_exit:1;
    bool flag_alarm:1;
    bool flag_window:1;
    bool flag_mock_bat:1;
    bool flag_autoboot:1;
};

int main(int argc, char** argv)
{
    SDL_LogSetPriority(SDL_LOG_CATEGORY_VIDEO ,SDL_LOG_PRIORITY_DEBUG);
    LOG("INFO", "charging-sdl version %s", CHARGING_SDL_VERSION);

    struct config config = {0};

    int screen_w = 540;
    int screen_h = 960;

    int rtc_fd;

    SDL_Window* window;
    SDL_Renderer* renderer;

    struct battery_device bat_info = {};
    SDL_Surface* battery_icon;
    SDL_Surface* lightning_icon;

    signal(SIGINT, int_handler);
    signal(SIGHUP, int_handler);
    signal(SIGTERM, int_handler);
    signal(SIGALRM, alarm_handler);

    int opt;
    while ((opt = getopt(argc, argv, "obeawt")) != -1) {
        switch (opt) {
        case 'o':
            config.flag_oled = true;
            break;
        case 'e':
            config.flag_exit = true;
            break;
        case 'a':
            config.flag_alarm = true;
            break;
        case 'w':
            config.flag_window = true;
            break;
        case 't':
            config.flag_mock_bat = true;
            break;
        case 'b':
            config.flag_autoboot = true;
            break;
        default:
            usage(argv[0]);
            return -1;
        }
    }

    rtc_fd = open(RTC_DEVICE, O_RDONLY | O_CLOEXEC);
    if(rtc_fd < 0) {
        LOG("INFO", "failed to open RTC: %s", RTC_DEVICE);
    } else if (set_alarm_from_rtc(rtc_fd) != 0) {
        LOG("INFO", "failed to read RTC: %s", RTC_DEVICE);
    }

    if(rtc_fd >= 0)
        close(rtc_fd);

    if (config.flag_exit) {
        update_bat_info(&bat_info, config.flag_mock_bat);
        if (!bat_info.is_charging)
            return retreason;
    }

    int max_brightness = 0;
    int brightness_file = open_brightness_file(&max_brightness);

    if (SDL_Init(SDL_INIT_EVENTS | SDL_INIT_VIDEO) < 0) {
        ERROR("failed to init SDL: %s", SDL_GetError());
        return -1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

    if (config.flag_window) {
        LOG("INFO", "creating test window");
        window = SDL_CreateWindow("Charge - Test Mode",
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            screen_w, screen_h, 0);
    } else {
        SDL_DisplayMode mode = { SDL_PIXELFORMAT_UNKNOWN, 0, 0, 0, 0 };
        if (SDL_GetDisplayMode(0, 0, &mode) != 0) {
            ERROR("error fetching display mode: %s", SDL_GetError());
            SDL_Quit();
            return -1;
        }
        screen_w = mode.w;
        screen_h = mode.h;
        LOG("INFO", "creating window");
        window = SDL_CreateWindow("Charge",
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            0, 0, SDL_WINDOW_FULLSCREEN | SDL_WINDOW_SHOWN);
    }
    CHECK_CREATE_SUCCESS(window);

    if (SDL_ShowCursor(SDL_DISABLE) < 0 )
        LOG("WARNING", "Failed to disable cursor");

    LOG("INFO" "using video driver: %s", SDL_GetCurrentVideoDriver());

    LOG("INFO", "creating general renderer");
    renderer = SDL_CreateRenderer(window, -1, 0);
    CHECK_CREATE_SUCCESS(renderer);

    LOG("INFO", glGetString(GL_RENDERER));

    SDL_Rect battery_rect;
    make_battery_rect(screen_w, screen_h, &battery_rect);

    LOG("INFO", "creating icon bitmaps");
    battery_icon = make_battery_icon(battery_rect, screen_w, screen_h);
    CHECK_CREATE_SUCCESS(battery_icon);

    SDL_Rect is_charging_area = {
        .x = 0,
        .y = screen_w / 8 * 0.2,
        .w = screen_w / 8,
        .h = screen_w / 8
    };

    lightning_icon = make_lightning_icon(is_charging_area.w, is_charging_area.h);
    CHECK_CREATE_SUCCESS(battery_icon);

    LOG("INFO", "creating textures from icons");
    SDL_Texture* battery_icon_texture = SDL_CreateTextureFromSurface(renderer, battery_icon);
    CHECK_CREATE_SUCCESS(battery_icon_texture);

    SDL_Texture* lightning_icon_texture = SDL_CreateTextureFromSurface(renderer, lightning_icon);
    CHECK_CREATE_SUCCESS(lightning_icon_texture);

    SDL_RenderClear(renderer);

    SDL_Event ev;
    Uint32 start = SDL_GetTicks();
    Uint32 last_charging = SDL_GetTicks();
    Uint32 frame = 0;

    SDL_Rect oled_rect;
    if (config.flag_oled) {
        srand(time(NULL));
        make_oled_rect(screen_h, &oled_rect);
    }

    int *keys;

    bool displayOn = true;
    Uint32 blinking = 0;

    while (running) {
        update_bat_info(&bat_info, config.flag_mock_bat);

        ++frame;
        blinking = blinking == 0 ? 0 : blinking - 1;

        if (displayOn) {
            SDL_RenderClear(renderer);

            if (bat_info.is_charging) {
                SDL_RenderCopy(renderer, lightning_icon_texture, NULL, &is_charging_area);
                last_charging = SDL_GetTicks();
                if(config.flag_autoboot && bat_info.percent > 20) {
                    retreason = EXIT_BOOT;
                    running = false;
                }
            } else if (config.flag_exit && SDL_GetTicks() - last_charging >= 2000) {
                retreason = EXIT_SHUTDOWN;
                running = false;
            }

            if(frame % 2 || blinking == 0) {
                SDL_RenderCopy(renderer, battery_icon_texture, NULL, NULL);

                if (config.flag_oled) {
                    SDL_SetRenderDrawColor(renderer, 128, 128, 128, 255);
                    SDL_RenderFillRect(renderer, &oled_rect);
                    move_oled_rect(screen_w, screen_h, &oled_rect);
                }

                SDL_Rect bat_ch_rect;
                bat_ch_rect.x = battery_rect.x + battery_rect.h * 0.05;
                bat_ch_rect.y = battery_rect.y + (battery_rect.h*0.90) * (100 - bat_info.percent)/100.0f + battery_rect.h * 0.05;
                bat_ch_rect.h = battery_rect.h - bat_ch_rect.y + battery_rect.y - battery_rect.h * 0.05;
                bat_ch_rect.w = battery_rect.w - battery_rect.h * 0.1;

                SDL_SetRenderDrawColor(renderer, (100-bat_info.percent)/100.0f*255, bat_info.percent/100.0f*255, 0, 255);
                SDL_RenderFillRect(renderer, &bat_ch_rect);
            }

            if(config.flag_window)
                LOG("INFO", "refresh");
            SDL_RenderPresent(renderer);
        }
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_KEYDOWN) {
                update_bat_info(&bat_info, config.flag_mock_bat);
                /* Droid 4 power button registers as 1073741824 this is a sdl bug*/
                if(ev.key.keysym.sym == SDLK_POWER || ev.key.keysym.sym == 1073741824) {
                    if(bat_info.percent > 5) {
                        retreason = EXIT_BOOT;
                        running = false;
                        break;
                    }
                    else {
                        blinking = 10;
                    }
                }
                if(brightness_file >= 0) {
                    char buf[256] = "";
                    int len = snprintf(buf, 256, "%i", max_brightness);
                    write(brightness_file, buf, len < 256 ? len : 256);
                    displayOn = true;
                }
                start = SDL_GetTicks();
            }
        }
        if(blinking == 0)
            SDL_Delay(1000);
        else
            SDL_Delay(250);
        if (SDL_GetTicks() - start >= SCREENTIME * 1000) {
            if(brightness_file >= 0) {
                char buf = '0';
                write(brightness_file, &buf, 1);
                displayOn = false;
            }
        }
    }

    SDL_FreeSurface(lightning_icon);
    SDL_FreeSurface(battery_icon);

    SDL_DestroyTexture(battery_icon_texture);
    SDL_DestroyTexture(lightning_icon_texture);

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    if(brightness_file >= 0) {
        char buf[256] = "";
        int len = snprintf(buf, 256, "%i", max_brightness);
        write(brightness_file, buf, len < 256 ? len : 256);
        close(brightness_file);
    }

    return retreason;
}
