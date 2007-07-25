/** @file
 * PDM - Pluggable Device Manager, USB Devices.
 */

/*
 * Copyright (C) 2006-2007 innotek GmbH
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation,
 * in version 2 as it comes in the "COPYING" file of the VirtualBox OSE
 * distribution. VirtualBox OSE is distributed in the hope that it will
 * be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * If you received this file as part of a commercial VirtualBox
 * distribution, then only the terms of your commercial VirtualBox
 * license agreement apply instead of the previous paragraph.
 */

#ifndef ___VBox_pdm_h
# include <VBox/pdm.h>
#endif

#ifndef ___VBox_pdmusb_h
#define ___VBox_pdmusb_h

__BEGIN_DECLS

/** @defgroup grp_pdm_usbdev    USB Devices
 * @ingroup grp_pdm
 * @{
 */


/** PDM USB Device Registration Structure,
 *
 * This structure is used when registering a device from VBoxUSBRegister() in HC Ring-3.
 * The PDM will make use of this structure untill the VM is destroyed.
 */
typedef struct PDMUSBREG
{
    /** Structure version. PDM_DEVREG_VERSION defines the current version. */
    uint32_t            u32Version;
    /** Device name. */
    char                szDeviceName[32];
    /** The description of the device. The UTF-8 string pointed to shall, like this structure,
     * remain unchanged from registration till VM destruction. */
    const char         *pszDescription;

    /** Flags, combination of the PDM_USBREG_FLAGS_* \#defines. */
    RTUINT              fFlags;
    /** Maximum number of instances (per VM). */
    RTUINT              cMaxInstances;
    /** Size of the instance data. */
    RTUINT              cbInstance;


    /**
     * Construct an USB device instance for a VM.
     *
     * @returns VBox status.
     * @param   pUsbIns     The USB device instance data.
     *                      If the registration structure is needed, pUsbDev->pDevReg points to it.
     * @param   iInstance   Instance number. Use this to figure out which registers and such to use.
     *                      The instance number is also found in pUsbDev->iInstance, but since it's
     *                      likely to be freqently used PDM passes it as parameter.
     * @param   pCfg        Configuration node handle for the device. Use this to obtain the configuration
     *                      of the device instance. It's also found in pUsbDev->pCfg, but since it's
     *                      primary usage will in this function it's passed as a parameter.
     * @param   pCfgGlobal  Handle to the global device configuration. Also found in pUsbDev->pCfgGlobal.
     * @remarks This callback is required.
     */
    DECLR3CALLBACKMEMBER(int, pfnConstruct,(PPDMUSBINS pUsbIns, int iInstance, PCFGMNODE pCfg, PCFGMNODE pCfgGlobal));

    /**
     * Init complete notification.
     *
     * This can be done to do communication with other devices and other
     * initialization which requires everything to be in place.
     *
     * @returns VBOX status code.
     * @param   pUsbIns     The USB device instance data.
     * @remarks Optional.
     */
    DECLR3CALLBACKMEMBER(int, pfnInitComplete,(PPDMUSBINS pUsbIns));

    /**
     * Destruct an USB device instance.
     *
     * Most VM resources are freed by the VM. This callback is provided so that any non-VM
     * resources can be freed correctly.
     *
     * This method will be called regardless of the pfnConstruc result to avoid
     * complicated failure paths.
     *
     * @returns VBox status.
     * @param   pUsbIns     The USB device instance data.
     * @remarks Optional.
     */
    DECLR3CALLBACKMEMBER(int, pfnDestruct,(PPDMUSBINS pUsbIns));

    /**
     * Power On notification.
     *
     * @returns VBox status.
     * @param   pUsbIns     The USB device instance data.
     * @remarks Optional.
     */
    DECLR3CALLBACKMEMBER(void, pfnPowerOn,(PPDMUSBINS pUsbIns));

    /**
     * Reset notification.
     *
     * @returns VBox status.
     * @param   pUsbIns     The USB device instance data.
     * @remarks Optional.
     */
    DECLR3CALLBACKMEMBER(void, pfnReset,(PPDMUSBINS pUsbIns));

    /**
     * Suspend notification.
     *
     * @returns VBox status.
     * @param   pUsbIns     The USB device instance data.
     * @remarks Optional.
     */
    DECLR3CALLBACKMEMBER(void, pfnSuspend,(PPDMUSBINS pUsbIns));

    /**
     * Resume notification.
     *
     * @returns VBox status.
     * @param   pUsbIns     The USB device instance data.
     * @remarks Optional.
     */
    DECLR3CALLBACKMEMBER(void, pfnResume,(PPDMUSBINS pUsbIns));

    /**
     * Power Off notification.
     *
     * @param   pUsbIns     The USB device instance data.
     */
    DECLR3CALLBACKMEMBER(void, pfnPowerOff,(PPDMUSBINS pUsbIns));

    /**
     * Attach command.
     *
     * This is called to let the USB device attach to a driver for a specified LUN
     * at runtime. This is not called during VM construction, the device constructor
     * have to attach to all the available drivers.
     *
     * @returns VBox status code.
     * @param   pUsbIns     The USB device instance data.
     * @param   iLUN        The logical unit which is being detached.
     * @remarks Optional.
     */
    DECLR3CALLBACKMEMBER(int, pfnAttach,(PPDMUSBINS pUsbIns, unsigned iLUN));

    /**
     * Detach notification.
     *
     * This is called when a driver is detaching itself from a LUN of the device.
     * The device should adjust it's state to reflect this.
     *
     * @param   pUsbIns     The USB device instance data.
     * @param   iLUN        The logical unit which is being detached.
     * @remarks Optional.
     */
    DECLR3CALLBACKMEMBER(void, pfnDetach,(PPDMUSBINS pUsbIns, unsigned iLUN));

    /**
     * Query the base interface of a logical unit.
     *
     * @returns VBOX status code.
     * @param   pUsbIns     The USB device instance data.
     * @param   iLUN        The logicial unit to query.
     * @param   ppBase      Where to store the pointer to the base interface of the LUN.
     * @remarks Optional.
     */
    DECLR3CALLBACKMEMBER(int, pfnQueryInterface,(PPDMUSBINS pUsbIns, unsigned iLUN, PPDMIBASE *ppBase));

    /** Just some init precaution. Must be set to PDM_USBREG_VERSION. */
    uint32_t            u32TheEnd;
} PDMUSBREG;
/** Pointer to a PDM USB Device Structure. */
typedef PDMUSBREG *PPDMUSBREG;
/** Const pointer to a PDM USB Device Structure. */
typedef PDMUSBREG const *PCPDMUSBREG;

