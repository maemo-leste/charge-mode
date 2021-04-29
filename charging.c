#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

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

#include "battery.h"
#include "atlas.h"
#include "draw.h"

#define CHARGING_SDL_VERSION "0.1.0"

#define UPTIME 5

#define RTC_DEVICE "/dev/rtc0"

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
    printf("Usage: %s [-tpcoearwf] [-f font]\n\
    -t: launch %s in test mode\n\
    -p: display battery capacity\n\
    -c: attempt to get current\n\
    -o: prevent burn-in on OLED screens\n\
    -r: disable exit on timer\n\
    -e: exit immediately if not charging\n\
    -a: exit on rtc alarm\n\
    -w: run in window\n\
    -f: font to use\n",
        appname, appname);
}

#ifndef NDEBUG
#define LOG(status, msg, ...)   \
    printf("[%s] ", status);    \
    printf(msg, ##__VA_ARGS__); \
    printf("\n")
#define ERROR(msg, ...) LOG("ERROR", msg, ##__VA_ARGS__)
#else
#define LOG(status, msg, ...)
#define ERROR(msg, ...)          \
    fprintf(stderr, "[ERROR] "); \
    fprintf(stderr, msg, ##__VA_ARGS)
#endif

#define CHECK_CREATE_SUCCESS(obj)                               \
    if (!obj) {                                                 \
        ERROR("failed to allocate object: %s", SDL_GetError()); \
        SDL_Quit();                                             \
        if (TTF_WasInit())                                      \
            TTF_Quit();                                         \
        exit(-1);                                                \
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

void update_bat_info(struct battery_device* dev)
{
    struct battery_info bat;
    if (battery_fill_info(&bat)) {
        dev->current = bat.current;
        if (!isfinite(bat.fraction)) {
            dev->percent = -1;
        } else {
            dev->percent = (int)(bat.fraction * 100.0);
        }
        dev->is_charging = bat.state == CHARGING;
    }
}

int main(int argc, char** argv)
{
    SDL_LogSetPriority(SDL_LOG_CATEGORY_VIDEO ,SDL_LOG_PRIORITY_DEBUG);
    LOG("INFO", "charging-sdl version %s", CHARGING_SDL_VERSION);

    char flag_test = 0, flag_percent = 0, flag_current = 0, flag_timer = 1;
    char flag_oled = 0, flag_exit = 0, flag_alarm = 0, flag_window=0;

    int screen_w = 540;
    int screen_h = 960;

    int rtc_fd;

    int numkeys;

    // needed only for mapphone specific hack
    int fb = open("/dev/fb0", O_RDWR);
    if(fb < 0)
        LOG("INFO", "failed to open framebuffer /dev/fb0");

    SDL_Window* window;
    SDL_Renderer* renderer;

    struct battery_device bat_info;
    SDL_Surface* battery_icon;
    SDL_Surface* lightning_icon;
    struct character_atlas* percent_atlas = NULL;
    char* flag_font = NULL;
    TTF_Font* font_struct = NULL;

    signal(SIGINT, int_handler);
    signal(SIGHUP, int_handler);
    signal(SIGTERM, int_handler);
    signal(SIGALRM, alarm_handler);

    int opt;
    while ((opt = getopt(argc, argv, "tpcoearwf:")) != -1) {
        switch (opt) {
        case 't':
            flag_test = 1;
            break;
        case 'p':
            flag_percent = 1;
            break;
        case 'c':
            flag_current = 1;
            break;
        case 'o':
            flag_oled = 1;
            break;
        case 'e':
            flag_exit = 1;
            break;
        case 'a':
            flag_alarm = 1;
            break;
        case 'r':
            flag_timer = 0;
            break;
        case 'w':
            flag_window = 1;
            break;
        case 'f':
            flag_font = optarg;
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

    if (flag_exit) {
        update_bat_info(&bat_info);
        if (!bat_info.is_charging)
            return retreason;
    }

    if (SDL_Init(SDL_INIT_EVENTS | SDL_INIT_VIDEO) < 0) {
        ERROR("failed to init SDL: %s", SDL_GetError());
        return -1;
    }
    if ((flag_percent || flag_current) && flag_font) {
        if (TTF_Init() < 0) {
            ERROR("failed to init SDL: %s", TTF_GetError());
            SDL_Quit();
            return -1;
        }
    }

    if (flag_test || flag_window) {
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

    printf("using video driver: %s", SDL_GetCurrentVideoDriver());

    LOG("INFO", "creating general renderer");
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    CHECK_CREATE_SUCCESS(renderer);

    LOG("INFO", "creating icon bitmaps");
    battery_icon = make_battery_icon(screen_w, screen_h);
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

    if (flag_percent || flag_current) {
        if (flag_font) {
            LOG("INFO", "using font %s", flag_font);
            if (flag_font == NULL) {
                ERROR("no font specified");
                TTF_Quit();
                SDL_Quit();
                return -1;
            }
            SDL_Color color = { 255, 255, 255 };
            font_struct = TTF_OpenFont(flag_font, 256);
            if (!font_struct) {
                ERROR("failed to open font: %s\n", TTF_GetError());
                TTF_Quit();
                SDL_Quit();
                return -1;
            }
            percent_atlas = create_character_atlas(renderer, "0123456789A.", color, font_struct);
            if (percent_atlas) {
                LOG("INFO", "successfully created percent/number text atlas");
            } else {
                ERROR("failed to create font: %s\n", TTF_GetError());
                TTF_Quit();
                SDL_Quit();
                return -1;
            }
        } else {
            LOG("WARNING", "no font set, turning off text renderering");
            percent_atlas = NULL;
        }
    }
    SDL_Event ev;
    Uint32 start = SDL_GetTicks();

    SDL_Rect battery_area;
    make_battery_rect(screen_w, screen_h, &battery_area);

    SDL_Rect oled_rect;
    if (flag_oled) {
        srand(time(NULL));
        make_oled_rect(screen_h, &oled_rect);
        SDL_SetRenderDrawColor(renderer, 128, 128, 128, 255);
    }

    Uint32 start_time = 0;
    Uint32 end_time = 0;

    int not_charging_timer=0;

    int *keys;

    char percent_text[4];
    char current_text[6];
    while (running) {
        SDL_RenderClear(renderer);

        update_bat_info(&bat_info);

        SDL_RenderCopy(renderer, battery_icon_texture, NULL, NULL);

        if (flag_percent && percent_atlas && bat_info.percent > 0) {
            sprintf(percent_text, "%d", bat_info.percent);
            if (percent_text[2]) {
                character_atlas_render_string(renderer, percent_atlas, percent_text,
                    battery_area.w * 0.8,
                    battery_area.w * 0.1 + battery_area.x,
                    battery_area.y + battery_area.h / 2);
            } else {
                character_atlas_render_string(renderer, percent_atlas, percent_text,
                    battery_area.w * 0.66 * 0.8,
                    battery_area.w * 2.33 * 0.1 + battery_area.x,
                    battery_area.y + battery_area.h / 2);
            }
        }

        if (bat_info.is_charging) {
            sprintf(current_text, "%2.1fA", bat_info.current);
            if (flag_current && bat_info.current > 0 && percent_atlas) {
                character_atlas_render_string(renderer, percent_atlas, current_text, is_charging_area.w * 1.2,
                    is_charging_area.w - is_charging_area.w * 0.05, is_charging_area.h + is_charging_area.h * 1.5);
            }
            SDL_RenderCopy(renderer, lightning_icon_texture, NULL, &is_charging_area);
            not_charging_timer = 0;
        } else if (flag_exit && ++not_charging_timer > 3) {
                retreason = EXIT_SHUTDOWN;
                running = false;
        }

        if (flag_oled) {
            SDL_RenderFillRect(renderer, &oled_rect);
            move_oled_rect(screen_w, screen_h, &oled_rect);
        }

        if (SDL_SCANCODE_KP_POWER <= numkeys && keys[SDL_SCANCODE_KP_POWER] == 1) {
            retreason = EXIT_BOOT;
            running = false;
        }

        SDL_RenderPresent(renderer);
        if (flag_test) {
            while (SDL_PollEvent(&ev)) {
                switch (ev.type) {
                case SDL_QUIT:
                    retreason = EXIT_BOOT;
                    running = false;
                    break;
                }
            }
        } else {
            while (SDL_PollEvent(&ev)){
                if (ev.type == SDL_KEYDOWN) {
                    /* Droid 4 power button registers as 1073741824 this is a sdl bug*/
                    if(ev.key.keysym.sym == SDL_SCANCODE_POWER || ev.key.keysym.sym == 1073741824) {
                        retreason = EXIT_BOOT;
                        running = false;
                        break;
                    }
                }
            }
            SDL_Delay(1000);
            if (flag_timer && SDL_GetTicks() - start >= UPTIME * 1000) {
                retreason = EXIT_BOOT;
                running = false;
            }
            SDL_RenderClear(renderer);

            // HACK: get mapphone comand mode displays to work
            if (fb >= 0) {
                lseek(fb, 0, SEEK_SET);
                char tmp = 0;
                write(fb, &tmp, 1);
            }
        }
    }

    if (percent_atlas)
        free_character_atlas(percent_atlas);
    if (font_struct)
        TTF_CloseFont(font_struct);
    SDL_FreeSurface(lightning_icon);
    SDL_FreeSurface(battery_icon);

    SDL_DestroyTexture(battery_icon_texture);
    SDL_DestroyTexture(lightning_icon_texture);

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    if (TTF_WasInit())
        TTF_Quit();
    SDL_Quit();
    return retreason;
}
