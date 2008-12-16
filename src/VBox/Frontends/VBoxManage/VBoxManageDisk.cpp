/* $Id$ */
/** @file
 * VBoxManage - The disk delated commands.
 */

/*
 * Copyright (C) 2006-2008 Sun Microsystems, Inc.
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa
 * Clara, CA 95054 USA or visit http://www.sun.com if you need
 * additional information or have any questions.
 */

#ifndef VBOX_ONLY_DOCS

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include <VBox/com/com.h>
#include <VBox/com/array.h>
#include <VBox/com/ErrorInfo.h>
#include <VBox/com/VirtualBox.h>

#include <iprt/asm.h>
#include <iprt/file.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <VBox/log.h>
#include <VBox/VBoxHDD-new.h>

#include "VBoxManage.h"
using namespace com;


// funcs
///////////////////////////////////////////////////////////////////////////////


static DECLCALLBACK(void) handleVDError(void *pvUser, int rc, RT_SRC_POS_DECL, const char *pszFormat, va_list va)
{
    RTPrintf("ERROR: ");
    RTPrintfV(pszFormat, va);
    RTPrintf("\n");
    RTPrintf("Error code %Rrc at %s(%u) in function %s\n", rc, RT_SRC_POS_ARGS);
}


int handleCreateHardDisk(int argc, char *argv[],
                         ComPtr<IVirtualBox> virtualBox, ComPtr<ISession> session)
{
    HRESULT rc;
    Bstr filename;
    uint64_t sizeMB = 0;
    Bstr format = "VDI";
    bool fStatic = false;
    Bstr comment;
    bool fRegister = false;
    const char *type = "normal";

    /* let's have a closer look at the arguments */
    for (int i = 0; i < argc; i++)
    {
        if (strcmp(argv[i], "-filename") == 0)
        {
            if (argc <= i + 1)
                return errorArgument("Missing argument to '%s'", argv[i]);
            i++;
            filename = argv[i];
        }
        else if (strcmp(argv[i], "-size") == 0)
        {
            if (argc <= i + 1)
                return errorArgument("Missing argument to '%s'", argv[i]);
            i++;
            sizeMB = RTStrToUInt64(argv[i]);
        }
        else if (strcmp(argv[i], "-format") == 0)
        {
            if (argc <= i + 1)
                return errorArgument("Missing argument to '%s'", argv[i]);
            i++;
            format = argv[i];
        }
        else if (strcmp(argv[i], "-static") == 0)
        {
            fStatic = true;
        }
        else if (strcmp(argv[i], "-comment") == 0)
        {
            if (argc <= i + 1)
                return errorArgument("Missing argument to '%s'", argv[i]);
            i++;
            comment = argv[i];
        }
        else if (strcmp(argv[i], "-register") == 0)
        {
            fRegister = true;
        }
        else if (strcmp(argv[i], "-type") == 0)
        {
            if (argc <= i + 1)
                return errorArgument("Missing argument to '%s'", argv[i]);
            i++;
            type = argv[i];
        }
        else
            return errorSyntax(USAGE_CREATEHD, "Invalid parameter '%s'", Utf8Str(argv[i]).raw());
    }
    /* check the outcome */
    if (!filename || (sizeMB == 0))
        return errorSyntax(USAGE_CREATEHD, "Parameters -filename and -size are required");

    if (strcmp(type, "normal") && strcmp(type, "writethrough"))
        return errorArgument("Invalid hard disk type '%s' specified", Utf8Str(type).raw());

    ComPtr<IHardDisk2> hardDisk;
    CHECK_ERROR(virtualBox, CreateHardDisk2(format, filename, hardDisk.asOutParam()));
    if (SUCCEEDED(rc) && hardDisk)
    {
        /* we will close the hard disk after the storage has been successfully
         * created unless fRegister is set */
        bool doClose = false;

        if (!comment.isNull())
        {
            CHECK_ERROR(hardDisk,COMSETTER(Description)(comment));
        }
        ComPtr<IProgress> progress;
        if (fStatic)
        {
            CHECK_ERROR(hardDisk, CreateFixedStorage(sizeMB, progress.asOutParam()));
        }
        else
        {
            CHECK_ERROR(hardDisk, CreateDynamicStorage(sizeMB, progress.asOutParam()));
        }
        if (SUCCEEDED(rc) && progress)
        {
            if (fStatic)
                showProgress(progress);
            else
                CHECK_ERROR(progress, WaitForCompletion(-1));
            if (SUCCEEDED(rc))
            {
                progress->COMGETTER(ResultCode)(&rc);
                if (FAILED(rc))
                {
                    com::ProgressErrorInfo info(progress);
                    if (info.isBasicAvailable())
                        RTPrintf("Error: failed to create hard disk. Error message: %lS\n", info.getText().raw());
                    else
                        RTPrintf("Error: failed to create hard disk. No error message available!\n");
                }
                else
                {
                    doClose = !fRegister;

                    Guid uuid;
                    CHECK_ERROR(hardDisk, COMGETTER(Id)(uuid.asOutParam()));

                    if (strcmp(type, "normal") == 0)
                    {
                        /* nothing required, default */
                    }
                    else if (strcmp(type, "writethrough") == 0)
                    {
                        CHECK_ERROR(hardDisk, COMSETTER(Type)(HardDiskType_Writethrough));
                    }

                    RTPrintf("Disk image created. UUID: %s\n", uuid.toString().raw());
                }
            }
        }
        if (doClose)
        {
            CHECK_ERROR(hardDisk, Close());
        }
    }
    return SUCCEEDED(rc) ? 0 : 1;
}