/** Current USBREG version number. */
#define PDM_USBREG_VERSION  0xed010000

/** PDM USB Device Flags.
 * @{ */
/* none yet */
/** @} */

#ifdef IN_RING3
/**
 * PDM USB Device API.
 */
typedef struct PDMUSBHLP
{
    /** Structure version. PDM_USBHLP_VERSION defines the current version. */
    uint32_t                    u32Version;

    /**
     * Attaches a driver (chain) to the USB device.
     *
     * The first call for a LUN this will serve as a registartion of the LUN. The pBaseInterface and
     * the pszDesc string will be registered with that LUN and kept around for PDMR3QueryUSBDeviceLun().
     *
     * @returns VBox status code.
     * @param   pUsbIns             The USB device instance.
     * @param   iLun                The logical unit to attach.
     * @param   pBaseInterface      Pointer to the base interface for that LUN. (device side / down)
     * @param   ppBaseInterface     Where to store the pointer to the base interface. (driver side / up)
     * @param   pszDesc             Pointer to a string describing the LUN. This string must remain valid
     *                              for the live of the device instance.
     */
    DECLR3CALLBACKMEMBER(int, pfnDriverAttach,(PPDMUSBINS pUsbIns, RTUINT iLun, PPDMIBASE pBaseInterface, PPDMIBASE *ppBaseInterface, const char *pszDesc));

    /**
     * Assert that the current thread is the emulation thread.
     *
     * @returns True if correct.
     * @returns False if wrong.
     * @param   pUsbIns             The USB device instance.
     * @param   pszFile             Filename of the assertion location.
     * @param   iLine               Linenumber of the assertion location.
     * @param   pszFunction         Function of the assertion location.
     */
    DECLR3CALLBACKMEMBER(bool, pfnAssertEMT,(PPDMUSBINS pUsbIns, const char *pszFile, unsigned iLine, const char *pszFunction));

    /**
     * Assert that the current thread is NOT the emulation thread.
     *
     * @returns True if correct.
     * @returns False if wrong.
     * @param   pUsbIns             The USB device instance.
     * @param   pszFile             Filename of the assertion location.
     * @param   iLine               Linenumber of the assertion location.
     * @param   pszFunction         Function of the assertion location.
     */
    DECLR3CALLBACKMEMBER(bool, pfnAssertOther,(PPDMUSBINS pUsbIns, const char *pszFile, unsigned iLine, const char *pszFunction));

    /**
     * Stops the VM and enters the debugger to look at the guest state.
     *
     * Use the PDMUsbDBGFStop() inline function with the RT_SRC_POS macro instead of
     * invoking this function directly.
     *
     * @returns VBox status code which must be passed up to the VMM.
     * @param   pUsbIns             The USB device instance.
     * @param   pszFile             Filename of the assertion location.
     * @param   iLine               The linenumber of the assertion location.
     * @param   pszFunction         Function of the assertion location.
     * @param   pszFormat           Message. (optional)
     * @param   va                  Message parameters.
     */
    DECLR3CALLBACKMEMBER(int, pfnDBGFStopV,(PPDMUSBINS pUsbIns, const char *pszFile, unsigned iLine, const char *pszFunction, const char *pszFormat, va_list va));

    /**
     * Register a info handler with DBGF,
     *
     * @returns VBox status code.
     * @param   pUsbIns             The USB device instance.
     * @param   pszName             The identifier of the info.
     * @param   pszDesc             The description of the info and any arguments the handler may take.
     * @param   pfnHandler          The handler function to be called to display the info.
     */
/** @todo    DECLR3CALLBACKMEMBER(int, pfnDBGFInfoRegister,(PPDMUSBINS pUsbIns, const char *pszName, const char *pszDesc, PFNDBGFHANDLERUSB pfnHandler)); */

    /**
     * Allocate memory which is associated with current VM instance
     * and automatically freed on it's destruction.
     *
     * @returns Pointer to allocated memory. The memory is *NOT* zero-ed.
     * @param   pUsbIns             The USB device instance.
     * @param   cb                  Number of bytes to allocate.
     */
    DECLR3CALLBACKMEMBER(void *, pfnMMHeapAlloc,(PPDMUSBINS pUsbIns, size_t cb));

    /**
     * Allocate memory which is associated with current VM instance
     * and automatically freed on it's destruction. The memory is ZEROed.
     *
     * @returns Pointer to allocated memory. The memory is *NOT* zero-ed.
     * @param   pUsbIns             The USB device instance.
     * @param   cb                  Number of bytes to allocate.
     */
    DECLR3CALLBACKMEMBER(void *, pfnMMHeapAllocZ,(PPDMUSBINS pUsbIns, size_t cb));

    /**
     * Create a queue.
     *
     * @returns VBox status code.
     * @param   pUsbIns             The USB device instance.
     * @param   cbItem              Size a queue item.
     * @param   cItems              Number of items in the queue.
     * @param   cMilliesInterval    Number of milliseconds between polling the queue.
     *                              If 0 then the emulation thread will be notified whenever an item arrives.
     * @param   pfnCallback         The consumer function.
     * @param   ppQueue             Where to store the queue handle on success.
     * @thread  The emulation thread.
     */
/** @todo    DECLR3CALLBACKMEMBER(int, pfnPDMQueueCreate,(PPDMUSBINS pUsbIns, RTUINT cbItem, RTUINT cItems, uint32_t cMilliesInterval, PFNPDMQUEUEUSB pfnCallback, PPDMQUEUE *ppQueue)); */

    /**
     * Register a save state data unit.
     *
     * @returns VBox status.
     * @param   pUsbIns             The USB device instance.
     * @param   pszName         Data unit name.
     * @param   u32Instance     The instance identifier of the data unit.
     *                          This must together with the name be unique.
     * @param   u32Version      Data layout version number.
     * @param   cbGuess         The approximate amount of data in the unit.
     *                          Only for progress indicators.
     * @param   pfnSavePrep     Prepare save callback, optional.
     * @param   pfnSaveExec     Execute save callback, optional.
     * @param   pfnSaveDone     Done save callback, optional.
     * @param   pfnLoadPrep     Prepare load callback, optional.
     * @param   pfnLoadExec     Execute load callback, optional.
     * @param   pfnLoadDone     Done load callback, optional.
     */
/** @todo    DECLR3CALLBACKMEMBER(int, pfnSSMRegister,(PPDMUSBINS pUsbIns, const char *pszName, uint32_t u32Instance, uint32_t u32Version, size_t cbGuess,
                                              PFNSSMUSBSAVEPREP pfnSavePrep, PFNSSMUSBSAVEEXEC pfnSaveExec, PFNSSMUSBSAVEDONE pfnSaveDone,
                                              PFNSSMUSBLOADPREP pfnLoadPrep, PFNSSMUSBLOADEXEC pfnLoadExec, PFNSSMUSBLOADDONE pfnLoadDone)); */

    /**
     * Register a STAM sample.
     *
     * Use the PDMUsbHlpSTAMRegister wrapper.
     *
     * @returns VBox status.
     * @param   pUsbIns             The USB device instance.
     * @param   pvSample            Pointer to the sample.
     * @param   enmType             Sample type. This indicates what pvSample is pointing at.
     * @param   enmVisibility       Visibility type specifying whether unused statistics should be visible or not.
     * @param   enmUnit             Sample unit.
     * @param   pszDesc             Sample description.
     * @param   pszName             The sample name format string.
     * @param   va                  Arguments to the format string.
     */
    DECLR3CALLBACKMEMBER(void, pfnSTAMRegisterV,(PPDMUSBINS pUsbIns, void *pvSample, STAMTYPE enmType, STAMVISIBILITY enmVisibility,
                                                 STAMUNIT enmUnit, const char *pszDesc, const char *pszName, va_list va));

    /**
     * Creates a timer.
     *
     * @returns VBox status.
     * @param   pUsbIns             The USB device instance.
     * @param   enmClock            The clock to use on this timer.
     * @param   pfnCallback         Callback function.
     * @param   pszDesc             Pointer to description string which must stay around
     *                              until the timer is fully destroyed (i.e. a bit after TMTimerDestroy()).
     * @param   ppTimer             Where to store the timer on success.
     */
/** @todo    DECLR3CALLBACKMEMBER(int, pfnTMTimerCreate,(PPDMUSBINS pUsbIns, TMCLOCK enmClock, PFNTMTIMERUSB pfnCallback, const char *pszDesc, PPTMTIMERHC ppTimer)); */

    /**
     * Set the VM error message
     *
     * @returns rc.
     * @param   pUsbIns             The USB device instance.
     * @param   rc                  VBox status code.
     * @param   RT_SRC_POS_DECL     Use RT_SRC_POS.
     * @param   pszFormat           Error message format string.
     * @param   va                  Error message arguments.
     */
    DECLR3CALLBACKMEMBER(int, pfnVMSetErrorV,(PPDMUSBINS pUsbIns, int rc, RT_SRC_POS_DECL, const char *pszFormat, va_list va));

    /**
     * Set the VM runtime error message
     *
     * @returns VBox status code.
     * @param   pUsbIns             The USB device instance.
     * @param   fFatal              Whether it is a fatal error or not.
     * @param   pszErrorID          Error ID string.
     * @param   pszFormat           Error message format string.
     * @param   va                  Error message arguments.
     */
    DECLR3CALLBACKMEMBER(int, pfnVMSetRuntimeErrorV,(PPDMUSBINS pUsbIns, bool fFatal, const char *pszErrorID, const char *pszFormat, va_list va));

    /** Just a safety precaution. */
    uint32_t                        u32TheEnd;
} PDMUSBHLP;
/** Pointer PDM USB Device API. */
typedef PDMUSBHLP *PPDMUSBHLP;
/** Pointer const PDM USB Device API. */
typedef const PDMUSBHLP *PCPDMUSBHLP;

