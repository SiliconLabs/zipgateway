#!/bin/sh

#############################################################################
# This script implements a generic "Node Identify driver" by printing the
# on/off indicator sequence to a log file.
#
# The script will spawn itself into a background process that will control
# the blink sequence.
#
# Call as:
#    script_name <on_time_ms> <off_time_ms> <num_cycles>
#
# Parameters
#    on_time_ms:  ON duration (in milliseconds) for a single blink cycle.
#                 If on_time is zero the indicator will be turned off and
#                 the background process will terminate immediately.
#    off_time_ms: OFF duration (in milliseconds) for a single blink cycle.
#    num_cycles:  Number of blink cycles. The background script will
#                 terminate when the specified number of cycles have
#                 completed. If num_cycles is zero the background script
#                 will run bg_blinker "forever" or until killed.
#############################################################################

# Make sure SCRIPTNAME points to where the script is installed
SCRIPTNAME=/usr/local/etc/zipgateway_node_identify_generic.sh
PIDFILE=/var/run/zipgateway-node-identify.pid
# The LED to control. On Raspberry Pi 3B+, led0 (the one next to power led) is available.
# This can be overridden with by setting as environment variable
test -z "$LEDPATH" && LEDPATH=/sys/class/leds/led0

msglog() {
    # The bg_blinker runs in a background process. Debug/status output should
    # go to a logfile instead of STDOUT
    echo "$(date '+%Y%m%d-%H:%M:%S') $*" >> /var/log/zipgateway-node-identify.log
}

# Divide integer by 1000 without calling external program.
# (/bin/sh does not support floating point math)
ms2sec() {
    num_digits=${#1}
    case $num_digits in
        0) res=0.0 ;;
        1) res=0.00$1 ;;
        2) res=0.0$1 ;;
        3) res=0.$1 ;;
        *) secpart=${1%???}
           mspart=${1#$secpart}
           res=$secpart.$mspart
        ;;
    esac
    echo $res
}

# Commands to configure Raspberry Pi 3B+ LED to blink
indicator_config() {
    echo none > $LEDPATH/trigger
    echo timer > $LEDPATH/trigger
    echo $1 > $LEDPATH/delay_on
    echo $2 > $LEDPATH/delay_off
}

# Put commands to turn indicator ON in indicator_on
indicator_on() {
    msglog "Indicator ON"
}

# Put commands to turn indicator OFF in indicator_off
indicator_off() {
    msglog "Indicator OFF"
}

# Stop RPi LED blinking
indicator_unconfig() {
    echo none > $LEDPATH/trigger
}
# bg_blinker is running in a background process
bg_blinker() {
    msglog "ENTER: bg_blinker $*"

    on_time_ms=$1
    off_time_ms=$2
    num_cycles=$3

    if [ $on_time_ms -eq 0 ]; then
        indicator_off
        msglog "EXIT: bg_blinker $*"
        return 0
    fi

    test -f $LEDPATH/trigger && indicator_config $on_time_ms $off_time_ms

    on_time_sec=$(ms2sec $on_time_ms)
    off_time_sec=$(ms2sec $off_time_ms)
    n=0

    # This loop is just a naive implementation of a periodic blinker.
    # It does not take into account the time used for calling the
    # various commands. Hence the blink sequence will drift.
    # In other words: the sequence will complete after:
    #    (on_time + off_time + "cmd_overhead_time") * num_cycles
    #
    # NB: num_periods = 0 means "run until stopped/killed"
    while [ $num_cycles -eq 0 -o $n -lt $num_cycles ] ; do
        n=$((n+1))
        msglog "n=$n"
        indicator_on
        sleep ${on_time_sec}s
        indicator_off
        sleep ${off_time_sec}s
    done

    test -f $LEDPATH/trigger && indicator_unconfig
    msglog "EXIT: bg_blinker $*"


    return 0
}

# Kill the process with pid found in $PIDFILE
kill_bg_blinker() {
    if [ -r "$PIDFILE" ]; then
        read pid < "$PIDFILE"
        if [ -n "${pid:-}" ]; then
            rm "$PIDFILE"
            if $(kill -0 "${pid:-}" 2> /dev/null); then
                kill "${pid:-}" && return 0
            fi
        fi
    fi
}


# Here we go!

# If called with "magic" string (by ourself) we are running in the background
if [ "$1" = "bg_blinker" ]; then
    shift
    bg_blinker "$@"
else
    # This is the normal entry point when calling the script
    if [ $# -ne 3 ]; then
        echo 'Required parameters: <on_time_ms> <off_time_ms> <num_cycles>' >&2
        return 1
    else
        # Kill current blinker process (if any)
        kill_bg_blinker

        # Start bg_blinker in background and save its process id
        $SCRIPTNAME bg_blinker "$@" &
        echo $! > $PIDFILE

        return 0
    fi
fi