#if 0 /* disabled until disk shrinking is implemented based on VBoxHDD-new */
static DECLCALLBACK(int) hardDiskProgressCallback(PVM pVM, unsigned uPercent, void *pvUser)
{
    unsigned *pPercent = (unsigned *)pvUser;

    if (*pPercent != uPercent)
    {
        *pPercent = uPercent;
        RTPrintf(".");
        if ((uPercent % 10) == 0 && uPercent)
            RTPrintf("%d%%", uPercent);
        RTStrmFlush(g_pStdOut);
    }

    return VINF_SUCCESS;
}
#endif


int handleModifyHardDisk(int argc, char *argv[],
                         ComPtr<IVirtualBox> virtualBox, ComPtr<ISession> session)
{
    HRESULT rc;

    /* The uuid/filename and a command */
    if (argc < 2)
        return errorSyntax(USAGE_MODIFYHD, "Incorrect number of parameters");

    ComPtr<IHardDisk2> hardDisk;
    Bstr filepath;

    /* first guess is that it's a UUID */
    Guid uuid(argv[0]);
    rc = virtualBox->GetHardDisk2(uuid, hardDisk.asOutParam());
    /* no? then it must be a filename */
    if (!hardDisk)
    {
        filepath = argv[0];
        CHECK_ERROR(virtualBox, FindHardDisk2(filepath, hardDisk.asOutParam()));
    }

    /* let's find out which command */
    if (strcmp(argv[1], "settype") == 0)
    {
        /* hard disk must be registered */
        if (SUCCEEDED(rc) && hardDisk)
        {
            char *type = NULL;

            if (argc <= 2)
                return errorArgument("Missing argument to for settype");

            type = argv[2];

            HardDiskType_T hddType;
            CHECK_ERROR(hardDisk, COMGETTER(Type)(&hddType));

            if (strcmp(type, "normal") == 0)
            {
                if (hddType != HardDiskType_Normal)
                    CHECK_ERROR(hardDisk, COMSETTER(Type)(HardDiskType_Normal));
            }
            else if (strcmp(type, "writethrough") == 0)
            {
                if (hddType != HardDiskType_Writethrough)
                    CHECK_ERROR(hardDisk, COMSETTER(Type)(HardDiskType_Writethrough));

            }
            else if (strcmp(type, "immutable") == 0)
            {
                if (hddType != HardDiskType_Immutable)
                    CHECK_ERROR(hardDisk, COMSETTER(Type)(HardDiskType_Immutable));
            }
            else
            {
                return errorArgument("Invalid hard disk type '%s' specified", Utf8Str(type).raw());
            }
        }
        else
            return errorArgument("Hard disk image not registered");
    }
    else if (strcmp(argv[1], "compact") == 0)
    {
#if 1
        RTPrintf("Error: Shrink hard disk operation is temporarily unavailable!\n");
        return 1;
#else
        /* the hard disk image might not be registered */
        if (!hardDisk)
        {
            virtualBox->OpenHardDisk2(Bstr(argv[0]), hardDisk.asOutParam());
            if (!hardDisk)
                return errorArgument("Hard disk image not found");
        }

        Bstr format;
        hardDisk->COMGETTER(Format)(format.asOutParam());
        if (format != "VDI")
            return errorArgument("Invalid hard disk type. The command only works on VDI files\n");

        Bstr fileName;
        hardDisk->COMGETTER(Location)(fileName.asOutParam());

        /* make sure the object reference is released */
        hardDisk = NULL;

        unsigned uProcent;

        RTPrintf("Shrinking '%lS': 0%%", fileName.raw());
        int vrc = VDIShrinkImage(Utf8Str(fileName).raw(), hardDiskProgressCallback, &uProcent);
        if (RT_FAILURE(vrc))
        {
            RTPrintf("Error while shrinking hard disk image: %Rrc\n", vrc);
            rc = E_FAIL;
        }
#endif
    }
    else
        return errorSyntax(USAGE_MODIFYHD, "Invalid parameter '%s'", Utf8Str(argv[1]).raw());

    return SUCCEEDED(rc) ? 0 : 1;
}