/** Current USBHLP version number. */
#define PDM_USBHLP_VERSION  0xec0020000

#endif /* IN_RING3 */

/**
 * PDM USB Device Instance.
 */
typedef struct PDMUSBINS
{
    /** Structure version. PDM_USBINS_VERSION defines the current version. */
    uint32_t                    u32Version;
    /** USB device instance number. */
    RTUINT                      iInstance;
    /** The base interface of the device.
     * The device constructor initializes this if it has any device level
     * interfaces to export. To obtain this interface call PDMR3QueryUSBDevice(). */
    PDMIBASE                    IBase;

    /** Internal data. */
    union
    {
#ifdef PDMUSBINSINT_DECLARED
        PDMUSBINSINT            s;
#endif
        uint8_t                 padding[HC_ARCH_BITS == 32 ? 48 : 96];
    } Internal;

    /** Pointer the HC PDM Device API. */
    R3PTRTYPE(PCPDMUSBHLP)      pDevHlp;
    /** Pointer to the USB device registration structure.  */
    R3PTRTYPE(PCPDMUSBREG)      pDevReg;
    /** Configuration handle. */
    R3PTRTYPE(PCFGMNODE)        pCfg;
    /** The (device) global configuration handle. */
    R3PTRTYPE(PCFGMNODE)        pCfgGlobal;
    /** Pointer to device instance data. */
    R3PTRTYPE(void *)           pvInstanceDataR3;
    /* padding to make achInstanceData aligned at 32 byte boundrary. */
    uint32_t                    au32Padding[HC_ARCH_BITS == 32 ? 4 : 6];
    /** Device instance data. The size of this area is defined
     * in the PDMUSBREG::cbInstanceData field. */
    char                        achInstanceData[8];
} PDMUSBINS;

