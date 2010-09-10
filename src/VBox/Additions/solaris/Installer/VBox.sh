#!/bin/sh
#
# VirtualBox startup script for Solaris Guests Additions
#
# Copyright (C) 2008-2010 Oracle Corporation
#
# This file is part of VirtualBox Open Source Edition (OSE), as
# available from http://www.virtualbox.org. This file is free software;
# you can redistribute it and/or modify it under the terms of the GNU
# General Public License (GPL) as published by the Free Software
# Foundation, in version 2 as it comes in the "COPYING" file of the
# VirtualBox OSE distribution. VirtualBox OSE is distributed in the
# hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
#

CPUTYPE=`isainfo -k`
ISADIR=""
if test "$CPUTYPE" = "amd64"; then
    ISADIR="amd64"
fi

INSTALL_DIR="/opt/VirtualBoxAdditions"
APP=`which $0`
APP=`basename $APP`
case "$APP" in
  VBoxClient)
    exec "$INSTALL_DIR/$ISADIR/VBoxClient" "$@"
  ;;
  VBoxService)
    exec "$INSTALL_DIR/$ISADIR/VBoxService" "$@"
  ;;
  VBoxControl)
    exec "$INSTALL_DIR/$ISADIR/VBoxControl" "$@"
  ;;
  *)
    echo "Unknown application - $APP"
  ;;
esac