int handleCloneHardDisk(int argc, char *argv[],
                        ComPtr<IVirtualBox> virtualBox, ComPtr<ISession> session)
{
    Bstr src, dst;
    Bstr format;
    bool remember = false;

    HRESULT rc;

    /* Parse the arguments. */
    for (int i = 0; i < argc; i++)
    {
        if (strcmp(argv[i], "-format") == 0)
        {
            if (argc <= i + 1)
            {
                return errorArgument("Missing argument to '%s'", argv[i]);
            }
            i++;
            format = argv[i];
        }
        else if (strcmp(argv[i], "-remember") == 0 ||
                 strcmp(argv[i], "-register") == 0 /* backward compatiblity */)
        {
            remember = true;
        }
        else if (src.isEmpty())
        {
            src = argv[i];
        }
        else if (dst.isEmpty())
        {
            dst = argv[i];
        }
        else
        {
            return errorSyntax(USAGE_CLONEHD, "Invalid parameter '%s'", Utf8Str(argv[i]).raw());
        }
    }

    if (src.isEmpty())
        return errorSyntax(USAGE_CLONEHD, "Mandatory UUID or input file parameter missing");
    if (dst.isEmpty())
        return errorSyntax(USAGE_CLONEHD, "Mandatory output file parameter missing");

    ComPtr<IHardDisk2> srcDisk;
    ComPtr<IHardDisk2> dstDisk;
    bool unknown = false;

    /* first guess is that it's a UUID */
    Guid uuid(Utf8Str(src).raw());
    rc = virtualBox->GetHardDisk2(uuid, srcDisk.asOutParam());
    /* no? then it must be a filename */
    if (FAILED (rc))
    {
        rc = virtualBox->FindHardDisk2(src, srcDisk.asOutParam());
        /* no? well, then it's an unkwnown image */
        if (FAILED (rc))
        {
            CHECK_ERROR(virtualBox, OpenHardDisk2(src, srcDisk.asOutParam()));
            if (SUCCEEDED (rc))
            {
                unknown = true;
            }
        }
    }

    do
    {
        if (!SUCCEEDED(rc))
            break;

        if (format.isEmpty())
        {
            /* get the format of the source hard disk */
            CHECK_ERROR_BREAK(srcDisk, COMGETTER(Format) (format.asOutParam()));
        }

        CHECK_ERROR_BREAK(virtualBox, CreateHardDisk2(format, dst, dstDisk.asOutParam()));

        ComPtr<IProgress> progress;
        CHECK_ERROR_BREAK(srcDisk, CloneTo(dstDisk, progress.asOutParam()));

        showProgress(progress);
        progress->COMGETTER(ResultCode)(&rc);
        if (FAILED(rc))
        {
            com::ProgressErrorInfo info(progress);
            if (info.isBasicAvailable())
                RTPrintf("Error: failed to clone hard disk. Error message: %lS\n", info.getText().raw());
            else
                RTPrintf("Error: failed to clone hard disk. No error message available!\n");
            break;
        }

        CHECK_ERROR_BREAK(dstDisk, COMGETTER(Id)(uuid.asOutParam()));

        RTPrintf("Clone hard disk created in format '%ls'. UUID: %s\n",
                 format.raw(), uuid.toString().raw());
    }
    while (0);

    if (!remember && !dstDisk.isNull())
    {
        /* forget the created clone */
        dstDisk->Close();
    }

    if (unknown)
    {
        /* close the unknown hard disk to forget it again */
        srcDisk->Close();
    }

    return SUCCEEDED(rc) ? 0 : 1;
}

