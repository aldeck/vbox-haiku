#! /bin/sh
# innotek VirtualBox
# Linux Additions VFS kernel module init script
#
# Copyright (C) 2006-2007 innotek GmbH
#
# Use only with permission.
#


# chkconfig: 35 30 60
# description: VirtualBox Linux Additions VFS kernel module
#
### BEGIN INIT INFO
# Provides:       vboxvfs
# Required-Start: vboxadd
# Required-Stop:
# Default-Start:  3 5
# Default-Stop:
# Description:    VirtualBox Linux Additions VFS kernel module
### END INIT INFO

PATH=$PATH:/bin:/sbin:/usr/sbin

if [ -f /etc/redhat-release ]; then
    system=redhat
elif [ -f /etc/SuSE-release ]; then
    system=suse
elif [ -f /etc/gentoo-release ]; then
    system=gentoo
else
    system=other
fi

if [ "$system" = "redhat" ]; then
    . /etc/init.d/functions
    fail_msg() {
        echo_failure
        echo
    }

    succ_msg() {
        echo_success
        echo
    }

    begin() {
        echo -n "$1"
    }
fi

if [ "$system" = "suse" ]; then
    . /etc/rc.status
    fail_msg() {
        rc_failed 1
        rc_status -v
    }

    succ_msg() {
        rc_reset
        rc_status -v
    }

    begin() {
        echo -n "$1"
    }
fi

if [ "$system" = "gentoo" ]; then
    . /sbin/functions.sh
    fail_msg() {
        eend 1
    }

    succ_msg() {
        eend $?
    }

    begin() {
        ebegin $1
    }

    if [ "`which $0`" = "/sbin/rc" ]; then
        shift
    fi
fi

if [ "$system" = "other" ]; then
    fail_msg() {
        echo " ...fail!"
    }

    succ_msg() {
        echo " ...done."
    }

    begin() {
        echo -n $1
    }
fi

kdir=/lib/modules/`uname -r`/misc
modname=vboxvfs
module="$kdir/$modname"

file=""
test -f $module.o  && file=$module.o
test -f $module.ko && file=$module.ko

fail() {
    if [ "$system" = "gentoo" ]; then
        eerror $1
        exit 1
    fi
    fail_msg
    echo "($1)"
    exit 1
}

test -z "$file" && {
    fail "Kernel module not found"
}

running() {
    lsmod | grep -q $modname[^_-]
}

start() {
    begin "Starting VirtualBox Additions shared folder support ";
    running || {
        modprobe $modname > /dev/null 2>&1 || {
            if dmesg | grep "vboxConnect failed" > /dev/null 2>&1; then
                fail_msg
                echo "You may be trying to run Guest Additions from binary release of VirtualBox"
                echo "in the Open Source Edition."
                exit 1
            fi
            fail "modprobe $modname failed"
        }
    }
    succ_msg
    return 0
}

stop() {
    begin "Stopping VirtualBox Additions shared folder support ";
    if running; then
        rmmod $modname || fail "Cannot unload module $modname"
    fi
    succ_msg
    return 0
}

restart() {
    stop && start
    return 0
}

dmnstatus() {
    if running; then
        echo "VirtualBox Additions shared folder support is currently running."
    else
        echo "VirtualBox Additions shared folder support is not currently running."
    fi
}

case "$1" in
start)
    start
    ;;
stop)
    stop
    ;;
restart)
    restart
    ;;
status)
    dmnstatus
    ;;
*)
    echo "Usage: $0 {start|stop|restart|status}"
    exit 1
esac

exit