/** Current USBINS version number. */
#define PDM_USBINS_VERSION  0xf3010000

/** Converts a pointer to the PDMUSBINS::IBase to a pointer to PDMUSBINS. */
#define PDMIBASE_2_PDMUSB(pInterface) ( (PPDMUSBINS)((char *)(pInterface) - RT_OFFSETOF(PDMUSBINS, IBase)) )


/** @def PDMUSB_ASSERT_EMT
 * Assert that the current thread is the emulation thread.
 */
#ifdef VBOX_STRICT
# define PDMUSB_ASSERT_EMT(pDevIns)  pDevIns->pDevHlp->pfnAssertEMT(pDevIns, __FILE__, __LINE__, __FUNCTION__)
#else
# define PDMUSB_ASSERT_EMT(pDevIns)  do { } while (0)
#endif

/** @def PDMUSB_ASSERT_OTHER
 * Assert that the current thread is NOT the emulation thread.
 */
#ifdef VBOX_STRICT
# define PDMUSB_ASSERT_OTHER(pDevIns)  pDevIns->pDevHlp->pfnAssertOther(pDevIns, __FILE__, __LINE__, __FUNCTION__)
#else
# define PDMUSB_ASSERT_OTHER(pDevIns)  do { } while (0)
#endif

/** @def PDMUSB_SET_ERROR
 * Set the VM error. See PDMDevHlpVMSetError() for printf like message formatting.
 */