int handleConvertHardDisk(int argc, char **argv)
{
    Bstr srcformat;
    Bstr dstformat;
    Bstr src;
    Bstr dst;
    int vrc;
    PVBOXHDD pSrcDisk = NULL;
    PVBOXHDD pDstDisk = NULL;

    /* Parse the arguments. */
    for (int i = 0; i < argc; i++)
    {
        if (strcmp(argv[i], "-srcformat") == 0)
        {
            if (argc <= i + 1)
            {
                return errorArgument("Missing argument to '%s'", argv[i]);
            }
            i++;
            srcformat = argv[i];
        }
        else if (strcmp(argv[i], "-dstformat") == 0)
        {
            if (argc <= i + 1)
            {
                return errorArgument("Missing argument to '%s'", argv[i]);
            }
            i++;
            dstformat = argv[i];
        }
        else if (src.isEmpty())
        {
            src = argv[i];
        }
        else if (dst.isEmpty())
        {
            dst = argv[i];
        }
        else
        {
            return errorSyntax(USAGE_CONVERTHD, "Invalid parameter '%s'", Utf8Str(argv[i]).raw());
        }
    }

    if (src.isEmpty())
        return errorSyntax(USAGE_CONVERTHD, "Mandatory input image parameter missing");
    if (dst.isEmpty())
        return errorSyntax(USAGE_CONVERTHD, "Mandatory output image parameter missing");


    PVDINTERFACE     pVDIfs = NULL;
    VDINTERFACE      vdInterfaceError;
    VDINTERFACEERROR vdInterfaceErrorCallbacks;
    vdInterfaceErrorCallbacks.cbSize       = sizeof(VDINTERFACEERROR);
    vdInterfaceErrorCallbacks.enmInterface = VDINTERFACETYPE_ERROR;
    vdInterfaceErrorCallbacks.pfnError     = handleVDError;

    vrc = VDInterfaceAdd(&vdInterfaceError, "VBoxManage_IError", VDINTERFACETYPE_ERROR,
                         &vdInterfaceErrorCallbacks, NULL, &pVDIfs);
    AssertRC(vrc);

    do
    {
        /* Try to determine input image format */
        if (srcformat.isEmpty())
        {
            char *pszFormat = NULL;
            vrc = VDGetFormat(Utf8Str(src).raw(), &pszFormat);
            if (RT_FAILURE(vrc))
            {
                RTPrintf("No file format specified and autodetect failed - please specify format: %Rrc\n", vrc);
                break;
            }
            srcformat = pszFormat;
            RTStrFree(pszFormat);
        }

        vrc = VDCreate(pVDIfs, &pSrcDisk);
        if (RT_FAILURE(vrc))
        {
            RTPrintf("Error while creating the source virtual disk container: %Rrc\n", vrc);
            break;
        }

        /* Open the input image */
        vrc = VDOpen(pSrcDisk, Utf8Str(srcformat).raw(), Utf8Str(src).raw(), VD_OPEN_FLAGS_READONLY, NULL);
        if (RT_FAILURE(vrc))
        {
            RTPrintf("Error while opening the source image: %Rrc\n", vrc);
            break;
        }

        /* Output format defaults to VDI */
        if (dstformat.isEmpty())
            dstformat = "VDI";

        vrc = VDCreate(pVDIfs, &pDstDisk);
        if (RT_FAILURE(vrc))
        {
            RTPrintf("Error while creating the destination virtual disk container: %Rrc\n", vrc);
            break;
        }

        uint64_t cbSize = VDGetSize(pSrcDisk, VD_LAST_IMAGE);
        RTPrintf("Converting image \"%s\" with size %RU64 bytes (%RU64MB)...\n", Utf8Str(src).raw(), cbSize, (cbSize + _1M - 1) / _1M);

        /* Create the output image */
        vrc = VDCopy(pSrcDisk, VD_LAST_IMAGE, pDstDisk, Utf8Str(dstformat).raw(),
                     Utf8Str(dst).raw(), false, 0, NULL, NULL, NULL, NULL);
        if (RT_FAILURE(vrc))
        {
            RTPrintf("Error while copying the image: %Rrc\n", vrc);
            break;
        }
    }
    while (0);
    if (pDstDisk)
        VDCloseAll(pDstDisk);
    if (pSrcDisk)
        VDCloseAll(pSrcDisk);

    return RT_SUCCESS(vrc) ? 0 : 1;
}


