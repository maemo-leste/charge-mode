#!/bin/sh

export SDL_VIDEODRIVER=kmsdrm
export SDL_HINT_RENDER_DRIVER=software
charging_sdl -ea

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