#define PDMUSB_SET_ERROR(pDevIns, rc, pszError) \
    PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS, "%s", pszError)

/** @def PDMUSB_SET_RUNTIME_ERROR
 * Set the VM runtime error. See PDMDevHlpVMSetRuntimeError() for printf like message formatting.
 */
#define PDMUSB_SET_RUNTIME_ERROR(pDevIns, fFatal, pszErrorID, pszError) \
    PDMDevHlpVMSetRuntimeError(pDevIns, fFatal, pszErrorID, "%s", pszError)


#ifdef IN_RING3

/**
 * VBOX_STRICT wrapper for pDevHlp->pfnDBGFStopV.
 *
 * @returns VBox status code which must be passed up to the VMM.
 * @param   pDevIns             Device instance.
 * @param   RT_SRC_POS_DECL     Use RT_SRC_POS.
 * @param   pszFormat           Message. (optional)
 * @param   ...                 Message parameters.
 */
DECLINLINE(int) PDMUsbDBGFStop(PPDMUSBINS pUsbIns, RT_SRC_POS_DECL, const char *pszFormat, ...)
{
#ifdef VBOX_STRICT
    int rc;
    va_list va;
    va_start(va, pszFormat);
    rc = pUsbIns->pDevHlp->pfnDBGFStopV(pUsbIns, RT_SRC_POS_ARGS, pszFormat, va);
    va_end(va);
    return rc;
#else
    return VINF_SUCCESS;
#endif
}


