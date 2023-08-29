#!/bin/sh

export SDL_VIDEODRIVER=kmsdrm
export SDL_RENDER_DRIVER=opengles2
charging_sdl -eab

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