int handleConvertDDImage(int argc, char *argv[])
{
    VDIMAGETYPE enmImgType = VD_IMAGE_TYPE_NORMAL;
    bool fReadFromStdIn = false;
    const char *format = NULL;
    const char *srcfilename = NULL;
    const char *dstfilename = NULL;
    const char *filesize = NULL;
    unsigned uImageFlags = 0; /**< @todo allow creation of non-default image variants */
    void *pvBuf = NULL;

    for (int i = 0; i < argc; i++)
    {
        if (!strcmp(argv[i], "-static"))
        {
            enmImgType = VD_IMAGE_TYPE_FIXED;
        }
        else if (strcmp(argv[i], "-format") == 0)
        {
            if (argc <= i + 1)
            {
                return errorArgument("Missing argument to '%s'", argv[i]);
            }
            i++;
            format = argv[i];
        }
        else
        {
            if (srcfilename)
            {
                if (dstfilename)
                {
                    if (fReadFromStdIn && !filesize)
                        filesize = argv[i];
                    else
                        return errorSyntax(USAGE_CONVERTDD, "Incorrect number of parameters");
                }
                else
                    dstfilename = argv[i];
            }
            else
            {
                srcfilename = argv[i];
#if defined(RT_OS_LINUX) || defined(RT_OS_DARWIN) || defined(RT_OS_SOLARIS)
                fReadFromStdIn = !strcmp(srcfilename, "stdin");
#endif
            }
        }
    }

    RTPrintf("Converting VDI: from DD image file=\"%s\" to file=\"%s\"...\n",
             srcfilename, dstfilename);

    int rc = VINF_SUCCESS;
    PVBOXHDD pDisk = NULL;

    PVDINTERFACE     pVDIfs = NULL;
    VDINTERFACE      vdInterfaceError;
    VDINTERFACEERROR vdInterfaceErrorCallbacks;
    vdInterfaceErrorCallbacks.cbSize       = sizeof(VDINTERFACEERROR);
    vdInterfaceErrorCallbacks.enmInterface = VDINTERFACETYPE_ERROR;
    vdInterfaceErrorCallbacks.pfnError     = handleVDError;

    rc = VDInterfaceAdd(&vdInterfaceError, "VBoxManage_IError", VDINTERFACETYPE_ERROR,
                        &vdInterfaceErrorCallbacks, NULL, &pVDIfs);
    AssertRC(rc);

    /* open raw image file. */
    RTFILE File;
    if (fReadFromStdIn)
        File = 0;
    else
        rc = RTFileOpen(&File, srcfilename, RTFILE_O_OPEN | RTFILE_O_READ | RTFILE_O_DENY_WRITE);
    if (RT_FAILURE(rc))
    {
        RTPrintf("File=\"%s\" open error: %Rrf\n", srcfilename, rc);
        goto out;
    }

    uint64_t cbFile;
    /* get image size. */
    if (fReadFromStdIn)
        cbFile = RTStrToUInt64(filesize);
    else
        rc = RTFileGetSize(File, &cbFile);
    if (RT_FAILURE(rc))
    {
        RTPrintf("Error getting image size for file \"%s\": %Rrc\n", srcfilename, rc);
        goto out;
    }

    RTPrintf("Creating %s image with size %RU64 bytes (%RU64MB)...\n", (enmImgType == VD_IMAGE_TYPE_FIXED) ? "fixed" : "dynamic", cbFile, (cbFile + _1M - 1) / _1M);
    char pszComment[256];
    RTStrPrintf(pszComment, sizeof(pszComment), "Converted image from %s", srcfilename);
    rc = VDCreate(pVDIfs, &pDisk);
    if (RT_FAILURE(rc))
    {
        RTPrintf("Error while creating the virtual disk container: %Rrc\n", rc);
        goto out;
    }

    Assert(RT_MIN(cbFile / 512 / 16 / 63, 16383) -
           (unsigned int)RT_MIN(cbFile / 512 / 16 / 63, 16383) == 0);
    PDMMEDIAGEOMETRY PCHS, LCHS;
    PCHS.cCylinders = (unsigned int)RT_MIN(cbFile / 512 / 16 / 63, 16383);
    PCHS.cHeads = 16;
    PCHS.cSectors = 63;
    LCHS.cCylinders = 0;
    LCHS.cHeads = 0;
    LCHS.cSectors = 0;
    rc = VDCreateBase(pDisk, format, dstfilename, enmImgType, cbFile,
                      uImageFlags, pszComment, &PCHS, &LCHS, NULL,
                      VD_OPEN_FLAGS_NORMAL, NULL, NULL);
    if (RT_FAILURE(rc))
    {
        RTPrintf("Error while creating the disk image \"%s\": %Rrc\n", dstfilename, rc);
        goto out;
    }

    size_t cbBuffer;
    cbBuffer = _1M;
    pvBuf = RTMemAlloc(cbBuffer);
    if (!pvBuf)
    {
        rc = VERR_NO_MEMORY;
        RTPrintf("Not enough memory allocating buffers for image \"%s\": %Rrc\n", dstfilename, rc);
        goto out;
    }

    uint64_t offFile;
    offFile = 0;
    while (offFile < cbFile)
    {
        size_t cbRead;
        size_t cbToRead;
        cbRead = 0;
        cbToRead = cbFile - offFile >= (uint64_t)cbBuffer ?
                            cbBuffer : (size_t) (cbFile - offFile);
        rc = RTFileRead(File, pvBuf, cbToRead, &cbRead);
        if (RT_FAILURE(rc) || !cbRead)
            break;
        rc = VDWrite(pDisk, offFile, pvBuf, cbRead);
        if (RT_FAILURE(rc))
        {
            RTPrintf("Failed to write to disk image \"%s\": %Rrc\n", dstfilename, rc);
            goto out;
        }
        offFile += cbRead;
    }

out:
    if (pvBuf)
        RTMemFree(pvBuf);
    if (pDisk)
        VDClose(pDisk, RT_FAILURE(rc));
    if (File != NIL_RTFILE)
        RTFileClose(File);

    return RT_FAILURE(rc);
}

