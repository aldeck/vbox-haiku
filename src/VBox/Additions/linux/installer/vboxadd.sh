#! /bin/sh
# InnoTek VirtualBox
# Linux Additions kernel module init script
#
# Copyright (C) 2006 InnoTek Systemberatung GmbH
#
# Use only with permission.
#


# chkconfig: 35 30 60
# description: VirtualBox Linux Additions kernel module
#
### BEGIN INIT INFO
# Provides:       vboxadd
# Required-Start:
# Required-Stop:
# Default-Start:  3 5
# Default-Stop:
# Description:    VirtualBox Linux Additions kernel module
### END INIT INFO

PATH=$PATH:/bin:/sbin:/usr/sbin

system=unknown
if [ -f /etc/redhat-release ]; then
    system=redhat
elif [ -f /etc/SuSE-release ]; then
    system=suse
elif [ -f /etc/debian_version ]; then
    system=debian
elif [ -f /etc/gentoo-release ]; then
    system=gentoo
elif [ -f /etc/slackware-version ]; then
    system=slackware
else
    echo "$0: Unknown system" 1>&2
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
        echo -n $1
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
        echo -n $1
    }
fi

if [ "$system" = "debian" ]; then
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

if [ "$system" = "slackware" ]; then
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
dev=/dev/vboxadd
modname=vboxadd
module=$kdir/$modname

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
    begin "Starting VirtualBox Additions ";
    running || {
        rm -f $dev || {
            fail "Cannot remove $dev"
        }

        modprobe $modname || {
            fail "modprobe $modname failed"
        }

        sleep 1
    }
    if [ ! -c $dev ]; then
        maj=`sed -n 's;\([0-9]\+\) vboxadd;\1;p' /proc/devices`
        if [ ! -z "$maj" ]; then
            min=0
        else
            min=`sed -n 's;\([0-9]\+\) vboxadd;\1;p' /proc/misc`
            if [ ! -z "$min" ]; then
                maj=10
            fi
        fi
        test -z "$maj" && {
            rmmod $modname
            fail "Cannot locate the VirtualBox device"
        }

        mknod -m 0664 $dev c $maj $min || {
            rmmod $modname
            fail "Cannot create device $dev with major $maj and minor $min"
        }
    fi

    succ_msg
    return 0
}

stop() {
    begin "Stopping VirtualBox Additions ";
    if running; then
        rmmod $modname || fail "Cannot unload module $modname"
        rm -f $dev || fail "Cannot unlink $dev"
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
        echo "The VirtualBox Additions are currently running."
    else
        echo "The VirtualBox Additions are not currently running."
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
