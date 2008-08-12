#!/bin/sh
# Sun xVM VirtualBox
# VirtualBox postinstall script for Solaris.
#
# Copyright (C) 2007-2008 Sun Microsystems, Inc.
#
# This file is part of VirtualBox Open Source Edition (OSE), as
# available from http://www.virtualbox.org. This file is free software;
# you can redistribute it and/or modify it under the terms of the GNU
# General Public License (GPL) as published by the Free Software
# Foundation, in version 2 as it comes in the "COPYING" file of the
# VirtualBox OSE distribution. VirtualBox OSE is distributed in the
# hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
#
# Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa
# Clara, CA 95054 USA or visit http://www.sun.com if you need
# additional information or have any questions.
#

# Check for xVM/Xen
currentisa=`uname -i`
if test "$currentisa" = "i86xpv"; then
    echo "## VirtualBox cannot run under xVM Dom0! Fatal Error, Aborting installation!"
    exit 2
fi

currentzone=`zonename`
if test "$currentzone" = "global"; then
    echo "Configuring VirtualBox kernel module..."
    /opt/VirtualBox/vboxdrv.sh restart silentunload
fi

# create links
echo "Creating links..."
# @todo desctivated, needs to be redone for the QT4 GUI
#if test -f /opt/VirtualBox/VirtualBox; then
#    /usr/sbin/installf -c none $PKGINST /usr/bin/VirtualBox=/opt/VirtualBox/VBox.sh s
#    # Qt links
#    /usr/sbin/installf -c none $PKGINST /usr/bin/VBoxQtconfig=/opt/VirtualBox/VBox.sh s
#    /usr/sbin/installf -c none $PKGINST /opt/VirtualBox/qtgcc/lib/libqt-mt.so=/opt/VirtualBox/qtgcc/lib/libqt-mt.so.3 s
#    /usr/sbin/installf -c none $PKGINST /opt/VirtualBox/qtgcc/lib/libqt-mt.so.3.3=/opt/VirtualBox/qtgcc/lib/libqt-mt.so.3 s
#    /usr/sbin/installf -c none $PKGINST /opt/VirtualBox/qtgcc/lib/libqt-mt.so.3.3.8=/opt/VirtualBox/qtgcc/lib/libqt-mt.so.3 s
#fi
if test -f /opt/VirtualBox/VirtualBox3; then
    /usr/sbin/installf -c none $PKGINST /usr/bin/VirtualBox3=/opt/VirtualBox/VBox.sh s
    # Qt links
    /usr/sbin/installf -c none $PKGINST /usr/bin/VBoxQtconfig=/opt/VirtualBox/VBox.sh s
    /usr/sbin/installf -c none $PKGINST /opt/VirtualBox/qtgcc/lib/libqt-mt.so=/opt/VirtualBox/qtgcc/lib/libqt-mt.so.3 s
    /usr/sbin/installf -c none $PKGINST /opt/VirtualBox/qtgcc/lib/libqt-mt.so.3.3=/opt/VirtualBox/qtgcc/lib/libqt-mt.so.3 s
    /usr/sbin/installf -c none $PKGINST /opt/VirtualBox/qtgcc/lib/libqt-mt.so.3.3.8=/opt/VirtualBox/qtgcc/lib/libqt-mt.so.3 s
fi
/usr/sbin/installf -c none $PKGINST /usr/bin/VBoxManage=/opt/VirtualBox/VBox.sh s
/usr/sbin/installf -c none $PKGINST /usr/bin/VBoxSDL=/opt/VirtualBox/VBox.sh s
if test -f /opt/VirtualBox/VBoxHeadless; then
    /usr/sbin/installf -c none $PKGINST /usr/bin/VBoxHeadless=/opt/VirtualBox/VBox.sh s
    if test -f /opt/VirtualBox/VBoxVRDP.so; then
        /usr/sbin/installf -c none $PKGINST /usr/bin/VBoxVRDP=/opt/VirtualBox/VBox.sh s
    fi
fi
if test -f /var/svc/manifest/application/virtualbox/webservice.xml; then
    /usr/sbin/svccfg import /var/svc/manifest/application/virtualbox/webservice.xml
fi
/usr/sbin/removef $PKGINST /opt/VirtualBox/etc/devlink.tab 1>/dev/null
/usr/sbin/removef $PKGINST /opt/VirtualBox/etc 1>/dev/null
rm -rf /opt/VirtualBox/etc
/usr/sbin/removef -f $PKGINST

/usr/sbin/installf -f $PKGINST

# We need to touch the desktop link inorder to add it to the menu right away
if test "$currentzone" = "global"; then
    if test -f "/usr/share/applications/virtualbox.desktop"; then
        touch /usr/share/applications/virtualbox.desktop
    fi

    # create /dev link for vboxdrv (only possible from global zone)
    /usr/sbin/devfsadm -i vboxdrv
fi

echo "Done."

# return 20 = requires reboot, 2 = partial failure, 0  = success
exit 0