/* inline wrappers */

#endif



/** Pointer to callbacks provided to the VBoxUSBRegister() call. */
typedef const struct PDMUSBREGCB *PCPDMUSBREGCB;

/**
 * Callbacks for VBoxUSBDeviceRegister().
 */
typedef struct PDMUSBREGCB
{
    /** Interface version.
     * This is set to PDM_USBREG_CB_VERSION. */
    uint32_t                    u32Version;

    /**
     * Registers a device with the current VM instance.
     *
     * @returns VBox status code.
     * @param   pCallbacks      Pointer to the callback table.
     * @param   pDevReg         Pointer to the device registration record.
     *                          This data must be permanent and readonly.
     */
    DECLR3CALLBACKMEMBER(int, pfnRegister,(PCPDMUSBREGCB pCallbacks, PCPDMUSBREG pDevReg));

    /**
     * Allocate memory which is associated with current VM instance
     * and automatically freed on it's destruction.
     *
     * @returns Pointer to allocated memory. The memory is *NOT* zero-ed.
     * @param   pCallbacks      Pointer to the callback table.
     * @param   cb              Number of bytes to allocate.
     */
    DECLR3CALLBACKMEMBER(void *, pfnMMHeapAlloc,(PCPDMUSBREGCB pCallbacks, size_t cb));
} PDMUSBREGCB;

/** Current version of the PDMUSBREGCB structure.  */
#define PDM_USBREG_CB_VERSION 0xee010000


/**
 * The VBoxUSBRegister callback function.
 *
 * PDM will invoke this function after loading a USB device module and letting
 * the module decide which devices to register and how to handle conflicts.
 *
 * @returns VBox status code.
 * @param   pCallbacks      Pointer to the callback table.
 * @param   u32Version      VBox version number.
 */
typedef DECLCALLBACK(int) FNPDMVBOXUSBREGISTER(PCPDMUSBREGCB pCallbacks, uint32_t u32Version);

/** @} */

__END_DECLS

#endif
