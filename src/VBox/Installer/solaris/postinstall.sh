#!/bin/sh
## @file
# Sun VirtualBox
# VirtualBox postinstall script for Solaris.
#

#
# Copyright (C) 2007-2009 Sun Microsystems, Inc.
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

install_python_bindings()
{
    PYTHONBIN=$1
    if test -x "$PYTHONBIN"; then
        VBOX_INSTALL_PATH=/opt/VirtualBox
        export VBOX_INSTALL_PATH
        cd /opt/VirtualBox/sdk/installer
        $PYTHONBIN ./vboxapisetup.py install > /dev/null
        return 0
    fi
    return 1
}

# Check for xVM/Xen
currentisa=`uname -i`
if test "$currentisa" = "i86xpv"; then
    echo "## VirtualBox cannot run under xVM Dom0! Fatal Error, Aborting installation!"
    exit 2
fi

osversion=`uname -r`
currentzone=`zonename`
if test "$currentzone" = "global"; then
    echo "Loading VirtualBox kernel modules..."
    /opt/VirtualBox/vboxdrv.sh stopall silentunload checkarch
    rc=$?
    if test "$rc" -eq 0; then
        /opt/VirtualBox/vboxdrv.sh start
        rc=$?

        # VBoxDrv loaded successfully, proceed with the rest...
        if test "$rc" -eq 0; then
            # Load VBoxNetAdapter vboxnet
            if test -f /platform/i86pc/kernel/drv/vboxnet.conf; then
                /opt/VirtualBox/vboxdrv.sh netstart
                rc=$?

                # nwam/dhcpagent fix
                nwamfile=/etc/nwam/llp
                nwambackupfile=$nwamfile.vbox
                if test "$rc" -eq 0 && test -f "$nwamfile"; then
                    sed -e '/vboxnet/d' $nwamfile > $nwambackupfile
                    echo "vboxnet0	static 192.168.56.1" >> $nwambackupfile
                    mv -f $nwambackupfile $nwamfile
                fi
            fi

            # Load VBoxNetFilter vboxflt
            if test -f /platform/i86pc/kernel/drv/vboxflt.conf; then
                /opt/VirtualBox/vboxdrv.sh fltstart
                rc=$?
            fi

            # Load VBoxUSBMon vboxusbmon (do NOT load for Solaris 10)
            if test -f /platform/i86pc/kernel/drv/vboxusbmon.conf && test "$osversion" != "5.10"; then
                /opt/VirtualBox/vboxdrv.sh usbstart
                rc=$?
                if test "$rc" -eq 0; then
                    # Add vboxusbmon to the devlink.tab
                    sed -e '/name=vboxusbmon/d' /etc/devlink.tab > /etc/devlink.vbox
                    echo "type=ddi_pseudo;name=vboxusbmon	\D" >> /etc/devlink.vbox
                    mv -f /etc/devlink.vbox /etc/devlink.tab
                fi
            fi
        fi
    fi

    # Fail on any errors while unloading previous modules because it makes it very hard to
    # track problems when older vboxdrv is hanging about in memory and add_drv of the new
    # one suceeds and it appears as though the new one is being used.
    if test "$rc" -ne 0; then
        echo "## Loading failed. Aborting installation."
        exit 2
    fi

    echo "Configuring services and drivers..."

    # Web service
    if test -f /var/svc/manifest/application/virtualbox/virtualbox-webservice.xml; then
        /usr/sbin/svccfg import /var/svc/manifest/application/virtualbox/virtualbox-webservice.xml
        /usr/sbin/svcadm disable -s svc:/application/virtualbox/webservice:default
    fi

    # Add vboxdrv to the devlink.tab
    sed -e '/name=vboxdrv/d' /etc/devlink.tab > /etc/devlink.vbox
    echo "type=ddi_pseudo;name=vboxdrv	\D" >> /etc/devlink.vbox
    mv -f /etc/devlink.vbox /etc/devlink.tab

    # Create the device link
    /usr/sbin/devfsadm -i vboxdrv

    # Don't create link for Solaris 10
    if test -f /platform/i86pc/kernel/drv/vboxusbmon.conf && test "$osversion" != "5.10"; then
        /usr/sbin/devfsadm -i vboxusbmon
    fi
    sync

    # We need to touch the desktop link in order to add it to the menu right away
    if test -f "/usr/share/applications/virtualbox.desktop"; then
        touch /usr/share/applications/virtualbox.desktop
    fi

    # Zone access service
    if test -f /var/svc/manifest/application/virtualbox/virtualbox-zoneaccess.xml; then
        /usr/sbin/svccfg import /var/svc/manifest/application/virtualbox/virtualbox-zoneaccess.xml
        /usr/sbin/svcadm enable -s svc:/application/virtualbox/zoneaccess
    fi

    # Install python bindings
    if test -f "/opt/VirtualBox/sdk/installer/vboxapisetup.py" || test -h "/opt/VirtualBox/sdk/installer/vboxapisetup.py"; then
        PYTHONBIN=`which python`
        if test -f "$PYTHONBIN" || test -h "$PYTHONBIN"; then
            echo "Installing Python bindings..."

            INSTALLEDIT=1
            PYTHONBIN=`which python2.4`
            install_python_bindings "$PYTHONBIN"
            if test "$?" -eq 0; then
                INSTALLEDIT=0
            fi
            PYTHONBIN=`which python2.5`
            install_python_bindings "$PYTHONBIN"
            if test "$?" -eq 0; then
                INSTALLEDIT=0
            fi
            PYTHONBIN=`which python2.6`
            install_python_bindings "$PYTHONBIN"
            if test "$?" -eq 0; then 
                INSTALLEDIT=0
            fi

            # remove files installed by Python build
            rm -rf /opt/VirtualBox/sdk/installer/build

            if test "$INSTALLEDIT" -ne 0; then
                echo "** No suitable Python version found. Requires Python 2.4, 2.5 or 2.6."
            fi
        else
            echo "** WARNING! Python not found, skipped installed Python bindings."
            echo "   Manually run '/opt/VirtualBox/sdk/installer/vboxapisetup.py install'"
            echo "   to install the bindings when python is available."
        fi
    fi

    # Update boot archive
    BOOTADMBIN=/sbin/bootadm
    if test -f "$BOOTADMBIN" || test -h "$BOOTADMBIN"; then
        echo "Updating boot archive..."
        $BOOTADMBIN update-archive > /dev/null
    fi
fi

/usr/sbin/installf -f $PKGINST

echo "Done."

# return 20 = requires reboot, 2 = partial failure, 0  = success
exit 0

