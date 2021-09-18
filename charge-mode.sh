#!/bin/sh

export DFBARGS="system=fbdev,no-cursor"
export SDL_VIDEODRIVER=directfb

font=$(fc-match --format=%{file} DejaVuSans.tff)

charging_sdl -creapf $font

retreason=$?

if [ $retreason -eq 0 ]; then
    echo booting default runlevel
    openrc default
elif [ $retreason -eq 1 ]; then
    echo powering down from charging mode
    poweroff
elif [ $retreason -eq 2 ]; then
    echo booting default runlevel due to rtc alarm
    openrc default
else
    echo charging_sdl failed! booting default runlevel
    openrc default
fi