int handleAddiSCSIDisk(int argc, char *argv[],
                       ComPtr <IVirtualBox> aVirtualBox, ComPtr<ISession> aSession)
{
    HRESULT rc;
    Bstr server;
    Bstr target;
    Bstr port;
    Bstr lun;
    Bstr username;
    Bstr password;
    Bstr comment;
    bool fIntNet = false;

    /* at least server and target */
    if (argc < 4)
        return errorSyntax(USAGE_ADDISCSIDISK, "Not enough parameters");

    /* let's have a closer look at the arguments */
    for (int i = 0; i < argc; i++)
    {
        if (strcmp(argv[i], "-server") == 0)
        {
            if (argc <= i + 1)
                return errorArgument("Missing argument to '%s'", argv[i]);
            i++;
            server = argv[i];
        }
        else if (strcmp(argv[i], "-target") == 0)
        {
            if (argc <= i + 1)
                return errorArgument("Missing argument to '%s'", argv[i]);
            i++;
            target = argv[i];
        }
        else if (strcmp(argv[i], "-port") == 0)
        {
            if (argc <= i + 1)
                return errorArgument("Missing argument to '%s'", argv[i]);
            i++;
            port = argv[i];
        }
        else if (strcmp(argv[i], "-lun") == 0)
        {
            if (argc <= i + 1)
                return errorArgument("Missing argument to '%s'", argv[i]);
            i++;
            lun = argv[i];
        }
        else if (strcmp(argv[i], "-encodedlun") == 0)
        {
            if (argc <= i + 1)
                return errorArgument("Missing argument to '%s'", argv[i]);
            i++;
            lun = BstrFmt("enc%s", argv[i]);
        }
        else if (strcmp(argv[i], "-username") == 0)
        {
            if (argc <= i + 1)
                return errorArgument("Missing argument to '%s'", argv[i]);
            i++;
            username = argv[i];
        }
        else if (strcmp(argv[i], "-password") == 0)
        {
            if (argc <= i + 1)
                return errorArgument("Missing argument to '%s'", argv[i]);
            i++;
            password = argv[i];
        }
        else if (strcmp(argv[i], "-comment") == 0)
        {
            if (argc <= i + 1)
                return errorArgument("Missing argument to '%s'", argv[i]);
            i++;
            comment = argv[i];
        }
        else if (strcmp(argv[i], "-intnet") == 0)
        {
            i++;
            fIntNet = true;
        }
        else
            return errorSyntax(USAGE_ADDISCSIDISK, "Invalid parameter '%s'", Utf8Str(argv[i]).raw());
    }

    /* check for required options */
    if (!server || !target)
        return errorSyntax(USAGE_ADDISCSIDISK, "Parameters -server and -target are required");

    do
    {
        ComPtr<IHardDisk2> hardDisk;
        CHECK_ERROR_BREAK (aVirtualBox,
            CreateHardDisk2(Bstr ("iSCSI"),
                            BstrFmt ("%ls/%ls", server.raw(), target.raw()),
                            hardDisk.asOutParam()));
        CheckComRCBreakRC (rc);

        if (!comment.isNull())
            CHECK_ERROR_BREAK(hardDisk, COMSETTER(Description)(comment));

        if (!port.isNull())
            server = BstrFmt ("%ls:%ls", server.raw(), port.raw());

        com::SafeArray <BSTR> names;
        com::SafeArray <BSTR> values;

        Bstr ("TargetAddress").detachTo (names.appendedRaw());
        server.detachTo (values.appendedRaw());
        Bstr ("TargetName").detachTo (names.appendedRaw());
        target.detachTo (values.appendedRaw());

        if (!lun.isNull())
        {
            Bstr ("LUN").detachTo (names.appendedRaw());
            lun.detachTo (values.appendedRaw());
        }
        if (!username.isNull())
        {
            Bstr ("InitiatorUsername").detachTo (names.appendedRaw());
            username.detachTo (values.appendedRaw());
        }
        if (!password.isNull())
        {
            Bstr ("InitiatorSecret").detachTo (names.appendedRaw());
            password.detachTo (values.appendedRaw());
        }

        /// @todo add -initiator option
        Bstr ("InitiatorName").detachTo (names.appendedRaw());
        Bstr ("iqn.2008-04.com.sun.virtualbox.initiator").detachTo (values.appendedRaw());

        /// @todo add -targetName and -targetPassword options

        if (fIntNet)
        {
            Bstr ("HostIPStack").detachTo (names.appendedRaw());
            Bstr ("0").detachTo (values.appendedRaw());
        }

        CHECK_ERROR_BREAK (hardDisk,
            SetProperties (ComSafeArrayAsInParam (names),
                           ComSafeArrayAsInParam (values)));

        Guid guid;
        CHECK_ERROR(hardDisk, COMGETTER(Id)(guid.asOutParam()));
        RTPrintf("iSCSI disk created. UUID: %s\n", guid.toString().raw());
    }
    while (0);

    return SUCCEEDED(rc) ? 0 : 1;
}


