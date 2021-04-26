#!/bin/sh

font=$(fc-match --format=%{file} DejaVuSans.tff)

xinit /bin/sh -c "charging_sdl -wcreapf $font; echo -n \$? > /var/run/chargingret" -- vt1

retreason=$(cat /var/run/chargingret)

if [ $retreason -lt 0 ]; then
    echo charging_sdl failed! booting default runlevel
    openrc default
elif [ $retreason -eq 0 ]; then
    echo booting default runlevel
    openrc default
elif [ $retreason -eq 1 ]; then
    echo powering down from charging mode
    poweroff
elif [ $retreason -eq 2 ]; then
    echo booting default runlevel due to rtc alarm
    openrc default
fi
