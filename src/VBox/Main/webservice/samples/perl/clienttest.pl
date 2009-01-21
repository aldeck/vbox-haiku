#!/usr/bin/perl

#
# This little perl program attempts to connect to a running VirtualBox
# webservice and calls various methods on it.
#
# To get this to run:
#
# 0) If not yet among perl's modules, install SOAP::Lite. Users of debian
#    based systems might try 'sudo apt-get install libsoap-lite-perl'.
#
# 1) In this directory, run
#    stubmaker file:///path/to/sdk/bindings/webservice/vboxwebService.wsdl
#    Note: the command is named stubmaker.pl on some systems.
#    stubmaker should be installed on your system if you have SOAP::Lite and
#    will, after a little while of thinking, create a vboxService.pm
#    file in the current directory, which the "use" statement below
#    then includes.
#
#    (SOAP::Lite supports parsing the WSDL file on every run of
#    the script, but it takes up to a minute to do so, hence the external
#    variant via stubmaker.pl here.)
#
# 2) Start vboxwebsrv.
#
# 3) Run this script.
#
#
# Copyright (C) 2006-2009 Sun Microsystems, Inc.
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

use strict;
use SOAP::Lite;
use vboxService;
use Data::Dumper;

my $cmd = 'clienttest';
my $optMode;
my $vmname;

while (my $this = shift(@ARGV))
{
    if (($this =~ /^-h/) || ($this =~ /^--help/))
    {
        print "$cmd: test the VirtualBox web service.\n".
              "Usage:\n".
              "    $cmd <mode>\n".
              "with <mode> being one of 'version', 'list', 'start'; default is 'list'.\n".
              "    $cmd version: print version of VirtualBox web service.\n".
              "    $cmd list: list installed virtual machines.\n".
              "    $cmd startvm <vm>: start the virtual machine named <vm>.\n";
        exit 0;
    }
    elsif (    ($this eq 'version')
            || ($this eq 'list')
          )
    {
        $optMode = $this;
    }
    elsif ($this eq 'startvm')
    {
        $optMode = $this;

        if (!($vmname = shift(@ARGV)))
        {
            die "[$cmd] Missing parameter: You must specify the name of the VM to start.\nStopped";
        }
    }
    else
    {
        die "[$cmd] Unknown option \"$this\"; stopped";
    }
}

$optMode = "list"
    if (!$optMode);

my $vbox = vboxService->IWebsessionManager_logon("test", "test");

if (!$vbox)
{
    die "[$cmd] Logon to session manager with user \"test\" and password \"test\" failed.\nStopped";
}

if ($optMode eq "version")
{
    my $v = vboxService->IVirtualBox_getVersion($vbox);
    print "[$cmd] Version number of running VirtualBox web service: $v\n";
}
elsif ($optMode eq "list")
{
    print "[$cmd] Listing machines:\n";
    my $result = vboxService->IVirtualBox_getMachines($vbox);
    foreach my $idMachine (@{$result->{'array'}})
    {
        my $if = vboxService->IManagedObjectRef_getInterfaceName($idMachine);
        my $name = vboxService->IMachine_getName($idMachine);

        print "machine $if $idMachine: $name\n";
    }
}
elsif ($optMode eq "startvm")
{
    # assume it's a UUID
    my $machine = vboxService->IVirtualBox_getMachine($vbox, $vmname);
    if (!$machine)
    {
        # no: then try a name
        $machine = vboxService->IVirtualBox_findMachine($vbox, $vmname);
    }

    die "[$cmd] Cannot find VM \"$vmname\"; stopped"
        if (!$machine);

    my $session = vboxService->IWebsessionManager_getSessionObject($vbox);
    die "[$cmd] Cannot get session object; stopped"
        if (!$session);

    my $uuid = vboxService->IMachine_getId($machine);
    die "[$cmd] Cannot get uuid for machine; stopped"
        if (!$uuid);

    print "[$cmd] UUID: $uuid\n";

    my $progress = vboxService->IVirtualBox_openRemoteSession($vbox,
                                                              $session,
                                                              $uuid,
                                                              "vrdp",
                                                              "");
    die "[$cmd] Cannot open remote session; stopped"
        if (!$progress);

    print("[$cmd] Waiting for the remote session to open...\n");
    vboxService->IProgress_waitForCompletion($progress, -1);

    my $fCompleted;
    $fCompleted = vboxService->IProgress_getCompleted($progress);
    print("[$cmd] Completed: $fCompleted\n");

    my $resultCode;
    $resultCode = vboxService->IProgress_getResultCode($progress);

    print("[$cmd] Result: $resultCode\n");

    vboxService->ISession_close($session);

    vboxService->IWebsessionManager_logoff($vbox);
}