int handleShowHardDiskInfo(int argc, char *argv[],
                           ComPtr<IVirtualBox> virtualBox, ComPtr<ISession> session)
{
    HRESULT rc;

    if (argc != 1)
        return errorSyntax(USAGE_SHOWHDINFO, "Incorrect number of parameters");

    ComPtr<IHardDisk2> hardDisk;
    Bstr filepath;

    bool unknown = false;

    /* first guess is that it's a UUID */
    Guid uuid(argv[0]);
    rc = virtualBox->GetHardDisk2(uuid, hardDisk.asOutParam());
    /* no? then it must be a filename */
    if (FAILED (rc))
    {
        filepath = argv[0];
        rc = virtualBox->FindHardDisk2(filepath, hardDisk.asOutParam());
        /* no? well, then it's an unkwnown image */
        if (FAILED (rc))
        {
            CHECK_ERROR(virtualBox, OpenHardDisk2(filepath, hardDisk.asOutParam()));
            if (SUCCEEDED (rc))
            {
                unknown = true;
            }
        }
    }
    do
    {
        if (!SUCCEEDED(rc))
            break;

        hardDisk->COMGETTER(Id)(uuid.asOutParam());
        RTPrintf("UUID:                 %s\n", uuid.toString().raw());

        /* check for accessibility */
        /// @todo NEWMEDIA check accessibility of all parents
        /// @todo NEWMEDIA print the full state value
        MediaState_T state;
        CHECK_ERROR_BREAK (hardDisk, COMGETTER(State)(&state));
        RTPrintf("Accessible:           %s\n", state != MediaState_Inaccessible ? "yes" : "no");

        if (state == MediaState_Inaccessible)
        {
            Bstr err;
            CHECK_ERROR_BREAK (hardDisk, COMGETTER(LastAccessError)(err.asOutParam()));
            RTPrintf("Access Error:         %lS\n", err.raw());
        }

        Bstr description;
        hardDisk->COMGETTER(Description)(description.asOutParam());
        if (description)
        {
            RTPrintf("Description:          %lS\n", description.raw());
        }

        ULONG64 logicalSize;
        hardDisk->COMGETTER(LogicalSize)(&logicalSize);
        RTPrintf("Logical size:         %llu MBytes\n", logicalSize);
        ULONG64 actualSize;
        hardDisk->COMGETTER(Size)(&actualSize);
        RTPrintf("Current size on disk: %llu MBytes\n", actualSize >> 20);

        HardDiskType_T type;
        hardDisk->COMGETTER(Type)(&type);
        const char *typeStr = "unknown";
        switch (type)
        {
            case HardDiskType_Normal:
                typeStr = "normal";
                break;
            case HardDiskType_Immutable:
                typeStr = "immutable";
                break;
            case HardDiskType_Writethrough:
                typeStr = "writethrough";
                break;
        }
        RTPrintf("Type:                 %s\n", typeStr);

        Bstr format;
        hardDisk->COMGETTER(Format)(format.asOutParam());
        RTPrintf("Storage format:       %lS\n", format.raw());

        if (!unknown)
        {
            com::SafeGUIDArray machineIds;
            hardDisk->COMGETTER(MachineIds)(ComSafeArrayAsOutParam(machineIds));
            for (size_t j = 0; j < machineIds.size(); ++ j)
            {
                ComPtr<IMachine> machine;
                CHECK_ERROR(virtualBox, GetMachine(machineIds[j], machine.asOutParam()));
                ASSERT(machine);
                Bstr name;
                machine->COMGETTER(Name)(name.asOutParam());
                machine->COMGETTER(Id)(uuid.asOutParam());
                RTPrintf("%s%lS (UUID: %RTuuid)\n",
                         j == 0 ? "In use by VMs:        " : "                      ",
                         name.raw(), &machineIds[j]);
            }
            /// @todo NEWMEDIA check usage in snapshots too
            /// @todo NEWMEDIA also list children and say 'differencing' for
            /// hard disks with the parent or 'base' otherwise.
        }

        Bstr loc;
        hardDisk->COMGETTER(Location)(loc.asOutParam());
        RTPrintf("Location:             %lS\n", loc.raw());
    }
    while (0);

    if (unknown)
    {
        /* close the unknown hard disk to forget it again */
        hardDisk->Close();
    }

    return SUCCEEDED(rc) ? 0 : 1;
}

