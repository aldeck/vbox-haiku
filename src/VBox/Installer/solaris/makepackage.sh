#!/bin/sh
# Sun xVM VirtualBox
# VirtualBox Solaris package creation script.
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

#
# Usage:
#       makespackage.sh $(PATH_TARGET)/install packagename $(KBUILD_TARGET_ARCH) [VBIPackageName]

if [ -z "$3" ]; then
    echo "Usage: $0 installdir packagename x86|amd64"
    echo "-- packagename must not have any extension (e.g. VirtualBox-SunOS-amd64-r28899)"
    exit 1
fi

VBOX_PKGFILE=$2.pkg
VBOX_ARCHIVE=$2.tar.gz
VBOX_PKGNAME=SUNWvbox

VBOX_GGREP=/usr/sfw/bin/ggrep
VBOX_AWK=/usr/bin/awk
VBOX_GTAR=/usr/sfw/bin/gtar

# check for GNU grep we use which might not ship with all Solaris
if test ! -f "$VBOX_GGREP" && test ! -h "$VBOX_GGREP"; then
    echo "## GNU grep not found in $VBOX_GGREP."
    exit 1
fi

# check for GNU tar we use which might not ship with all Solaris
if test ! -f "$VBOX_GTAR" && test ! -h "$VBOX_GTAR"; then
    echo "## GNU tar not found in $VBOX_GTAR."
    exit 1
fi

# bail out on non-zero exit status
set -e

# Fixup filelist using awk, the parameters must be in awk syntax
# params: filename condition action
filelist_fixup()
{
  "$VBOX_AWK" 'NF == 6 && '"$2"' { '"$3"' } { print }' "$1" > "tmp-$1"
  mv -f "tmp-$1" "$1"
}

# prepare file list
cd "$1"
echo 'i pkginfo=./vbox.pkginfo' > prototype
echo 'i postinstall=./postinstall.sh' >> prototype
echo 'i preremove=./preremove.sh' >> prototype
echo 'i space=./vbox.space' >> prototype
if test -f "./vbox.copyright"; then
    echo 'i copyright=./vbox.copyright' >> prototype
fi
echo 'e sed /etc/devlink.tab ? ? ?' >> prototype
find . -print | $VBOX_GGREP -v -E 'prototype|makepackage.sh|vbox.pkginfo|postinstall.sh|preremove.sh|ReadMe.txt|vbox.space|vbox.copyright|VirtualBoxKern' | pkgproto >> prototype

# don't grok for the sed class files
filelist_fixup prototype '$2 == "none"'                                                                 '$5 = "root"; $6 = "bin"'
filelist_fixup prototype '$2 == "none"'                                                                 '$3 = "opt/VirtualBox/"$3"="$3'

# install the kernel module to the right place.
if test "$3" = "x86"; then
    filelist_fixup prototype '$3 == "opt/VirtualBox/vboxdrv=vboxdrv"                                    '$3 = "platform/i86pc/kernel/drv/vboxdrv=vboxdrv"; $6 = "sys"'
else
    filelist_fixup prototype '$3 == "opt/VirtualBox/vboxdrv=vboxdrv"'                                   '$3 = "platform/i86pc/kernel/drv/amd64/vboxdrv=vboxdrv"; $6 = "sys"'
fi

filelist_fixup prototype '$3 == "opt/VirtualBox/vboxdrv.conf=vboxdrv.conf"'                             '$3 = "platform/i86pc/kernel/drv/vboxdrv.conf=vboxdrv.conf"'

# desktop links and icons
filelist_fixup prototype '$3 == "opt/VirtualBox/virtualbox.desktop=virtualbox.desktop"'                 '$3 = "usr/share/applications/virtualbox.desktop=virtualbox.desktop"'
filelist_fixup prototype '$3 == "opt/VirtualBox/VBox.png=VBox.png"'                                     '$3 = "usr/share/pixmaps/VBox.png=VBox.png"'

# webservice SMF manifest
filelist_fixup prototype '$3 == "opt/VirtualBox/virtualbox-webservice.xml=virtualbox-webservice.xml"'   '$3 = "var/svc/manifest/application/virtualbox/webservice.xml=virtualbox-webservice.xml'

# explicitly set timestamp to shutup warning
VBOXPKG_TIMESTAMP=vbox`date '+%Y%m%d%H%M%S'`

# create the package instance
pkgmk -p $VBOXPKG_TIMESTAMP -o -r .

# translate into package datastream
pkgtrans -s -o /var/spool/pkg "`pwd`/$VBOX_PKGFILE" "$VBOX_PKGNAME"

# $4 if exist would contain the path to the VBI package to include in the .tar.gz
if test -f "$4"; then
    $VBOX_GTAR zcvf "$VBOX_ARCHIVE" "$VBOX_PKGFILE" "$4" autoresponse ReadMe.txt
else
    $VBOX_GTAR zcvf "$VBOX_ARCHIVE" "$VBOX_PKGFILE" autoresponse ReadMe.txt
fi

echo "## Packaging and transfer completed successfully!"
rm -rf "/var/spool/pkg/$VBOX_PKGNAME"

exit $?

