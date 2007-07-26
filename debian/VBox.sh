#!/bin/sh
#
# innotek VirtualBox
#
# Copyright (C) 2006-2007 innotek GmbH
#
# Use only with permission
#

PATH="/usr/bin:/bin:/usr/sbin:/sbin"

# Note: This script must not fail if the module was not successfully installed
#       because the user might not want to run a VM but only change VM params!

if [ "$1" = "shutdown" ]; then
    SHUTDOWN="true"
elif ! lsmod|grep -q vboxdrv; then
    cat << EOF
WARNING: The vboxdrv kernel module is not loaded. Either there is no module
         available for the current kernel (`uname -r`) or it failed to
         load. Please recompile the kernel module and install it by

           sudo /etc/init.d/vboxdrv setup

         You will not be able to start VMs until this problem is fixed.
EOF
elif [ ! -c /dev/vboxdrv ]; then
    cat << EOF
WARNING: The character device /dev/vboxdrv does not exist. Try

           sudo /etc/init.d/virtualbox restart

         and if that is not successful, try to re-install the package.

	 You will not be able to start VMs until this problem is fixed.
EOF
elif [ ! -w /dev/vboxdrv ]; then
    if [ "`id | grep vboxusers`" = "" ]; then
        cat << EOF
WARNING: You are not a member of the "vboxusers" group.  Please add yourself
         to this group before starting VirtualBox.

	 You will not be able to start VMs until this problem is fixed.
EOF
    else
        cat << EOF
WARNING: /dev/vboxdrv not writable for some reason. If you recently added the
         current user to the vboxusers group then you have to logout and
	 re-login to take the change effect.

	 You will not be able to start VMs until this problem is fixed.
EOF
    fi
fi

if [ -f /etc/vbox/module_not_compiled ]; then
    cat << EOF
WARNING: The compilation of the vboxdrv.ko kernel module failed during the
         installation for some reason. Starting a VM will not be possible.
         Please consult the User Manual for build instructions.
EOF
fi

SERVER_PID=`ps -U \`whoami\` | grep VBoxSVC | awk '{ print $1 }'`
if [ -z "$SERVER_PID" ]; then
    # Server not running yet/anymore, cleanup socket path.
    # See IPC_GetDefaultSocketPath()!
    if [ -n "$LOGNAME" ]; then
        rm -rf /tmp/.vbox-$LOGNAME-ipc > /dev/null 2>&1
    else
        rm -rf /tmp/.vbox-$USER-ipc > /dev/null 2>&1
    fi
fi

if [ "$SHUTDOWN" = "true" ]; then
    if [ -n "$SERVER_PID" ]; then
        kill -TERM $SERVER_PID
        sleep 2
    fi
    exit 0
fi

APP=`which $0`
APP=${APP##/*/}
case "$APP" in
  VirtualBox)
    exec "/usr/lib/virtualbox/VirtualBox" "$@"
  ;;
  VBoxManage)
    exec "/usr/lib/virtualbox/VBoxManage" "$@"
  ;;
  VBoxSDL)
    exec "/usr/lib/virtualbox/VBoxSDL" "$@"
  ;;
  VBoxVRDP)
    exec "/usr/lib/virtualbox/VBoxVRDP" "$@"
  ;;
  *)
    echo "Unknown application - $APP"
  ;;
esac