int handleOpenMedium(int argc, char *argv[],
                     ComPtr<IVirtualBox> virtualBox, ComPtr<ISession> session)
{
    HRESULT rc;

    if (argc < 2)
        return errorSyntax(USAGE_REGISTERIMAGE, "Not enough parameters");

    Bstr filepath(argv[1]);

    if (strcmp(argv[0], "disk") == 0)
    {
        const char *type = NULL;
        /* there can be a type parameter */
        if ((argc > 2) && (argc != 4))
            return errorSyntax(USAGE_REGISTERIMAGE, "Incorrect number of parameters");
        if (argc == 4)
        {
            if (strcmp(argv[2], "-type") != 0)
                return errorSyntax(USAGE_REGISTERIMAGE, "Invalid parameter '%s'", Utf8Str(argv[2]).raw());
            if (   (strcmp(argv[3], "normal") != 0)
                && (strcmp(argv[3], "immutable") != 0)
                && (strcmp(argv[3], "writethrough") != 0))
                return errorArgument("Invalid hard disk type '%s' specified", Utf8Str(argv[3]).raw());
            type = argv[3];
        }

        ComPtr<IHardDisk2> hardDisk;
        CHECK_ERROR(virtualBox, OpenHardDisk2(filepath, hardDisk.asOutParam()));
        if (SUCCEEDED(rc) && hardDisk)
        {
            /* change the type if requested */
            if (type)
            {
                if (strcmp(type, "normal") == 0)
                    CHECK_ERROR(hardDisk, COMSETTER(Type)(HardDiskType_Normal));
                else if (strcmp(type, "immutable") == 0)
                    CHECK_ERROR(hardDisk, COMSETTER(Type)(HardDiskType_Immutable));
                else if (strcmp(type, "writethrough") == 0)
                    CHECK_ERROR(hardDisk, COMSETTER(Type)(HardDiskType_Writethrough));
            }
        }
    }
    else if (strcmp(argv[0], "dvd") == 0)
    {
        ComPtr<IDVDImage2> dvdImage;
        CHECK_ERROR(virtualBox, OpenDVDImage(filepath, Guid(), dvdImage.asOutParam()));
    }
    else if (strcmp(argv[0], "floppy") == 0)
    {
        ComPtr<IFloppyImage2> floppyImage;
        CHECK_ERROR(virtualBox, OpenFloppyImage(filepath, Guid(), floppyImage.asOutParam()));
    }
    else
        return errorSyntax(USAGE_REGISTERIMAGE, "Invalid parameter '%s'", Utf8Str(argv[1]).raw());

    return SUCCEEDED(rc) ? 0 : 1;
}

int handleCloseMedium(int argc, char *argv[],
                      ComPtr<IVirtualBox> virtualBox, ComPtr<ISession> session)
{
    HRESULT rc;

    if (argc != 2)
        return errorSyntax(USAGE_UNREGISTERIMAGE, "Incorrect number of parameters");

    /* first guess is that it's a UUID */
    Guid uuid(argv[1]);

    if (strcmp(argv[0], "disk") == 0)
    {
        ComPtr<IHardDisk2> hardDisk;
        rc = virtualBox->GetHardDisk2(uuid, hardDisk.asOutParam());
        /* not a UUID or not registered? Then it must be a filename */
        if (!hardDisk)
        {
            CHECK_ERROR(virtualBox, FindHardDisk2(Bstr(argv[1]), hardDisk.asOutParam()));
        }
        if (SUCCEEDED(rc) && hardDisk)
        {
            CHECK_ERROR(hardDisk, Close());
        }
    }
    else
    if (strcmp(argv[0], "dvd") == 0)
    {
        ComPtr<IDVDImage2> dvdImage;
        rc = virtualBox->GetDVDImage(uuid, dvdImage.asOutParam());
        /* not a UUID or not registered? Then it must be a filename */
        if (!dvdImage)
        {
            CHECK_ERROR(virtualBox, FindDVDImage(Bstr(argv[1]), dvdImage.asOutParam()));
        }
        if (SUCCEEDED(rc) && dvdImage)
        {
            CHECK_ERROR(dvdImage, Close());
        }
    }
    else
    if (strcmp(argv[0], "floppy") == 0)
    {
        ComPtr<IFloppyImage2> floppyImage;
        rc = virtualBox->GetFloppyImage(uuid, floppyImage.asOutParam());
        /* not a UUID or not registered? Then it must be a filename */
        if (!floppyImage)
        {
            CHECK_ERROR(virtualBox, FindFloppyImage(Bstr(argv[1]), floppyImage.asOutParam()));
        }
        if (SUCCEEDED(rc) && floppyImage)
        {
            CHECK_ERROR(floppyImage, Close());
        }
    }
    else
        return errorSyntax(USAGE_UNREGISTERIMAGE, "Invalid parameter '%s'", Utf8Str(argv[1]).raw());

    return SUCCEEDED(rc) ? 0 : 1;
}
#endif /* !VBOX_ONLY_DOCS */
