/** @file
 * InnoTek Portable Runtime - Assembly Functions.
 */

/*
 * Copyright (C) 2006 InnoTek Systemberatung GmbH
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

#ifndef __iprt_asm_h__
#define __iprt_asm_h__

#include <iprt/cdefs.h>
#include <iprt/types.h>
/** @todo #include <iprt/param.h> for PAGE_SIZE. */
/** @def RT_INLINE_ASM_USES_INTRIN
 * Defined as 1 if we're using a _MSC_VER 1400.
 * Otherwise defined as 0.
 */

#ifdef _MSC_VER
# if _MSC_VER >= 1400
#  define RT_INLINE_ASM_USES_INTRIN 1
#  include <intrin.h>
   /* Emit the intrinsics at all optimization levels. */
#  pragma intrinsic(__cpuid)
#  pragma intrinsic(_enable)
#  pragma intrinsic(_disable)
#  pragma intrinsic(__rdtsc)
#  pragma intrinsic(__readmsr)
#  pragma intrinsic(__writemsr)
#  pragma intrinsic(__outbyte)
#  pragma intrinsic(__outword)
#  pragma intrinsic(__outdword)
#  pragma intrinsic(__inbyte)
#  pragma intrinsic(__inword)
#  pragma intrinsic(__indword)
#  pragma intrinsic(__invlpg)
#  pragma intrinsic(__stosd)
#  pragma intrinsic(__stosw)
#  pragma intrinsic(__stosb)
#  pragma intrinsic(__readcr0)
#  pragma intrinsic(__readcr2)
#  pragma intrinsic(__readcr3)
#  pragma intrinsic(__readcr4)
#  pragma intrinsic(__writecr0)
#  pragma intrinsic(__writecr3)
#  pragma intrinsic(__writecr4)
#  pragma intrinsic(_BitScanForward)
#  pragma intrinsic(_BitScanReverse)
#  pragma intrinsic(_bittest)
#  pragma intrinsic(_bittestandset)
#  pragma intrinsic(_bittestandreset)
#  pragma intrinsic(_bittestandcomplement)
#  pragma intrinsic(_byteswap_ushort)
#  pragma intrinsic(_byteswap_ulong)
#  pragma intrinsic(_interlockedbittestandset)
#  pragma intrinsic(_interlockedbittestandreset)
#  pragma intrinsic(_InterlockedAnd)
#  pragma intrinsic(_InterlockedOr)
#  pragma intrinsic(_InterlockedIncrement)
#  pragma intrinsic(_InterlockedDecrement)
#  pragma intrinsic(_InterlockedExchange)
#  pragma intrinsic(_InterlockedCompareExchange)
#  pragma intrinsic(_InterlockedCompareExchange64)
#  ifdef __AMD64__
#   pragma intrinsic(__stosq)
#   pragma intrinsic(__readcr8)
#   pragma intrinsic(__writecr8)
#   pragma intrinsic(_byteswap_uint64)
#   pragma intrinsic(_InterlockedExchange64)
#  endif
# endif
#endif
#ifndef RT_INLINE_ASM_USES_INTRIN
# define RT_INLINE_ASM_USES_INTRIN 0
#endif



/** @defgroup grp_asm       ASM - Assembly Routines
 * @ingroup grp_rt
 * @{
 */

/** @def RT_INLINE_ASM_EXTERNAL
 * Defined as 1 if the compiler does not support inline assembly.
 * The ASM* functions will then be implemented in an external .asm file.
 *
 * @remark  At the present time it's unconfirmed whether or not Microsoft skipped
 *          inline assmebly in their AMD64 compiler.
 */
#if defined(_MSC_VER) && defined(__AMD64__)
# define RT_INLINE_ASM_EXTERNAL 1
#else
# define RT_INLINE_ASM_EXTERNAL 0
#endif

/** @def RT_INLINE_ASM_GNU_STYLE
 * Defined as 1 if the compiler understand GNU style inline assembly.
 */
#if defined(_MSC_VER)
# define RT_INLINE_ASM_GNU_STYLE 0
#else
# define RT_INLINE_ASM_GNU_STYLE 1
#endif


/** @todo find a more proper place for this structure? */
#pragma pack(1)
/** IDTR */
typedef struct RTIDTR
{
    /** Size of the IDT. */
    uint16_t    cbIdt;
    /** Address of the IDT. */
    uintptr_t   pIdt;
} RTIDTR, *PRTIDTR;
#pragma pack()

#pragma pack(1)
/** GDTR */
typedef struct RTGDTR
{
    /** Size of the GDT. */
    uint16_t    cbGdt;
    /** Address of the GDT. */
    uintptr_t   pGdt;
} RTGDTR, *PRTGDTR;
#pragma pack()


/** @def ASMReturnAddress
 * Gets the return address of the current (or calling if you like) function or method.
 */
#ifdef _MSC_VER
# ifdef __cplusplus
extern "C"
# endif
void * _ReturnAddress(void);
# pragma intrinsic(_ReturnAddress)
# define ASMReturnAddress() _ReturnAddress()
#elif defined(__GNUC__) || defined(__DOXYGEN__)
# define ASMReturnAddress() __builtin_return_address(0)
#else
# error "Unsupported compiler."
#endif


/**
 * Gets the content of the IDTR CPU register.
 * @param   pIdtr   Where to store the IDTR contents.
 */
#if RT_INLINE_ASM_EXTERNAL
DECLASM(void) ASMGetIDTR(PRTIDTR pIdtr);
#else
DECLINLINE(void) ASMGetIDTR(PRTIDTR pIdtr)
{
# if RT_INLINE_ASM_GNU_STYLE
    __asm__ __volatile__ ("sidt %0" : "=m" (*pIdtr));
# else
    __asm
    {
#  ifdef __AMD64__
        mov     rax, [pIdtr]
        sidt    [rax]
#  else
        mov     eax, [pIdtr]
        sidt    [eax]
#  endif
    }
# endif
}
#endif


/**
 * Sets the content of the IDTR CPU register.
 * @param   pIdtr   Where to load the IDTR contents from
 */
#if RT_INLINE_ASM_EXTERNAL
DECLASM(void) ASMSetIDTR(const RTIDTR *pIdtr);
#else
DECLINLINE(void) ASMSetIDTR(const RTIDTR *pIdtr)
{
# if RT_INLINE_ASM_GNU_STYLE
    __asm__ __volatile__ ("lidt %0" : : "m" (*pIdtr));
# else
    __asm
    {
#  ifdef __AMD64__
        mov     rax, [pIdtr]
        lidt    [rax]
#  else
        mov     eax, [pIdtr]
        lidt    [eax]
#  endif
    }
# endif
}
#endif


/**
 * Gets the content of the GDTR CPU register.
 * @param   pGdtr   Where to store the GDTR contents.
 */
#if RT_INLINE_ASM_EXTERNAL
DECLASM(void) ASMGetGDTR(PRTGDTR pGdtr);
#else
DECLINLINE(void) ASMGetGDTR(PRTGDTR pGdtr)
{
# if RT_INLINE_ASM_GNU_STYLE
    __asm__ __volatile__ ("sgdt %0" : "=m" (*pGdtr));
# else
    __asm
    {
#  ifdef __AMD64__
        mov     rax, [pGdtr]
        sgdt    [rax]
#  else
        mov     eax, [pGdtr]
        sgdt    [eax]
#  endif
    }
# endif
}
#endif

/**
 * Get the cs register.
 * @returns cs.
 */
#if RT_INLINE_ASM_EXTERNAL
DECLASM(RTSEL) ASMGetCS(void);
#else
DECLINLINE(RTSEL) ASMGetCS(void)
{
    RTSEL SelCS;
# if RT_INLINE_ASM_GNU_STYLE
    __asm__ __volatile__("movw  %%cs, %0\n\t" : "=r" (SelCS));
# else
    __asm
    {
        mov     ax, cs
        mov     [SelCS], ax
    }
# endif
    return SelCS;
}
#endif


/**
 * Get the DS register.
 * @returns DS.
 */
#if RT_INLINE_ASM_EXTERNAL
DECLASM(RTSEL) ASMGetDS(void);
#else
DECLINLINE(RTSEL) ASMGetDS(void)
{
    RTSEL SelDS;
# if RT_INLINE_ASM_GNU_STYLE
    __asm__ __volatile__("movw  %%ds, %0\n\t" : "=r" (SelDS));
# else
    __asm
    {
        mov     ax, ds
        mov     [SelDS], ax
    }
# endif
    return SelDS;
}
#endif


/**
 * Get the ES register.
 * @returns ES.
 */
#if RT_INLINE_ASM_EXTERNAL
DECLASM(RTSEL) ASMGetES(void);
#else
DECLINLINE(RTSEL) ASMGetES(void)
{
    RTSEL SelES;
# if RT_INLINE_ASM_GNU_STYLE
    __asm__ __volatile__("movw  %%es, %0\n\t" : "=r" (SelES));
# else
    __asm
    {
        mov     ax, es
        mov     [SelES], ax
    }
# endif
    return SelES;
}
#endif


/**
 * Get the FS register.
 * @returns FS.
 */
#if RT_INLINE_ASM_EXTERNAL
DECLASM(RTSEL) ASMGetFS(void);
#else
DECLINLINE(RTSEL) ASMGetFS(void)
{
    RTSEL SelFS;
# if RT_INLINE_ASM_GNU_STYLE
    __asm__ __volatile__("movw  %%fs, %0\n\t" : "=r" (SelFS));
# else
    __asm
    {
        mov     ax, fs
        mov     [SelFS], ax
    }
# endif
    return SelFS;
}
# endif


/**
 * Get the GS register.
 * @returns GS.
 */
#if RT_INLINE_ASM_EXTERNAL
DECLASM(RTSEL) ASMGetGS(void);
#else
DECLINLINE(RTSEL) ASMGetGS(void)
{
    RTSEL SelGS;
# if RT_INLINE_ASM_GNU_STYLE
    __asm__ __volatile__("movw  %%gs, %0\n\t" : "=r" (SelGS));
# else
    __asm
    {
        mov     ax, gs
        mov     [SelGS], ax
    }
# endif
    return SelGS;
}
#endif


/**
 * Get the SS register.
 * @returns SS.
 */
#if RT_INLINE_ASM_EXTERNAL
DECLASM(RTSEL) ASMGetSS(void);
#else
DECLINLINE(RTSEL) ASMGetSS(void)
{
    RTSEL SelSS;
# if RT_INLINE_ASM_GNU_STYLE
    __asm__ __volatile__("movw  %%ss, %0\n\t" : "=r" (SelSS));
# else
    __asm
    {
        mov     ax, ss
        mov     [SelSS], ax
    }
# endif
    return SelSS;
}
#endif


/**
 * Get the TR register.
 * @returns TR.
 */
#if RT_INLINE_ASM_EXTERNAL
DECLASM(RTSEL) ASMGetTR(void);
#else
DECLINLINE(RTSEL) ASMGetTR(void)
{
    RTSEL SelTR;
# if RT_INLINE_ASM_GNU_STYLE
    __asm__ __volatile__("str %w0\n\t" : "=r" (SelTR));
# else
    __asm
    {
        str     ax
        mov     [SelTR], ax
    }
# endif
    return SelTR;
}
#endif


/**
 * Get the [RE]FLAGS register.
 * @returns [RE]FLAGS.
 */
#if RT_INLINE_ASM_EXTERNAL
DECLASM(RTCCUINTREG) ASMGetFlags(void);
#else
DECLINLINE(RTCCUINTREG) ASMGetFlags(void)
{
    RTCCUINTREG uFlags;
# if RT_INLINE_ASM_GNU_STYLE
#  ifdef __AMD64__
    __asm__ __volatile__("pushfq\n\t"
                         "popq  %0\n\t"
                         : "=m" (uFlags));
#  else
    __asm__ __volatile__("pushfl\n\t"
                         "popl  %0\n\t"
                         : "=m" (uFlags));
#  endif
# else
    __asm
    {
#  ifdef __AMD64__
        pushfq
        pop  [uFlags]
#  else
        pushfd
        pop  [uFlags]
#  endif
    }
# endif
    return uFlags;
}
#endif


/**
 * Set the [RE]FLAGS register.
 * @param   uFlags      The new [RE]FLAGS value.
 */
#if RT_INLINE_ASM_EXTERNAL
DECLASM(void) ASMSetFlags(RTCCUINTREG uFlags);
#else
DECLINLINE(void) ASMSetFlags(RTCCUINTREG uFlags)
{
# if RT_INLINE_ASM_GNU_STYLE
#  ifdef __AMD64__
    __asm__ __volatile__("pushq %0\n\t"
                         "popfq\n\t"
                         : : "m" (uFlags));
#  else
    __asm__ __volatile__("pushl %0\n\t"
                         "popfl\n\t"
                         : : "m" (uFlags));
#  endif
# else
    __asm
    {
#  ifdef __AMD64__
        push    [uFlags]
        popfq
#  else
        push    [uFlags]
        popfd
#  endif
    }
# endif
}
#endif


/**
 * Gets the content of the CPU timestamp counter register.
 *
 * @returns TSC.
 */
#if RT_INLINE_ASM_EXTERNAL && !RT_INLINE_ASM_USES_INTRIN
DECLASM(uint64_t) ASMReadTSC(void);
#else
DECLINLINE(uint64_t) ASMReadTSC(void)
{
    RTUINT64U u;
# if RT_INLINE_ASM_GNU_STYLE
    __asm__ __volatile__ ("rdtsc\n\t" : "=a" (u.s.Lo), "=d" (u.s.Hi));
# else
#  if RT_INLINE_ASM_USES_INTRIN
    u.u = __rdtsc();
#  else
    __asm
    {
        rdtsc
        mov     [u.s.Lo], eax
        mov     [u.s.Hi], edx
    }
#  endif
# endif
    return u.u;
}
#endif


/**
 * Performs the cpuid instruction returning all registers.
 *
 * @param   uOperator   CPUID operation (eax).
 * @param   pvEAX       Where to store eax.
 * @param   pvEBX       Where to store ebx.
 * @param   pvECX       Where to store ecx.
 * @param   pvEDX       Where to store edx.
 * @remark  We're using void pointers to ease the use of special bitfield structures and such.
 */
#if RT_INLINE_ASM_EXTERNAL && !RT_INLINE_ASM_USES_INTRIN
DECLASM(void) ASMCpuId(uint32_t uOperator, void *pvEAX, void *pvEBX, void *pvECX, void *pvEDX);
#else
DECLINLINE(void) ASMCpuId(uint32_t uOperator, void *pvEAX, void *pvEBX, void *pvECX, void *pvEDX)
{
# if RT_INLINE_ASM_GNU_STYLE
#  ifdef __AMD64__
    RTCCUINTREG uRAX, uRBX, uRCX, uRDX;
    __asm__ ("cpuid\n\t"
             : "=a" (uRAX),
               "=b" (uRBX),
               "=c" (uRCX),
               "=d" (uRDX)
             : "0" (uOperator));
    *(uint32_t *)pvEAX = (uint32_t)uRAX;
    *(uint32_t *)pvEBX = (uint32_t)uRBX;
    *(uint32_t *)pvECX = (uint32_t)uRCX;
    *(uint32_t *)pvEDX = (uint32_t)uRDX;
#  else
    __asm__ ("xchgl %%ebx, %1\n\t"
             "cpuid\n\t"
             "xchgl %%ebx, %1\n\t"
             : "=a" (*(uint32_t *)pvEAX),
               "=r" (*(uint32_t *)pvEBX),
               "=c" (*(uint32_t *)pvECX),
               "=d" (*(uint32_t *)pvEDX)
             : "0" (uOperator));
#  endif

# elif RT_INLINE_ASM_USES_INTRIN
    int aInfo[4];
    __cpuid(aInfo, uOperator);
    *(uint32_t *)pvEAX = aInfo[0];
    *(uint32_t *)pvEBX = aInfo[1];
    *(uint32_t *)pvECX = aInfo[2];
    *(uint32_t *)pvEDX = aInfo[3];

# else
    uint32_t    uEAX;
    uint32_t    uEBX;
    uint32_t    uECX;
    uint32_t    uEDX;
    __asm
    {
        push    ebx
        mov     eax, [uOperator]
        cpuid
        mov     [uEAX], eax
        mov     [uEBX], ebx
        mov     [uECX], ecx
        mov     [uEDX], edx
        pop     ebx
    }
    *(uint32_t *)pvEAX = uEAX;
    *(uint32_t *)pvEBX = uEBX;
    *(uint32_t *)pvECX = uECX;
    *(uint32_t *)pvEDX = uEDX;
# endif
}
#endif


/**
 * Performs the cpuid instruction returning ecx and edx.
 *
 * @param   uOperator   CPUID operation (eax).
 * @param   pvECX       Where to store ecx.
 * @param   pvEDX       Where to store edx.
 * @remark  We're using void pointers to ease the use of special bitfield structures and such.
 */
#if RT_INLINE_ASM_EXTERNAL && !RT_INLINE_ASM_USES_INTRIN
DECLASM(void) ASMCpuId_ECX_EDX(uint32_t uOperator, void *pvECX, void *pvEDX);
#else
DECLINLINE(void) ASMCpuId_ECX_EDX(uint32_t uOperator, void *pvECX, void *pvEDX)
{
    uint32_t uEBX;
    ASMCpuId(uOperator, &uOperator, &uEBX, pvECX, pvEDX);
}
#endif


/**
 * Performs the cpuid instruction returning edx.
 *
 * @param   uOperator   CPUID operation (eax).
 * @returns EDX after cpuid operation.
 */
#if RT_INLINE_ASM_EXTERNAL && !RT_INLINE_ASM_USES_INTRIN
DECLASM(uint32_t) ASMCpuId_EDX(uint32_t uOperator);
#else
DECLINLINE(uint32_t) ASMCpuId_EDX(uint32_t uOperator)
{
    RTCCUINTREG xDX;
# if RT_INLINE_ASM_GNU_STYLE
#  ifdef __AMD64__
    RTCCUINTREG uSpill;
    __asm__ ("cpuid"
             : "=a" (uSpill),
               "=d" (xDX)
             : "0" (uOperator)
             : "rbx", "rcx");
#  elif (defined(PIC) || defined(__DARWIN__)) && defined(__i386__) /* darwin: PIC by default. */
    __asm__ ("push  %%ebx\n\t"
             "cpuid\n\t"
             "pop   %%ebx\n\t"
             : "=a" (uOperator),
               "=d" (xDX)
             : "0" (uOperator)
             : "ecx");
#  else
    __asm__ ("cpuid"
             : "=a" (uOperator),
               "=d" (xDX)
             : "0" (uOperator)
             : "ebx", "ecx");
#  endif

# elif RT_INLINE_ASM_USES_INTRIN
    int aInfo[4];
    __cpuid(aInfo, uOperator);
    xDX = aInfo[3];

# else
    __asm
    {
        push    ebx
        mov     eax, [uOperator]
        cpuid
        mov     [xDX], edx
        pop     ebx
    }
# endif
    return (uint32_t)xDX;
}
#endif


/**
 * Performs the cpuid instruction returning ecx.
 *
 * @param   uOperator   CPUID operation (eax).
 * @returns ECX after cpuid operation.
 */
#if RT_INLINE_ASM_EXTERNAL && !RT_INLINE_ASM_USES_INTRIN
DECLASM(uint32_t) ASMCpuId_ECX(uint32_t uOperator);
#else
DECLINLINE(uint32_t) ASMCpuId_ECX(uint32_t uOperator)
{
    RTCCUINTREG xCX;
# if RT_INLINE_ASM_GNU_STYLE
#  ifdef __AMD64__
    RTCCUINTREG uSpill;
    __asm__ ("cpuid"
             : "=a" (uSpill),
               "=c" (xCX)
             : "0" (uOperator)
             : "rbx", "rdx");
#  elif (defined(PIC) || defined(__DARWIN__)) && defined(__i386__) /* darwin: 4.0.1 compiler option / bug? */
    __asm__ ("push  %%ebx\n\t"
             "cpuid\n\t"
             "pop   %%ebx\n\t"
             : "=a" (uOperator),
               "=c" (xCX)
             : "0" (uOperator)
             : "edx");
#  else
    __asm__ ("cpuid"
             : "=a" (uOperator),
               "=c" (xCX)
             : "0" (uOperator)
             : "ebx", "edx");

#  endif

# elif RT_INLINE_ASM_USES_INTRIN
    int aInfo[4];
    __cpuid(aInfo, uOperator);
    xCX = aInfo[2];

# else
    __asm
    {
        push    ebx
        mov     eax, [uOperator]
        cpuid
        mov     [xCX], ecx
        pop     ebx
    }
# endif
    return (uint32_t)xCX;
}
#endif


/**
 * Checks if the current CPU supports CPUID.
 *
 * @returns true if CPUID is supported.
 */
DECLINLINE(bool) ASMHasCpuId(void)
{
#ifdef __AMD64__
    return true; /* ASSUME that all amd64 compatible CPUs have cpuid. */
#else /* !__AMD64__ */
    bool        fRet = false;
# if RT_INLINE_ASM_GNU_STYLE
    uint32_t    u1;
    uint32_t    u2;
    __asm__ ("pushf\n\t"
             "pop   %1\n\t"
             "mov   %1, %2\n\t"
             "xorl  $0x200000, %1\n\t"
             "push  %1\n\t"
             "popf\n\t"
             "pushf\n\t"
             "pop   %1\n\t"
             "cmpl  %1, %2\n\t"
             "setne %0\n\t"
             "push  %2\n\t"
             "popf\n\t"
             : "=m" (fRet), "=r" (u1), "=r" (u2));
# else
    __asm
    {
        pushfd
        pop     eax
        mov     ebx, eax
        xor     eax, 0200000h
        push    eax
        popfd
        pushfd
        pop     eax
        cmp     eax, ebx
        setne   fRet
        push    ebx
        popfd
    }
# endif
    return fRet;
#endif /* !__AMD64__ */
}


/**
 * Gets the APIC ID of the current CPU.
 *
 * @returns the APIC ID.
 */
#if RT_INLINE_ASM_EXTERNAL && !RT_INLINE_ASM_USES_INTRIN
DECLASM(uint8_t) ASMGetApicId(void);
#else
DECLINLINE(uint8_t) ASMGetApicId(void)
{
    RTCCUINTREG xBX;
# if RT_INLINE_ASM_GNU_STYLE
#  ifdef __AMD64__
    RTCCUINTREG uSpill;
    __asm__ ("cpuid"
             : "=a" (uSpill),
               "=b" (xBX)
             : "0" (1)
             : "rcx", "rdx");
#  elif (defined(PIC) || defined(__DARWIN__)) && defined(__i386__)
    RTCCUINTREG uSpill;
    __asm__ ("mov   %%ebx,%1\n\t"
             "cpuid\n\t"
             "xchgl %%ebx,%1\n\t"
             : "=a" (uSpill),
               "=r" (xBX)
             : "0" (1)
             : "ecx", "edx");
#  else
    RTCCUINTREG uSpill;
    __asm__ ("cpuid"
             : "=a" (uSpill),
               "=b" (xBX)
             : "0" (1)
             : "ecx", "edx");
#  endif

# elif RT_INLINE_ASM_USES_INTRIN
    int aInfo[4];
    __cpuid(aInfo, 1);
    xBX = aInfo[1];

# else
    __asm
    {
        push    ebx
        mov     eax, 1
        cpuid
        mov     [xBX], ebx
        pop     ebx
    }
# endif
    return (uint8_t)(xBX >> 24);
}
#endif

/**
 * Get cr0.
 * @returns cr0.
 */
#if RT_INLINE_ASM_EXTERNAL && !RT_INLINE_ASM_USES_INTRIN
DECLASM(RTCCUINTREG) ASMGetCR0(void);
#else
DECLINLINE(RTCCUINTREG) ASMGetCR0(void)
{
    RTCCUINTREG uCR0;
# if RT_INLINE_ASM_USES_INTRIN
    uCR0 = __readcr0();

# elif RT_INLINE_ASM_GNU_STYLE
#  ifdef __AMD64__
    __asm__ ("movq  %%cr0, %0\t\n" : "=r" (uCR0));
#  else
    __asm__ ("movl  %%cr0, %0\t\n" : "=r" (uCR0));
#  endif
# else
    __asm
    {
#  ifdef __AMD64__
        mov     rax, cr0
        mov     [uCR0], rax
#  else
        mov     eax, cr0
        mov     [uCR0], eax
#  endif
    }
# endif
    return uCR0;
}
#endif


/**
 * Sets the CR0 register.
 * @param   uCR0 The new CR0 value.
 */
#if RT_INLINE_ASM_EXTERNAL && !RT_INLINE_ASM_USES_INTRIN
DECLASM(void) ASMSetCR0(RTCCUINTREG uCR0);
#else
DECLINLINE(void) ASMSetCR0(RTCCUINTREG uCR0)
{
# if RT_INLINE_ASM_USES_INTRIN
    __writecr0(uCR0);

# elif RT_INLINE_ASM_GNU_STYLE
#  ifdef __AMD64__
    __asm__ __volatile__("movq %0, %%cr0\n\t" :: "r" (uCR0));
#  else
    __asm__ __volatile__("movl %0, %%cr0\n\t" :: "r" (uCR0));
#  endif
# else
    __asm
    {
#  ifdef __AMD64__
        mov     rax, [uCR0]
        mov     cr0, rax
#  else
        mov     eax, [uCR0]
        mov     cr0, eax
#  endif
    }
# endif
}
#endif


/**
 * Get cr2.
 * @returns cr2.
 */
#if RT_INLINE_ASM_EXTERNAL && !RT_INLINE_ASM_USES_INTRIN
DECLASM(RTCCUINTREG) ASMGetCR2(void);
#else
DECLINLINE(RTCCUINTREG) ASMGetCR2(void)
{
    RTCCUINTREG uCR2;
# if RT_INLINE_ASM_USES_INTRIN
    uCR2 = __readcr2();

# elif RT_INLINE_ASM_GNU_STYLE
#  ifdef __AMD64__
    __asm__ ("movq  %%cr2, %0\t\n" : "=r" (uCR2));
#  else
    __asm__ ("movl  %%cr2, %0\t\n" : "=r" (uCR2));
#  endif
# else
    __asm
    {
#  ifdef __AMD64__
        mov     rax, cr2
        mov     [uCR2], rax
#  else
        mov     eax, cr2
        mov     [uCR2], eax
#  endif
    }
# endif
    return uCR2;
}
#endif


/**
 * Sets the CR2 register.
 * @param   uCR2 The new CR0 value.
 */
#if RT_INLINE_ASM_EXTERNAL
DECLASM(void) ASMSetCR2(RTCCUINTREG uCR2);
#else
DECLINLINE(void) ASMSetCR2(RTCCUINTREG uCR2)
{
# if RT_INLINE_ASM_GNU_STYLE
#  ifdef __AMD64__
    __asm__ __volatile__("movq %0, %%cr2\n\t" :: "r" (uCR2));
#  else
    __asm__ __volatile__("movl %0, %%cr2\n\t" :: "r" (uCR2));
#  endif
# else
    __asm
    {
#  ifdef __AMD64__
        mov     rax, [uCR2]
        mov     cr2, rax
#  else
        mov     eax, [uCR2]
        mov     cr2, eax
#  endif
    }
# endif
}
#endif


/**
 * Get cr3.
 * @returns cr3.
 */
#if RT_INLINE_ASM_EXTERNAL && !RT_INLINE_ASM_USES_INTRIN
DECLASM(RTCCUINTREG) ASMGetCR3(void);
#else
DECLINLINE(RTCCUINTREG) ASMGetCR3(void)
{
    RTCCUINTREG uCR3;
# if RT_INLINE_ASM_USES_INTRIN
    uCR3 = __readcr3();

# elif RT_INLINE_ASM_GNU_STYLE
#  ifdef __AMD64__
    __asm__ ("movq  %%cr3, %0\t\n" : "=r" (uCR3));
#  else
    __asm__ ("movl  %%cr3, %0\t\n" : "=r" (uCR3));
#  endif
# else
    __asm
    {
#  ifdef __AMD64__
        mov     rax, cr3
        mov     [uCR3], rax
#  else
        mov     eax, cr3
        mov     [uCR3], eax
#  endif
    }
# endif
    return uCR3;
}
#endif


/**
 * Sets the CR3 register.
 *
 * @param   uCR3    New CR3 value.
 */
#if RT_INLINE_ASM_EXTERNAL && !RT_INLINE_ASM_USES_INTRIN
DECLASM(void) ASMSetCR3(RTCCUINTREG uCR3);
#else
DECLINLINE(void) ASMSetCR3(RTCCUINTREG uCR3)
{
# if RT_INLINE_ASM_USES_INTRIN
    __writecr3(uCR3);

# elif RT_INLINE_ASM_GNU_STYLE
#  ifdef __AMD64__
    __asm__ __volatile__ ("movq %0, %%cr3\n\t" : : "r" (uCR3));
#  else
    __asm__ __volatile__ ("movl %0, %%cr3\n\t" : : "r" (uCR3));
#  endif
# else
    __asm
    {
#  ifdef __AMD64__
        mov     rax, [uCR3]
        mov     cr3, rax
#  else
        mov     eax, [uCR3]
        mov     cr3, eax
#  endif
    }
# endif
}
#endif


/**
 * Reloads the CR3 register.
 */
#if RT_INLINE_ASM_EXTERNAL && !RT_INLINE_ASM_USES_INTRIN
DECLASM(void) ASMReloadCR3(void);
#else
DECLINLINE(void) ASMReloadCR3(void)
{
# if RT_INLINE_ASM_USES_INTRIN
    __writecr3(__readcr3());

# elif RT_INLINE_ASM_GNU_STYLE
    RTCCUINTREG u;
#  ifdef __AMD64__
    __asm__ __volatile__ ("movq %%cr3, %0\n\t"
                          "movq %0, %%cr3\n\t"
                          : "=r" (u));
#  else
    __asm__ __volatile__ ("movl %%cr3, %0\n\t"
                          "movl %0, %%cr3\n\t"
                          : "=r" (u));
#  endif
# else
    __asm
    {
#  ifdef __AMD64__
        mov     rax, cr3
        mov     cr3, rax
#  else
        mov     eax, cr3
        mov     cr3, eax
#  endif
    }
# endif
}
#endif


/**
 * Get cr4.
 * @returns cr4.
 */
#if RT_INLINE_ASM_EXTERNAL && !RT_INLINE_ASM_USES_INTRIN
DECLASM(RTCCUINTREG) ASMGetCR4(void);
#else
DECLINLINE(RTCCUINTREG) ASMGetCR4(void)
{
    RTCCUINTREG uCR4;
# if RT_INLINE_ASM_USES_INTRIN
    uCR4 = __readcr4();

# elif RT_INLINE_ASM_GNU_STYLE
#  ifdef __AMD64__
    __asm__ ("movq  %%cr4, %0\t\n" : "=r" (uCR4));
#  else
    __asm__ ("movl  %%cr4, %0\t\n" : "=r" (uCR4));
#  endif
# else
    __asm
    {
#  ifdef __AMD64__
        mov     rax, cr4
        mov     [uCR4], rax
#  else
        push    eax /* just in case */
        /*mov     eax, cr4*/
        _emit 0x0f
        _emit 0x20
        _emit 0xe0
        mov     [uCR4], eax
        pop     eax
#  endif
    }
# endif
    return uCR4;
}
#endif


/**
 * Sets the CR4 register.
 *
 * @param   uCR4    New CR4 value.
 */
#if RT_INLINE_ASM_EXTERNAL && !RT_INLINE_ASM_USES_INTRIN
DECLASM(void) ASMSetCR4(RTCCUINTREG uCR4);
#else
DECLINLINE(void) ASMSetCR4(RTCCUINTREG uCR4)
{
# if RT_INLINE_ASM_USES_INTRIN
    __writecr4(uCR4);

# elif RT_INLINE_ASM_GNU_STYLE
#  ifdef __AMD64__
    __asm__ __volatile__ ("movq %0, %%cr4\n\t" : : "r" (uCR4));
#  else
    __asm__ __volatile__ ("movl %0, %%cr4\n\t" : : "r" (uCR4));
#  endif
# else
    __asm
    {
#  ifdef __AMD64__
        mov     rax, [uCR4]
        mov     cr4, rax
#  else
        mov     eax, [uCR4]
        _emit   0x0F
        _emit   0x22
        _emit   0xE0        /* mov     cr4, eax */
#  endif
    }
# endif
}
#endif


/**
 * Get cr8.
 * @returns cr8.
 * @remark  The lock prefix hack for access from non-64-bit modes is NOT used and 0 is returned.
 */
#if RT_INLINE_ASM_EXTERNAL && !RT_INLINE_ASM_USES_INTRIN
DECLASM(RTCCUINTREG) ASMGetCR8(void);
#else
DECLINLINE(RTCCUINTREG) ASMGetCR8(void)
{
# ifdef __AMD64__
    RTCCUINTREG uCR8;
#  if RT_INLINE_ASM_USES_INTRIN
    uCR8 = __readcr8();

#  elif RT_INLINE_ASM_GNU_STYLE
    __asm__ ("movq  %%cr8, %0\t\n" : "=r" (uCR8));
#  else
    __asm
    {
        mov     rax, cr8
        mov     [uCR8], rax
    }
#  endif
    return uCR8;
# else /* !__AMD64__ */
    return 0;
# endif /* !__AMD64__ */
}
#endif


/**
 * Enables interrupts (EFLAGS.IF).
 */
#if RT_INLINE_ASM_EXTERNAL && !RT_INLINE_ASM_USES_INTRIN
DECLASM(void) ASMIntEnable(void);
#else
DECLINLINE(void) ASMIntEnable(void)
{
# if RT_INLINE_ASM_GNU_STYLE
    __asm("sti\n");
# elif RT_INLINE_ASM_USES_INTRIN
    _enable();
# else
    __asm sti
# endif
}
#endif


/**
 * Disables interrupts (!EFLAGS.IF).
 */
#if RT_INLINE_ASM_EXTERNAL && !RT_INLINE_ASM_USES_INTRIN
DECLASM(void) ASMIntDisable(void);
#else
DECLINLINE(void) ASMIntDisable(void)
{
# if RT_INLINE_ASM_GNU_STYLE
    __asm("cli\n");
# elif RT_INLINE_ASM_USES_INTRIN
    _disable();
# else
    __asm cli
# endif
}
#endif


/**
 * Disables interrupts and returns previous xFLAGS.
 */
#if RT_INLINE_ASM_EXTERNAL && !RT_INLINE_ASM_USES_INTRIN
DECLASM(RTCCUINTREG) ASMIntDisableFlags(void);
#else
DECLINLINE(RTCCUINTREG) ASMIntDisableFlags(void)
{
    RTCCUINTREG xFlags;
# if RT_INLINE_ASM_GNU_STYLE
#  ifdef __AMD64__
    __asm__ __volatile__("pushfq\n\t"
                         "cli\n\t"
                         "popq  %0\n\t"
                         : "=m" (xFlags));
#  else
    __asm__ __volatile__("pushfl\n\t"
                         "cli\n\t"
                         "popl  %0\n\t"
                         : "=m" (xFlags));
#  endif
# elif RT_INLINE_ASM_USES_INTRIN && !defined(__X86__)
    xFlags = ASMGetFlags();
    _disable();
# else
    __asm {
        pushfd
        cli
        pop  [xFlags]
    }
# endif
    return xFlags;
}
#endif


/**
 * Reads a machine specific register.
 *
 * @returns Register content.
 * @param   uRegister   Register to read.
 */
#if RT_INLINE_ASM_EXTERNAL && !RT_INLINE_ASM_USES_INTRIN
DECLASM(uint64_t) ASMRdMsr(uint32_t uRegister);
#else
DECLINLINE(uint64_t) ASMRdMsr(uint32_t uRegister)
{
    RTUINT64U u;
# if RT_INLINE_ASM_GNU_STYLE
    __asm__ ("rdmsr\n\t"
             : "=a" (u.s.Lo),
               "=d" (u.s.Hi)
             : "c" (uRegister));

# elif RT_INLINE_ASM_USES_INTRIN
    u.u = __readmsr(uRegister);

# else
    __asm
    {
        mov     ecx, [uRegister]
        rdmsr
        mov     [u.s.Lo], eax
        mov     [u.s.Hi], edx
    }
# endif

    return u.u;
}
#endif


/**
 * Writes a machine specific register.
 *
 * @returns Register content.
 * @param   uRegister   Register to write to.
 * @param   u64Val      Value to write.
 */
#if RT_INLINE_ASM_EXTERNAL && !RT_INLINE_ASM_USES_INTRIN
DECLASM(void) ASMWrMsr(uint32_t uRegister, uint64_t u64Val);
#else
DECLINLINE(void) ASMWrMsr(uint32_t uRegister, uint64_t u64Val)
{
    RTUINT64U u;

    u.u = u64Val;
# if RT_INLINE_ASM_GNU_STYLE
    __asm__ __volatile__("wrmsr\n\t"
                         ::"a" (u.s.Lo),
                           "d" (u.s.Hi),
                           "c" (uRegister));

# elif RT_INLINE_ASM_USES_INTRIN
    __writemsr(uRegister, u.u);

# else
    __asm
    {
        mov     ecx, [uRegister]
        mov     edx, [u.s.Hi]
        mov     eax, [u.s.Lo]
        wrmsr
    }
# endif
}
#endif


/**
 * Reads low part of a machine specific register.
 *
 * @returns Register content.
 * @param   uRegister   Register to read.
 */
#if RT_INLINE_ASM_EXTERNAL && !RT_INLINE_ASM_USES_INTRIN
DECLASM(uint32_t) ASMRdMsr_Low(uint32_t uRegister);
#else
DECLINLINE(uint32_t) ASMRdMsr_Low(uint32_t uRegister)
{
    uint32_t u32;
# if RT_INLINE_ASM_GNU_STYLE
    __asm__ ("rdmsr\n\t"
             : "=a" (u32)
             : "c" (uRegister)
             : "edx");

# elif RT_INLINE_ASM_USES_INTRIN
    u32 = (uint32_t)__readmsr(uRegister);

#else
    __asm
    {
        mov     ecx, [uRegister]
        rdmsr
        mov     [u32], eax
    }
# endif

    return u32;
}
#endif


/**
 * Reads high part of a machine specific register.
 *
 * @returns Register content.
 * @param   uRegister   Register to read.
 */
#if RT_INLINE_ASM_EXTERNAL && !RT_INLINE_ASM_USES_INTRIN
DECLASM(uint32_t) ASMRdMsr_High(uint32_t uRegister);
#else
DECLINLINE(uint32_t) ASMRdMsr_High(uint32_t uRegister)
{
    uint32_t    u32;
# if RT_INLINE_ASM_GNU_STYLE
    __asm__ ("rdmsr\n\t"
             : "=d" (u32)
             : "c" (uRegister)
             : "eax");

# elif RT_INLINE_ASM_USES_INTRIN
    u32 = (uint32_t)(__readmsr(uRegister) >> 32);

# else
    __asm
    {
        mov     ecx, [uRegister]
        rdmsr
        mov     [u32], edx
    }
# endif

    return u32;
}
#endif


/**
 * Gets dr7.
 *
 * @returns dr7.
 */
#if RT_INLINE_ASM_EXTERNAL
DECLASM(RTCCUINTREG) ASMGetDR7(void);
#else
DECLINLINE(RTCCUINTREG) ASMGetDR7(void)
{
    RTCCUINTREG uDR7;
# if RT_INLINE_ASM_GNU_STYLE
#  ifdef __AMD64__
    __asm__ ("movq   %%dr7, %0\n\t" : "=r" (uDR7));
#  else
    __asm__ ("movl   %%dr7, %0\n\t" : "=r" (uDR7));
#  endif
# else
    __asm
    {
#  ifdef __AMD64__
        mov     rax, dr7
        mov     [uDR7], rax
#  else
        mov     eax, dr7
        mov     [uDR7], eax
#  endif
    }
# endif
    return uDR7;
}
#endif


/**
 * Gets dr6.
 *
 * @returns dr6.
 */
#if RT_INLINE_ASM_EXTERNAL
DECLASM(RTCCUINTREG) ASMGetDR6(void);
#else
DECLINLINE(RTCCUINTREG) ASMGetDR6(void)
{
    RTCCUINTREG uDR6;
# if RT_INLINE_ASM_GNU_STYLE
#  ifdef __AMD64__
    __asm__ ("movq   %%dr6, %0\n\t" : "=r" (uDR6));
#  else
    __asm__ ("movl   %%dr6, %0\n\t" : "=r" (uDR6));
#  endif
# else
    __asm
    {
#  ifdef __AMD64__
        mov     rax, dr6
        mov     [uDR6], rax
#  else
        mov     eax, dr6
        mov     [uDR6], eax
#  endif
    }
# endif
    return uDR6;
}
#endif


/**
 * Reads and clears DR6.
 *
 * @returns DR6.
 */
#if RT_INLINE_ASM_EXTERNAL
DECLASM(RTCCUINTREG) ASMGetAndClearDR6(void);
#else
DECLINLINE(RTCCUINTREG) ASMGetAndClearDR6(void)
{
    RTCCUINTREG uDR6;
# if RT_INLINE_ASM_GNU_STYLE
    RTCCUINTREG uNewValue =  0xffff0ff0;  /* 31-16 and 4-11 are 1's, 12 and 63-31 are zero. */
#  ifdef __AMD64__
    __asm__ ("movq   %%dr6, %0\n\t"
             "movq   %1, %%dr6\n\t"
             : "=r" (uDR6)
             : "r" (uNewValue));
#  else
    __asm__ ("movl   %%dr6, %0\n\t"
             "movl   %1, %%dr6\n\t"
             : "=r" (uDR6)
             : "r" (uNewValue));
#  endif
# else
    __asm
    {
#  ifdef __AMD64__
        mov     rax, dr6
        mov     [uDR6], rax
        mov     rcx, rax
        mov     ecx, 0ffff0ff0h;        /* 31-16 and 4-11 are 1's, 12 and 63-31 are zero. */
        mov     dr6, rcx
#  else
        mov     eax, dr6
        mov     [uDR6], eax
        mov     ecx, 0ffff0ff0h;        /* 31-16 and 4-11 are 1's, 12 is zero. */
        mov     dr6, ecx
#  endif
    }
# endif
    return uDR6;
}
#endif


/** @deprecated */
#define ASMOutB(p, b)   ASMOutU8(p,b)
/** @deprecated */
#define ASMInB(p)       ASMInU8(p)

/**
 * Writes a 8-bit unsigned integer to an I/O port.
 *
 * @param   Port    I/O port to read from.
 * @param   u8      8-bit integer to write.
 */
#if RT_INLINE_ASM_EXTERNAL && !RT_INLINE_ASM_USES_INTRIN
DECLASM(void) ASMOutU8(RTIOPORT Port, uint8_t u8);
#else
DECLINLINE(void) ASMOutU8(RTIOPORT Port, uint8_t u8)
{
# if RT_INLINE_ASM_GNU_STYLE
    __asm__ __volatile__("outb %b1, %w0\n\t"
                         :: "Nd" (Port),
                            "a" (u8));

# elif RT_INLINE_ASM_USES_INTRIN
    __outbyte(Port, u8);

# else
    __asm
    {
        mov     dx, [Port]
        mov     al, [u8]
        out     dx, al
    }
# endif
}
#endif


/**
 * Gets a 8-bit unsigned integer from an I/O port.
 *
 * @returns 8-bit integer.
 * @param   Port    I/O port to read from.
 */
#if RT_INLINE_ASM_EXTERNAL && !RT_INLINE_ASM_USES_INTRIN
DECLASM(uint8_t) ASMInU8(RTIOPORT Port);
#else
DECLINLINE(uint8_t) ASMInU8(RTIOPORT Port)
{
    uint8_t u8;
# if RT_INLINE_ASM_GNU_STYLE
    __asm__ __volatile__("inb %w1, %b0\n\t"
                         : "=a" (u8)
                         : "Nd" (Port));

# elif RT_INLINE_ASM_USES_INTRIN
    u8 = __inbyte(Port);

# else
    __asm
    {
        mov     dx, [Port]
        in      al, dx
        mov     [u8], al
    }
# endif
    return u8;
}
#endif


/**
 * Writes a 16-bit unsigned integer to an I/O port.
 *
 * @param   Port    I/O port to read from.
 * @param   u16     16-bit integer to write.
 */
#if RT_INLINE_ASM_EXTERNAL && !RT_INLINE_ASM_USES_INTRIN
DECLASM(void) ASMOutU16(RTIOPORT Port, uint16_t u16);
#else
DECLINLINE(void) ASMOutU16(RTIOPORT Port, uint16_t u16)
{
# if RT_INLINE_ASM_GNU_STYLE
    __asm__ __volatile__("outw %w1, %w0\n\t"
                         :: "Nd" (Port),
                            "a" (u16));

# elif RT_INLINE_ASM_USES_INTRIN
    __outword(Port, u16);

# else
    __asm
    {
        mov     dx, [Port]
        mov     ax, [u16]
        out     dx, ax
    }
# endif
}
#endif


/**
 * Gets a 16-bit unsigned integer from an I/O port.
 *
 * @returns 16-bit integer.
 * @param   Port    I/O port to read from.
 */
#if RT_INLINE_ASM_EXTERNAL && !RT_INLINE_ASM_USES_INTRIN
DECLASM(uint16_t) ASMInU16(RTIOPORT Port);
#else
DECLINLINE(uint16_t) ASMInU16(RTIOPORT Port)
{
    uint16_t u16;
# if RT_INLINE_ASM_GNU_STYLE
    __asm__ __volatile__("inw %w1, %w0\n\t"
                         : "=a" (u16)
                         : "Nd" (Port));

# elif RT_INLINE_ASM_USES_INTRIN
    u16 = __inword(Port);

# else
    __asm
    {
        mov     dx, [Port]
        in      ax, dx
        mov     [u16], ax
    }
# endif
    return u16;
}
#endif


/**
 * Writes a 32-bit unsigned integer to an I/O port.
 *
 * @param   Port    I/O port to read from.
 * @param   u32     32-bit integer to write.
 */
#if RT_INLINE_ASM_EXTERNAL && !RT_INLINE_ASM_USES_INTRIN
DECLASM(void) ASMOutU32(RTIOPORT Port, uint32_t u32);
#else
DECLINLINE(void) ASMOutU32(RTIOPORT Port, uint32_t u32)
{
# if RT_INLINE_ASM_GNU_STYLE
    __asm__ __volatile__("outl %1, %w0\n\t"
                         :: "Nd" (Port),
                            "a" (u32));

# elif RT_INLINE_ASM_USES_INTRIN
    __outdword(Port, u32);

# else
    __asm
    {
        mov     dx, [Port]
        mov     eax, [u32]
        out     dx, eax
    }
# endif
}
#endif


/**
 * Gets a 32-bit unsigned integer from an I/O port.
 *
 * @returns 32-bit integer.
 * @param   Port    I/O port to read from.
 */
#if RT_INLINE_ASM_EXTERNAL && !RT_INLINE_ASM_USES_INTRIN
DECLASM(uint32_t) ASMInU32(RTIOPORT Port);
#else
DECLINLINE(uint32_t) ASMInU32(RTIOPORT Port)
{
    uint32_t u32;
# if RT_INLINE_ASM_GNU_STYLE
    __asm__ __volatile__("inl %w1, %0\n\t"
                         : "=a" (u32)
                         : "Nd" (Port));

# elif RT_INLINE_ASM_USES_INTRIN
    u32 = __indword(Port);

# else
    __asm
    {
        mov     dx, [Port]
        in      eax, dx
        mov     [u32], eax
    }
# endif
    return u32;
}
#endif


/**
 * Atomically Exchange an unsigned 8-bit value.
 *
 * @returns Current *pu8 value
 * @param   pu8    Pointer to the 8-bit variable to update.
 * @param   u8     The 8-bit value to assign to *pu8.
 */
#if RT_INLINE_ASM_EXTERNAL
DECLASM(uint8_t) ASMAtomicXchgU8(volatile uint8_t *pu8, uint8_t u8);
#else
DECLINLINE(uint8_t) ASMAtomicXchgU8(volatile uint8_t *pu8, uint8_t u8)
{
# if RT_INLINE_ASM_GNU_STYLE
    __asm__ __volatile__("xchgb %0, %1\n\t"
                         : "=m" (*pu8),
                           "=r" (u8)
                         : "1" (u8));
# else
    __asm
    {
#  ifdef __AMD64__
        mov     rdx, [pu8]
        mov     al, [u8]
        xchg    [rdx], al
        mov     [u8], al
#  else
        mov     edx, [pu8]
        mov     al, [u8]
        xchg    [edx], al
        mov     [u8], al
#  endif
    }
# endif
    return u8;
}
#endif


/**
 * Atomically Exchange a signed 8-bit value.
 *
 * @returns Current *pu8 value
 * @param   pi8     Pointer to the 8-bit variable to update.
 * @param   i8      The 8-bit value to assign to *pi8.
 */
DECLINLINE(int8_t) ASMAtomicXchgS8(volatile int8_t *pi8, int8_t i8)
{
    return (int8_t)ASMAtomicXchgU8((volatile uint8_t *)pi8, (uint8_t)i8);
}


/**
 * Atomically Exchange an unsigned 16-bit value.
 *
 * @returns Current *pu16 value
 * @param   pu16    Pointer to the 16-bit variable to update.
 * @param   u16     The 16-bit value to assign to *pu16.
 */
#if RT_INLINE_ASM_EXTERNAL
DECLASM(uint16_t) ASMAtomicXchgU16(volatile uint16_t *pu16, uint16_t u16);
#else
DECLINLINE(uint16_t) ASMAtomicXchgU16(volatile uint16_t *pu16, uint16_t u16)
{
# if RT_INLINE_ASM_GNU_STYLE
    __asm__ __volatile__("xchgw %0, %1\n\t"
                         : "=m" (*pu16),
                           "=r" (u16)
                         : "1" (u16));
# else
    __asm
    {
#  ifdef __AMD64__
        mov     rdx, [pu16]
        mov     ax, [u16]
        xchg    [rdx], ax
        mov     [u16], ax
#  else
        mov     edx, [pu16]
        mov     ax, [u16]
        xchg    [edx], ax
        mov     [u16], ax
#  endif
    }
# endif
    return u16;
}
#endif


/**
 * Atomically Exchange a signed 16-bit value.
 *
 * @returns Current *pu16 value
 * @param   pi16    Pointer to the 16-bit variable to update.
 * @param   i16     The 16-bit value to assign to *pi16.
 */
DECLINLINE(int16_t) ASMAtomicXchgS16(volatile int16_t *pi16, int16_t i16)
{
    return (int16_t)ASMAtomicXchgU16((volatile uint16_t *)pi16, (uint16_t)i16);
}


/**
 * Atomically Exchange an unsigned 32-bit value.
 *
 * @returns Current *pu32 value
 * @param   pu32    Pointer to the 32-bit variable to update.
 * @param   u32     The 32-bit value to assign to *pu32.
 */
#if RT_INLINE_ASM_EXTERNAL && !RT_INLINE_ASM_USES_INTRIN
DECLASM(uint32_t) ASMAtomicXchgU32(volatile uint32_t *pu32, uint32_t u32);
#else
DECLINLINE(uint32_t) ASMAtomicXchgU32(volatile uint32_t *pu32, uint32_t u32)
{
# if RT_INLINE_ASM_GNU_STYLE
    __asm__ __volatile__("xchgl %0, %1\n\t"
                         : "=m" (*pu32),
                           "=r" (u32)
                         : "1" (u32));

# elif RT_INLINE_ASM_USES_INTRIN
   u32 = _InterlockedExchange((long *)pu32, u32);

# else
    __asm
    {
#  ifdef __AMD64__
        mov     rdx, [pu32]
        mov     eax, u32
        xchg    [rdx], eax
        mov     [u32], eax
#  else
        mov     edx, [pu32]
        mov     eax, u32
        xchg    [edx], eax
        mov     [u32], eax
#  endif
    }
# endif
    return u32;
}
#endif


/**
 * Atomically Exchange a signed 32-bit value.
 *
 * @returns Current *pu32 value
 * @param   pi32    Pointer to the 32-bit variable to update.
 * @param   i32     The 32-bit value to assign to *pi32.
 */
DECLINLINE(int32_t) ASMAtomicXchgS32(volatile int32_t *pi32, int32_t i32)
{
    return (int32_t)ASMAtomicXchgU32((volatile uint32_t *)pi32, (uint32_t)i32);
}


/**
 * Atomically Exchange an unsigned 64-bit value.
 *
 * @returns Current *pu64 value
 * @param   pu64    Pointer to the 64-bit variable to update.
 * @param   u64     The 64-bit value to assign to *pu64.
 */
#if RT_INLINE_ASM_EXTERNAL && !RT_INLINE_ASM_USES_INTRIN
DECLASM(uint64_t) ASMAtomicXchgU64(volatile uint64_t *pu64, uint64_t u64);
#else
DECLINLINE(uint64_t) ASMAtomicXchgU64(volatile uint64_t *pu64, uint64_t u64)
{
# if defined(__AMD64__)
#  if RT_INLINE_ASM_USES_INTRIN
   u64 = _InterlockedExchange64((__int64 *)pu64, u64);

#  elif RT_INLINE_ASM_GNU_STYLE
    __asm__ __volatile__("xchgq %0, %1\n\t"
                         : "=m" (*pu64),
                           "=r" (u64)
                         : "1" (u64));
#  else
    __asm
    {
        mov     rdx, [pu64]
        mov     rax, [u64]
        xchg    [rdx], rax
        mov     [u64], rax
    }
#  endif
# else /* !__AMD64__ */
#  if RT_INLINE_ASM_GNU_STYLE
#   if defined(PIC) || defined(__DARWIN__) /* darwin: 4.0.1 compiler option / bug? */
    uint32_t u32 = (uint32_t)u64;
    __asm__ __volatile__(/*"xchgl %%esi, %5\n\t"*/
                         "xchgl %%ebx, %3\n\t"
                         "1:\n\t"
                         "lock; cmpxchg8b (%5)\n\t"
                         "jnz 1b\n\t"
                         "xchgl %%ebx, %3\n\t"
                         /*"xchgl %%esi, %5\n\t"*/
                         : "=A" (u64),
                           "=m" (*pu64)
                         : "0" (*pu64),
                           "m" ( u32 ),
                           "c" ( (uint32_t)(u64 >> 32) ),
                           "S" (pu64) );
#   else /* !PIC */
    __asm__ __volatile__("1:\n\t"
                         "lock; cmpxchg8b %1\n\t"
                         "jnz 1b\n\t"
                         : "=A" (u64),
                           "=m" (*pu64)
                         : "0" (*pu64),
                           "b" ( (uint32_t)u64 ),
                           "c" ( (uint32_t)(u64 >> 32) ));
#   endif
#  else
    __asm
    {
        mov     ebx, dword ptr [u64]
        mov     ecx, dword ptr [u64 + 4]
        mov     edi, pu64
        mov     eax, dword ptr [edi]
        mov     edx, dword ptr [edi + 4]
    retry:
        lock cmpxchg8b [edi]
        jnz retry
        mov     dword ptr [u64], eax
        mov     dword ptr [u64 + 4], edx
    }
#  endif
# endif /* !__AMD64__ */
    return u64;
}
#endif


/**
 * Atomically Exchange an signed 64-bit value.
 *
 * @returns Current *pi64 value
 * @param   pi64    Pointer to the 64-bit variable to update.
 * @param   i64     The 64-bit value to assign to *pi64.
 */
DECLINLINE(int64_t) ASMAtomicXchgS64(volatile int64_t *pi64, int64_t i64)
{
    return (int64_t)ASMAtomicXchgU64((volatile uint64_t *)pi64, (uint64_t)i64);
}


#ifdef __AMD64__
/**
 * Atomically Exchange an unsigned 128-bit value.
 *
 * @returns Current *pu128.
 * @param   pu128   Pointer to the 128-bit variable to update.
 * @param   u128    The 128-bit value to assign to *pu128.
 *
 * @remark  We cannot really assume that any hardware supports this. Nor do I have
 *          GAS support for it. So, for the time being we'll BREAK the atomic
 *          bit of this function and use two 64-bit exchanges instead.
 */
# if 0 /* see remark RT_INLINE_ASM_EXTERNAL */
DECLASM(uint128_t) ASMAtomicXchgU128(volatile uint128_t *pu128, uint128_t u128);
# else
DECLINLINE(uint128_t) ASMAtomicXchgU128(volatile uint128_t *pu128, uint128_t u128)
{
   if (true)/*ASMCpuId_ECX(1) & BIT(13))*/
   {
       /** @todo this is clumsy code */
       RTUINT128U u128Ret;
       u128Ret.u = u128;
       u128Ret.s.Lo = ASMAtomicXchgU64(&((PRTUINT128U)(uintptr_t)pu128)->s.Lo, u128Ret.s.Lo);
       u128Ret.s.Hi = ASMAtomicXchgU64(&((PRTUINT128U)(uintptr_t)pu128)->s.Hi, u128Ret.s.Hi);
       return u128Ret.u;
   }
#if 0  /* later? */
   else
   {
#  if RT_INLINE_ASM_GNU_STYLE
        __asm__ __volatile__("1:\n\t"
                             "lock; cmpxchg8b %1\n\t"
                             "jnz 1b\n\t"
                             : "=A" (u128),
                               "=m" (*pu128)
                             : "0" (*pu128),
                               "b" ( (uint64_t)u128 ),
                               "c" ( (uint64_t)(u128 >> 64) ));
#  else
        __asm
        {
            mov     rbx, dword ptr [u128]
            mov     rcx, dword ptr [u128 + 4]
            mov     rdi, pu128
            mov     rax, dword ptr [rdi]
            mov     rdx, dword ptr [rdi + 4]
        retry:
            lock cmpxchg16b [rdi]
            jnz retry
            mov     dword ptr [u128], rax
            mov     dword ptr [u128 + 4], rdx
        }
#  endif
    }
    return u128;
#endif
}
# endif
#endif /* __AMD64__ */


/**
 * Atomically Reads a unsigned 64-bit value.
 *
 * @returns Current *pu64 value
 * @param   pu64    Pointer to the 64-bit variable to read.
 *                  The memory pointed to must be writable.
 * @remark  This will fault if the memory is read-only!
 */
#if RT_INLINE_ASM_EXTERNAL
DECLASM(uint64_t) ASMAtomicReadU64(volatile uint64_t *pu64);
#else
DECLINLINE(uint64_t) ASMAtomicReadU64(volatile uint64_t *pu64)
{
    uint64_t u64;
# ifdef __AMD64__
#  if RT_INLINE_ASM_GNU_STYLE
    __asm__ __volatile__("movq %1, %0\n\t"
                         : "=r" (u64)
                         : "m" (*pu64));
#  else
    __asm
    {
        mov     rdx, [pu64]
        mov     rax, [rdx]
        mov     [u64], rax
    }
#  endif
# else /* !__AMD64__ */
#  if RT_INLINE_ASM_GNU_STYLE
#   if defined(PIC) || defined(__DARWIN__) /* darwin: 4.0.1 compiler option / bug? */
    uint32_t u32EBX = 0;
    __asm__ __volatile__("xchgl %%ebx, %3\n\t"
                         "lock; cmpxchg8b (%5)\n\t"
                         "xchgl %%ebx, %3\n\t"
                         : "=A" (u64),
                           "=m" (*pu64)
                         : "0" (0),
                           "m" (u32EBX),
                           "c" (0),
                           "S" (pu64));
#   else /* !PIC */
    __asm__ __volatile__("lock; cmpxchg8b %1\n\t"
                         : "=A" (u64),
                           "=m" (*pu64)
                         : "0" (0),
                           "b" (0),
                           "c" (0));
#   endif
#  else
    __asm
    {
        xor     eax, eax
        xor     edx, edx
        mov     edi, pu64
        xor     ecx, ecx
        xor     ebx, ebx
        lock cmpxchg8b [edi]
        mov     dword ptr [u64], eax
        mov     dword ptr [u64 + 4], edx
    }
#  endif
# endif /* !__AMD64__ */
    return u64;
}
#endif


/**
 * Atomically Reads a signed 64-bit value.
 *
 * @returns Current *pi64 value
 * @param   pi64    Pointer to the 64-bit variable to read.
 *                  The memory pointed to must be writable.
 * @remark  This will fault if the memory is read-only!
 */
DECLINLINE(int64_t) ASMAtomicReadS64(volatile int64_t *pi64)
{
    return (int64_t)ASMAtomicReadU64((volatile uint64_t *)pi64);
}


/**
 * Atomically Exchange a value which size might differ
 * between platforms or compilers.
 *
 * @param   pu      Pointer to the variable to update.
 * @param   uNew    The value to assign to *pu.
 */
#define ASMAtomicXchgSize(pu, uNew) \
    do { \
        switch (sizeof(*(pu))) { \
            case 1: ASMAtomicXchgU8((volatile uint8_t *)(void *)(pu), (uint8_t)(uNew)); break; \
            case 2: ASMAtomicXchgU16((volatile uint16_t *)(void *)(pu), (uint16_t)(uNew)); break; \
            case 4: ASMAtomicXchgU32((volatile uint32_t *)(void *)(pu), (uint32_t)(uNew)); break; \
            case 8: ASMAtomicXchgU64((volatile uint64_t *)(void *)(pu), (uint64_t)(uNew)); break; \
            default: AssertMsgFailed(("ASMAtomicXchgSize: size %d is not supported\n", sizeof(*(pu)))); \
        } \
    } while (0)


/**
 * Atomically Exchange a pointer value.
 *
 * @returns Current *ppv value
 * @param   ppv    Pointer to the pointer variable to update.
 * @param   pv     The pointer value to assign to *ppv.
 */
DECLINLINE(void *) ASMAtomicXchgPtr(void * volatile *ppv, void *pv)
{
#if ARCH_BITS == 32
    return (void *)ASMAtomicXchgU32((volatile uint32_t *)(void *)ppv, (uint32_t)pv);
#elif ARCH_BITS == 64
    return (void *)ASMAtomicXchgU64((volatile uint64_t *)(void *)ppv, (uint64_t)pv);
#else
# error "ARCH_BITS is bogus"
#endif
}


/**
 * Atomically Compare and Exchange an unsigned 32-bit value.
 *
 * @returns true if xchg was done.
 * @returns false if xchg wasn't done.
 *
 * @param   pu32        Pointer to the value to update.
 * @param   u32New      The new value to assigned to *pu32.
 * @param   u32Old      The old value to *pu32 compare with.
 */
#if RT_INLINE_ASM_EXTERNAL && !RT_INLINE_ASM_USES_INTRIN
DECLASM(bool) ASMAtomicCmpXchgU32(volatile uint32_t *pu32, const uint32_t u32New, const uint32_t u32Old);
#else
DECLINLINE(bool) ASMAtomicCmpXchgU32(volatile uint32_t *pu32, const uint32_t u32New, const uint32_t u32Old)
{
# if RT_INLINE_ASM_GNU_STYLE
    uint32_t u32Ret;
    __asm__ __volatile__("lock; cmpxchgl %2, %0\n\t"
                         "setz  %%al\n\t"
                         "movzx %%al, %%eax\n\t"
                         : "=m" (*pu32),
                           "=a" (u32Ret)
                         : "r" (u32New),
                           "1" (u32Old));
    return (bool)u32Ret;

# elif RT_INLINE_ASM_USES_INTRIN
    return _InterlockedCompareExchange((long *)pu32, u32New, u32Old) == u32Old;

# else
    uint32_t u32Ret;
    __asm
    {
#  ifdef __AMD64__
        mov     rdx, [pu32]
#  else
        mov     edx, [pu32]
#  endif
        mov     eax, [u32Old]
        mov     ecx, [u32New]
#  ifdef __AMD64__
        lock cmpxchg [rdx], ecx
#  else
        lock cmpxchg [edx], ecx
#  endif
        setz    al
        movzx   eax, al
        mov     [u32Ret], eax
    }
    return !!u32Ret;
# endif
}
#endif


/**
 * Atomically Compare and Exchange a signed 32-bit value.
 *
 * @returns true if xchg was done.
 * @returns false if xchg wasn't done.
 *
 * @param   pi32        Pointer to the value to update.
 * @param   i32New      The new value to assigned to *pi32.
 * @param   i32Old      The old value to *pi32 compare with.
 */
DECLINLINE(bool) ASMAtomicCmpXchgS32(volatile int32_t *pi32, const int32_t i32New, const int32_t i32Old)
{
    return ASMAtomicCmpXchgU32((volatile uint32_t *)pi32, (uint32_t)i32New, (uint32_t)i32Old);
}


/**
 * Atomically Compare and exchange an unsigned 64-bit value.
 *
 * @returns true if xchg was done.
 * @returns false if xchg wasn't done.
 *
 * @param   pu64    Pointer to the 64-bit variable to update.
 * @param   u64New  The 64-bit value to assign to *pu64.
 * @param   u64Old  The value to compare with.
 */
#if RT_INLINE_ASM_EXTERNAL && !RT_INLINE_ASM_USES_INTRIN
DECLASM(bool) ASMAtomicCmpXchgU64(volatile uint64_t *pu64, const uint64_t u64New, const uint64_t u64Old);
#else
DECLINLINE(bool) ASMAtomicCmpXchgU64(volatile uint64_t *pu64, const uint64_t u64New, const uint64_t u64Old)
{
# if RT_INLINE_ASM_USES_INTRIN
   return _InterlockedCompareExchange64((__int64 *)pu64, u64New, u64Old) == u64Old;

# elif defined(__AMD64__)
#  if RT_INLINE_ASM_GNU_STYLE
    uint64_t u64Ret;
    __asm__ __volatile__("lock; cmpxchgq %2, %0\n\t"
                         "setz  %%al\n\t"
                         "movzx %%al, %%eax\n\t"
                         : "=m" (*pu64),
                           "=a" (u64Ret)
                         : "r" (u64New),
                           "1" (u64Old));
    return (bool)u64Ret;
#  else
    bool fRet;
    __asm
    {
        mov     rdx, [pu32]
        mov     rax, [u64Old]
        mov     rcx, [u64New]
        lock cmpxchg [rdx], rcx
        setz    al
        mov     [fRet], al
    }
    return fRet;
#  endif
# else /* !__AMD64__ */
    uint32_t u32Ret;
#  if RT_INLINE_ASM_GNU_STYLE
#   if defined(PIC) || defined(__DARWIN__) /* darwin: 4.0.1 compiler option / bug? */
    uint32_t u32 = (uint32_t)u64New;
    __asm__ __volatile__("xchgl %%ebx, %3\n\t"
                         "lock; cmpxchg8b (%5)\n\t"
                         "setz  %%al\n\t"
                         "xchgl %%ebx, %3\n\t"
                         "movzx %%al, %%eax\n\t"
                         : "=a" (u32Ret),
                           "=m" (*pu64)
                         : "A" (u64Old),
                           "m" ( u32 ),
                           "c" ( (uint32_t)(u64New >> 32) ),
                           "S" (pu64) );
#   else /* !PIC */
    __asm__ __volatile__("lock; cmpxchg8b %1\n\t"
                         "setz  %%al\n\t"
                         "movzx %%al, %%eax\n\t"
                         : "=a" (u32Ret),
                           "=m" (*pu64)
                         : "A" (u64Old),
                           "b" ( (uint32_t)u64New ),
                           "c" ( (uint32_t)(u64New >> 32) ));
#   endif
    return (bool)u32Ret;
#  else
    __asm
    {
        mov     ebx, dword ptr [u64New]
        mov     ecx, dword ptr [u64New + 4]
        mov     edi, [pu64]
        mov     eax, dword ptr [u64Old]
        mov     edx, dword ptr [u64Old + 4]
        lock cmpxchg8b [edi]
        setz    al
        movzx   eax, al
        mov     dword ptr [u32Ret], eax
    }
    return !!u32Ret;
#  endif
# endif /* !__AMD64__ */
}
#endif


/**
 * Atomically Compare and exchange a signed 64-bit value.
 *
 * @returns true if xchg was done.
 * @returns false if xchg wasn't done.
 *
 * @param   pi64    Pointer to the 64-bit variable to update.
 * @param   i64     The 64-bit value to assign to *pu64.
 * @param   i64Old  The value to compare with.
 */
DECLINLINE(bool) ASMAtomicCmpXchgS64(volatile int64_t *pi64, const int64_t i64, const int64_t i64Old)
{
    return ASMAtomicCmpXchgU64((volatile uint64_t *)pi64, (uint64_t)i64, (uint64_t)i64Old);
}



/** @def ASMAtomicCmpXchgSize
 * Atomically Compare and Exchange a value which size might differ
 * between platforms or compilers.
 *
 * @param   pu          Pointer to the value to update.
 * @param   uNew        The new value to assigned to *pu.
 * @param   uOld        The old value to *pu compare with.
 * @param   fRc         Where to store the result.
 */
#define ASMAtomicCmpXchgSize(pu, uNew, uOld, fRc) \
    do { \
        switch (sizeof(*(pu))) { \
            case 4: (fRc) = ASMAtomicCmpXchgU32((volatile uint32_t *)(void *)(pu), (uint32_t)(uNew), (uint32_t)(uOld)); \
                break; \
            case 8: (fRc) = ASMAtomicCmpXchgU64((volatile uint64_t *)(void *)(pu), (uint64_t)(uNew), (uint64_t)(uOld)); \
                break; \
            default: AssertMsgFailed(("ASMAtomicCmpXchgSize: size %d is not supported\n", sizeof(*(pu)))); \
                (fRc) = false; \
                break; \
        } \
    } while (0)


/**
 * Atomically Compare and Exchange a pointer value.
 *
 * @returns true if xchg was done.
 * @returns false if xchg wasn't done.
 *
 * @param   ppv         Pointer to the value to update.
 * @param   pvNew       The new value to assigned to *ppv.
 * @param   pvOld       The old value to *ppv compare with.
 */
DECLINLINE(bool) ASMAtomicCmpXchgPtr(void * volatile *ppv, void *pvNew, void *pvOld)
{
#if ARCH_BITS == 32
    return ASMAtomicCmpXchgU32((volatile uint32_t *)(void *)ppv, (uint32_t)pvNew, (uint32_t)pvOld);
#elif ARCH_BITS == 64
    return ASMAtomicCmpXchgU64((volatile uint64_t *)(void *)ppv, (uint64_t)pvNew, (uint64_t)pvOld);
#else
# error "ARCH_BITS is bogus"
#endif
}


/**
 * Atomically increment a 32-bit value.
 *
 * @returns The new value.
 * @param   pu32        Pointer to the value to increment.
 */
#if RT_INLINE_ASM_EXTERNAL && !RT_INLINE_ASM_USES_INTRIN
DECLASM(uint32_t) ASMAtomicIncU32(uint32_t volatile *pu32);
#else
DECLINLINE(uint32_t) ASMAtomicIncU32(uint32_t volatile *pu32)
{
    uint32_t u32;
# if RT_INLINE_ASM_USES_INTRIN
    u32 = _InterlockedIncrement((long *)pu32);

# elif RT_INLINE_ASM_GNU_STYLE
    __asm__ __volatile__("lock; xaddl %0, %1\n\t"
                         "incl %0\n\t"
                         : "=r" (u32),
                           "=m" (*pu32)
                         : "0" (1)
                         : "memory");
# else
    __asm
    {
        mov     eax, 1
#  ifdef __AMD64__
        mov     rdx, [pu32]
        lock xadd [rdx], eax
#  else
        mov     edx, [pu32]
        lock xadd [edx], eax
#  endif
        inc     eax
        mov     u32, eax
    }
# endif
    return u32;
}
#endif


/**
 * Atomically increment a signed 32-bit value.
 *
 * @returns The new value.
 * @param   pi32        Pointer to the value to increment.
 */
DECLINLINE(int32_t) ASMAtomicIncS32(int32_t volatile *pi32)
{
    return (int32_t)ASMAtomicIncU32((uint32_t volatile *)pi32);
}


/**
 * Atomically decrement an unsigned 32-bit value.
 *
 * @returns The new value.
 * @param   pu32        Pointer to the value to decrement.
 */
#if RT_INLINE_ASM_EXTERNAL && !RT_INLINE_ASM_USES_INTRIN
DECLASM(uint32_t) ASMAtomicDecU32(uint32_t volatile *pu32);
#else
DECLINLINE(uint32_t) ASMAtomicDecU32(uint32_t volatile *pu32)
{
    uint32_t u32;
# if RT_INLINE_ASM_USES_INTRIN
    u32 = _InterlockedDecrement((long *)pu32);

# elif RT_INLINE_ASM_GNU_STYLE
    __asm__ __volatile__("lock; xaddl %0, %1\n\t"
                         "decl %0\n\t"
                         : "=r" (u32),
                           "=m" (*pu32)
                         : "0" (-1)
                         : "memory");
# else
    __asm
    {
        mov     eax, -1
#  ifdef __AMD64__
        mov     rdx, [pu32]
        lock xadd [rdx], eax
#  else
        mov     edx, [pu32]
        lock xadd [edx], eax
#  endif
        dec     eax
        mov     u32, eax
    }
# endif
    return u32;
}
#endif


/**
 * Atomically decrement a signed 32-bit value.
 *
 * @returns The new value.
 * @param   pi32        Pointer to the value to decrement.
 */
DECLINLINE(int32_t) ASMAtomicDecS32(int32_t volatile *pi32)
{
    return (int32_t)ASMAtomicDecU32((uint32_t volatile *)pi32);
}


/**
 * Atomically Or an unsigned 32-bit value.
 *
 * @param   pu32   Pointer to the pointer variable to OR u32 with.
 * @param   u32    The value to OR *pu32 with.
 */
#if RT_INLINE_ASM_EXTERNAL && !RT_INLINE_ASM_USES_INTRIN
DECLASM(void) ASMAtomicOrU32(uint32_t volatile *pu32, uint32_t u32);
#else
DECLINLINE(void) ASMAtomicOrU32(uint32_t volatile *pu32, uint32_t u32)
{
# if RT_INLINE_ASM_USES_INTRIN
    _InterlockedOr((long volatile *)pu32, (long)u32);

# elif RT_INLINE_ASM_GNU_STYLE
    __asm__ __volatile__("lock; orl %1, %0\n\t"
                         : "=m" (*pu32)
                         : "r" (u32));
# else
    __asm
    {
        mov     eax, [u32]
#  ifdef __AMD64__
        mov     rdx, [pu32]
        lock    or [rdx], eax
#  else
        mov     edx, [pu32]
        lock    or [edx], eax
#  endif
    }
# endif
}
#endif


/**
 * Atomically Or a signed 32-bit value.
 *
 * @param   pi32   Pointer to the pointer variable to OR u32 with.
 * @param   i32    The value to OR *pu32 with.
 */
DECLINLINE(void) ASMAtomicOrS32(int32_t volatile *pi32, int32_t i32)
{
    ASMAtomicOrU32((uint32_t volatile *)pi32, i32);
}


/**
 * Atomically And an unsigned 32-bit value.
 *
 * @param   pu32   Pointer to the pointer variable to AND u32 with.
 * @param   u32    The value to AND *pu32 with.
 */
#if RT_INLINE_ASM_EXTERNAL && !RT_INLINE_ASM_USES_INTRIN
DECLASM(void) ASMAtomicAndU32(uint32_t volatile *pu32, uint32_t u32);
#else
DECLINLINE(void) ASMAtomicAndU32(uint32_t volatile *pu32, uint32_t u32)
{
# if RT_INLINE_ASM_USES_INTRIN
    _InterlockedAnd((long volatile *)pu32, u32);

# elif RT_INLINE_ASM_GNU_STYLE
    __asm__ __volatile__("lock; andl %1, %0\n\t"
                         : "=m" (*pu32)
                         : "r" (u32));
# else
    __asm
    {
        mov     eax, [u32]
#  ifdef __AMD64__
        mov     rdx, [pu32]
        lock and [rdx], eax
#  else
        mov     edx, [pu32]
        lock and [edx], eax
#  endif
    }
# endif
}
#endif


/**
 * Atomically And a signed 32-bit value.
 *
 * @param   pi32   Pointer to the pointer variable to AND i32 with.
 * @param   i32    The value to AND *pi32 with.
 */
DECLINLINE(void) ASMAtomicAndS32(int32_t volatile *pi32, int32_t i32)
{
    ASMAtomicAndU32((uint32_t volatile *)pi32, (uint32_t)i32);
}


/**
 * Invalidate page.
 *
 * @param   pv      Address of the page to invalidate.
 */
#if RT_INLINE_ASM_EXTERNAL && !RT_INLINE_ASM_USES_INTRIN
DECLASM(void) ASMInvalidatePage(void *pv);
#else
DECLINLINE(void) ASMInvalidatePage(void *pv)
{
# if RT_INLINE_ASM_USES_INTRIN
    __invlpg(pv);

# elif RT_INLINE_ASM_GNU_STYLE
    __asm__ __volatile__("invlpg %0\n\t"
                         : : "m" (*(uint8_t *)pv));
# else
    __asm
    {
#  ifdef __AMD64__
        mov     rax, [pv]
        invlpg  [rax]
#  else
        mov     eax, [pv]
        invlpg  [eax]
#  endif
    }
# endif
}
#endif


#if defined(PAGE_SIZE) && !defined(NT_INCLUDED)
# if PAGE_SIZE != 0x1000
#  error "PAGE_SIZE is not 0x1000!"
# endif
#endif

/**
 * Zeros a 4K memory page.
 *
 * @param   pv  Pointer to the memory block. This must be page aligned.
 */
#if RT_INLINE_ASM_EXTERNAL && !RT_INLINE_ASM_USES_INTRIN
DECLASM(void) ASMMemZeroPage(volatile void *pv);
# else
DECLINLINE(void) ASMMemZeroPage(volatile void *pv)
{
#  if RT_INLINE_ASM_USES_INTRIN
#   ifdef __AMD64__
    __stosq((unsigned __int64 *)pv, 0, /*PAGE_SIZE*/0x1000 / 8);
#   else
    __stosd((unsigned long *)pv, 0, /*PAGE_SIZE*/0x1000 / 4);
#   endif

#  elif RT_INLINE_ASM_GNU_STYLE
    RTUINTREG uDummy;
#   ifdef __AMD64__
    __asm__ __volatile__ ("rep stosq"
                          : "=D" (pv),
                            "=c" (uDummy)
                          : "0" (pv),
                            "c" (0x1000 >> 3),
                            "a" (0)
                          : "memory");
#   else
    __asm__ __volatile__ ("rep stosl"
                          : "=D" (pv),
                            "=c" (uDummy)
                          : "0" (pv),
                            "c" (0x1000 >> 2),
                            "a" (0)
                          : "memory");
#   endif
#  else
    __asm
    {
#   ifdef __AMD64__
        xor     rax, rax
        mov     ecx, 0200h
        mov     rdi, [pv]
        rep     stosq
#   else
        xor     eax, eax
        mov     ecx, 0400h
        mov     edi, [pv]
        rep     stosd
#   endif
    }
#  endif
}
# endif


/**
 * Zeros a memory block with a 32-bit aligned size.
 *
 * @param   pv  Pointer to the memory block.
 * @param   cb  Number of bytes in the block. This MUST be aligned on 32-bit!
 */
#if RT_INLINE_ASM_EXTERNAL && !RT_INLINE_ASM_USES_INTRIN
DECLASM(void) ASMMemZero32(volatile void *pv, size_t cb);
#else
DECLINLINE(void) ASMMemZero32(volatile void *pv, size_t cb)
{
# if RT_INLINE_ASM_USES_INTRIN
    __stosd((unsigned long *)pv, 0, cb >> 2);

# elif RT_INLINE_ASM_GNU_STYLE
    __asm__ __volatile__ ("rep stosl"
                          : "=D" (pv),
                            "=c" (cb)
                          : "0" (pv),
                            "1" (cb >> 2),
                            "a" (0)
                          : "memory");
# else
    __asm
    {
        xor     eax, eax
#  ifdef __AMD64__
        mov     rcx, [cb]
        shr     rcx, 2
        mov     rdi, [pv]
#  else
        mov     ecx, [cb]
        shr     ecx, 2
        mov     edi, [pv]
#  endif
        rep stosd
    }
# endif
}
#endif


/**
 * Fills a memory block with a 32-bit aligned size.
 *
 * @param   pv  Pointer to the memory block.
 * @param   cb  Number of bytes in the block. This MUST be aligned on 32-bit!
 * @param   u32 The value to fill with.
 */
#if RT_INLINE_ASM_EXTERNAL && !RT_INLINE_ASM_USES_INTRIN
DECLASM(void) ASMMemFill32(volatile void *pv, size_t cb, uint32_t u32);
#else
DECLINLINE(void) ASMMemFill32(volatile void *pv, size_t cb, uint32_t u32)
{
# if RT_INLINE_ASM_USES_INTRIN
    __stosd((unsigned long *)pv, 0, cb >> 2);

# elif RT_INLINE_ASM_GNU_STYLE
    __asm__ __volatile__ ("rep stosl"
                          : "=D" (pv),
                            "=c" (cb)
                          : "0" (pv),
                            "1" (cb >> 2),
                            "a" (u32)
                          : "memory");
# else
    __asm
    {
#  ifdef __AMD64__
        mov     rcx, [cb]
        shr     rcx, 2
        mov     rdi, [pv]
#  else
        mov     ecx, [cb]
        shr     ecx, 2
        mov     edi, [pv]
#  endif
        mov     eax, [u32]
        rep stosd
    }
# endif
}
#endif



/**
 * Multiplies two unsigned 32-bit values returning an unsigned 64-bit result.
 *
 * @returns u32F1 * u32F2.
 */
#if RT_INLINE_ASM_EXTERNAL && !defined(__AMD64__)
DECLASM(uint64_t) ASMMult2xU32RetU64(uint32_t u32F1, uint32_t u32F2);
#else
DECLINLINE(uint64_t) ASMMult2xU32RetU64(uint32_t u32F1, uint32_t u32F2)
{
# ifdef __AMD64__
    return (uint64_t)u32F1 * u32F2;
# else /* !__AMD64__ */
    uint64_t u64;
#  if RT_INLINE_ASM_GNU_STYLE
    __asm__ __volatile__("mull %%edx"
                         : "=A" (u64)
                         : "a" (u32F2), "d" (u32F1));
#  else
    __asm
    {
        mov     edx, [u32F1]
        mov     eax, [u32F2]
        mul     edx
        mov     dword ptr [u64], eax
        mov     dword ptr [u64 + 4], edx
    }
#  endif
    return u64;
# endif /* !__AMD64__ */
}
#endif


/**
 * Multiplies two signed 32-bit values returning a signed 64-bit result.
 *
 * @returns u32F1 * u32F2.
 */
#if RT_INLINE_ASM_EXTERNAL && !defined(__AMD64__)
DECLASM(int64_t) ASMMult2xS32RetS64(int32_t i32F1, int32_t i32F2);
#else
DECLINLINE(int64_t) ASMMult2xS32RetS64(int32_t i32F1, int32_t i32F2)
{
# ifdef __AMD64__
    return (int64_t)i32F1 * i32F2;
# else /* !__AMD64__ */
    int64_t i64;
#  if RT_INLINE_ASM_GNU_STYLE
    __asm__ __volatile__("imull %%edx"
                         : "=A" (i64)
                         : "a" (i32F2), "d" (i32F1));
#  else
    __asm
    {
        mov     edx, [i32F1]
        mov     eax, [i32F2]
        imul    edx
        mov     dword ptr [i64], eax
        mov     dword ptr [i64 + 4], edx
    }
#  endif
    return i64;
# endif /* !__AMD64__ */
}
#endif


/**
 * Devides a 64-bit unsigned by a 32-bit unsigned returning an unsigned 32-bit result.
 *
 * @returns u64 / u32.
 */
#if RT_INLINE_ASM_EXTERNAL && !defined(__AMD64__)
DECLASM(uint32_t) ASMDivU64ByU32RetU32(uint64_t u64, uint32_t u32);
#else
DECLINLINE(uint32_t) ASMDivU64ByU32RetU32(uint64_t u64, uint32_t u32)
{
# ifdef __AMD64__
    return (uint32_t)(u64 / u32);
# else /* !__AMD64__ */
#  if RT_INLINE_ASM_GNU_STYLE
    RTUINTREG uDummy;
    __asm__ __volatile__("divl %3"
                         : "=a" (u32), "=d"(uDummy)
                         : "A" (u64), "r" (u32));
#  else
    __asm
    {
        mov     eax, dword ptr [u64]
        mov     edx, dword ptr [u64 + 4]
        mov     ecx, [u32]
        div     ecx
        mov     [u32], eax
    }
#  endif
    return u32;
# endif /* !__AMD64__ */
}
#endif


/**
 * Devides a 64-bit signed by a 32-bit signed returning a signed 32-bit result.
 *
 * @returns u64 / u32.
 */
#if RT_INLINE_ASM_EXTERNAL && !defined(__AMD64__)
DECLASM(int32_t) ASMDivS64ByS32RetS32(int64_t i64, int32_t i32);
#else
DECLINLINE(int32_t) ASMDivS64ByS32RetS32(int64_t i64, int32_t i32)
{
# ifdef __AMD64__
    return (int32_t)(i64 / i32);
# else /* !__AMD64__ */
#  if RT_INLINE_ASM_GNU_STYLE
    RTUINTREG iDummy;
    __asm__ __volatile__("idivl %3"
                         : "=a" (i32), "=d"(iDummy)
                         : "A" (i64), "r" (i32));
#  else
    __asm
    {
        mov     eax, dword ptr [i64]
        mov     edx, dword ptr [i64 + 4]
        mov     ecx, [i32]
        idiv    ecx
        mov     [i32], eax
    }
#  endif
    return i32;
# endif /* !__AMD64__ */
}
#endif


/**
 * Multiple a 64-bit by a 32-bit integer and divide the result by a 32-bit integer
 * using a 96 bit intermediate result.
 * 
 * @returns (u64A * u32B) / u32C.
 * @param   u64A    The 64-bit value.
 * @param   u32B    The 32-bit value to multiple by A.
 * @param   u32C    The 32-bit value to divide A*B by.
 */
#if RT_INLINE_ASM_EXTERNAL
DECLASM(uint64_t) ASMMultU64ByU32DivByU32(uint64_t u64A, uint32_t u32B, uint32_t u32C);
#else
DECLINLINE(uint64_t) ASMMultU64ByU32DivByU32(uint64_t u64A, uint32_t u32B, uint32_t u32C)
{
# if RT_INLINE_ASM_GNU_STYLE && defined(__AMD64__)
    uint64_t u64Result, u64Spill;
    __asm__ __volatile__("mulq %2\n\t"
                         "divq %3\n\t"
                         : "=a" (u64Result), 
                           "=d" (u64Spill)
                         : "r" ((uint64_t)u32B),
                           "r" ((uint64_t)u32C),
                           "0" (u64A), 
                           "1" (0));
    return u64Result;
# else
    RTUINT64U   u;
    uint64_t    u64Low = (uint64_t)(u64A & 0xffffffff) * u32B;
    uint64_t    u64Hi  = (uint64_t)(u64A >> 32)        * u32B;
    u64Hi  += (u64Low >> 32);
    u.s.Hi = (uint32_t)(u64Hi / u32C);
    u.s.Lo = (uint32_t)((((u64Hi % u32C) << 32) + (u64Low & 0xffffffff)) / u32C);
    return u.u;
# endif 
}
#endif 


/**
 * Probes a byte pointer for read access.
 *
 * While the function will not fault if the byte is not read accessible,
 * the idea is to do this in a safe place like before acquiring locks
 * and such like.
 *
 * Also, this functions guarantees that an eager compiler is not going
 * to optimize the probing away.
 *
 * @param   pvByte      Pointer to the byte.
 */
#if RT_INLINE_ASM_EXTERNAL
DECLASM(uint8_t) ASMProbeReadByte(const void *pvByte);
#else
DECLINLINE(uint8_t) ASMProbeReadByte(const void *pvByte)
{
    /** @todo verify that the compiler actually doesn't optimize this away. (intel & gcc) */
    uint8_t u8;
# if RT_INLINE_ASM_GNU_STYLE
    __asm__ __volatile__("movb (%1), %0\n\t"
                         : "=r" (u8)
                         : "r" (pvByte));
# else
    __asm
    {
#  ifdef __AMD64__
        mov     rax, [pvByte]
        mov     al, [rax]
#  else
        mov     eax, [pvByte]
        mov     al, [eax]
#  endif
        mov     [u8], al
    }
# endif
    return u8;
}
#endif

/**
 * Probes a buffer for read access page by page.
 *
 * While the function will fault if the buffer is not fully read
 * accessible, the idea is to do this in a safe place like before
 * acquiring locks and such like.
 *
 * Also, this functions guarantees that an eager compiler is not going
 * to optimize the probing away.
 *
 * @param   pvBuf       Pointer to the buffer.
 * @param   cbBuf       The size of the buffer in bytes. Must be >= 1.
 */
DECLINLINE(void) ASMProbeReadBuffer(const void *pvBuf, size_t cbBuf)
{
    /** @todo verify that the compiler actually doesn't optimize this away. (intel & gcc) */
    /* the first byte */
    const uint8_t *pu8 = (const uint8_t *)pvBuf;
    ASMProbeReadByte(pu8);

    /* the pages in between pages. */
    while (cbBuf > /*PAGE_SIZE*/0x1000)
    {
        ASMProbeReadByte(pu8);
        cbBuf -= /*PAGE_SIZE*/0x1000;
        pu8   += /*PAGE_SIZE*/0x1000;
    }

    /* the last byte */
    ASMProbeReadByte(pu8 + cbBuf - 1);
}


/** @def ASMBreakpoint
 * Debugger Breakpoint.
 * @remark  In the gnu world we add a nop instruction after the int3 to
 *          force gdb to remain at the int3 source line.
 * @remark  The L4 kernel will try make sense of the breakpoint, thus the jmp.
 * @internal
 */
#if RT_INLINE_ASM_GNU_STYLE
# ifndef __L4ENV__
#  define ASMBreakpoint()       do { __asm__ __volatile__ ("int3\n\tnop"); } while (0)
# else
#  define ASMBreakpoint()       do { __asm__ __volatile__ ("int3; jmp 1f; 1:"); } while (0)
# endif
#else
# define ASMBreakpoint()        __debugbreak()
#endif



/** @defgroup grp_inline_bits   Bit Operations
 * @{
 */


/**
 * Sets a bit in a bitmap.
 *
 * @param   pvBitmap    Pointer to the bitmap.
 * @param   iBit        The bit to set.
 */
#if RT_INLINE_ASM_EXTERNAL && !RT_INLINE_ASM_USES_INTRIN
DECLASM(void) ASMBitSet(volatile void *pvBitmap, int32_t iBit);
#else
DECLINLINE(void) ASMBitSet(volatile void *pvBitmap, int32_t iBit)
{
# if RT_INLINE_ASM_USES_INTRIN
    _bittestandset((long *)pvBitmap, iBit);

# elif RT_INLINE_ASM_GNU_STYLE
    __asm__ __volatile__ ("btsl %1, %0"
                          : "=m" (*(volatile long *)pvBitmap)
                          : "Ir" (iBit)
                          : "memory");
# else
    __asm
    {
#  ifdef __AMD64__
        mov     rax, [pvBitmap]
        mov     edx, [iBit]
        bts     [rax], edx
#  else
        mov     eax, [pvBitmap]
        mov     edx, [iBit]
        bts     [eax], edx
#  endif
    }
# endif
}
#endif


/**
 * Atomically sets a bit in a bitmap.
 *
 * @param   pvBitmap    Pointer to the bitmap.
 * @param   iBit        The bit to set.
 */
#if RT_INLINE_ASM_EXTERNAL && !RT_INLINE_ASM_USES_INTRIN
DECLASM(void) ASMAtomicBitSet(volatile void *pvBitmap, int32_t iBit);
#else
DECLINLINE(void) ASMAtomicBitSet(volatile void *pvBitmap, int32_t iBit)
{
# if RT_INLINE_ASM_USES_INTRIN
    _interlockedbittestandset((long *)pvBitmap, iBit);
# elif RT_INLINE_ASM_GNU_STYLE
    __asm__ __volatile__ ("lock; btsl %1, %0"
                          : "=m" (*(volatile long *)pvBitmap)
                          : "Ir" (iBit)
                          : "memory");
# else
    __asm
    {
#  ifdef __AMD64__
        mov     rax, [pvBitmap]
        mov     edx, [iBit]
        lock bts [rax], edx
#  else
        mov     eax, [pvBitmap]
        mov     edx, [iBit]
        lock bts [eax], edx
#  endif
    }
# endif
}
#endif


/**
 * Clears a bit in a bitmap.
 *
 * @param   pvBitmap    Pointer to the bitmap.
 * @param   iBit        The bit to clear.
 */
#if RT_INLINE_ASM_EXTERNAL && !RT_INLINE_ASM_USES_INTRIN
DECLASM(void) ASMBitClear(volatile void *pvBitmap, int32_t iBit);
#else
DECLINLINE(void) ASMBitClear(volatile void *pvBitmap, int32_t iBit)
{
# if RT_INLINE_ASM_USES_INTRIN
    _bittestandreset((long *)pvBitmap, iBit);

# elif RT_INLINE_ASM_GNU_STYLE
    __asm__ __volatile__ ("btrl %1, %0"
                          : "=m" (*(volatile long *)pvBitmap)
                          : "Ir" (iBit)
                          : "memory");
# else
    __asm
    {
#  ifdef __AMD64__
        mov     rax, [pvBitmap]
        mov     edx, [iBit]
        btr     [rax], edx
#  else
        mov     eax, [pvBitmap]
        mov     edx, [iBit]
        btr     [eax], edx
#  endif
    }
# endif
}
#endif


/**
 * Atomically clears a bit in a bitmap.
 *
 * @param   pvBitmap    Pointer to the bitmap.
 * @param   iBit        The bit to toggle set.
 * @remark  No memory barrier, take care on smp.
 */
#if RT_INLINE_ASM_EXTERNAL
DECLASM(void) ASMAtomicBitClear(volatile void *pvBitmap, int32_t iBit);
#else
DECLINLINE(void) ASMAtomicBitClear(volatile void *pvBitmap, int32_t iBit)
{
# if RT_INLINE_ASM_GNU_STYLE
    __asm__ __volatile__ ("lock; btrl %1, %0"
                          : "=m" (*(volatile long *)pvBitmap)
                          : "Ir" (iBit)
                          : "memory");
# else
    __asm
    {
#  ifdef __AMD64__
        mov     rax, [pvBitmap]
        mov     edx, [iBit]
        lock btr [rax], edx
#  else
        mov     eax, [pvBitmap]
        mov     edx, [iBit]
        lock btr [eax], edx
#  endif
    }
# endif
}
#endif


/**
 * Toggles a bit in a bitmap.
 *
 * @param   pvBitmap    Pointer to the bitmap.
 * @param   iBit        The bit to toggle.
 */
#if RT_INLINE_ASM_EXTERNAL && !RT_INLINE_ASM_USES_INTRIN
DECLASM(void) ASMBitToggle(volatile void *pvBitmap, int32_t iBit);
#else
DECLINLINE(void) ASMBitToggle(volatile void *pvBitmap, int32_t iBit)
{
# if RT_INLINE_ASM_USES_INTRIN
    _bittestandcomplement((long *)pvBitmap, iBit);
# elif RT_INLINE_ASM_GNU_STYLE
    __asm__ __volatile__ ("btcl %1, %0"
                          : "=m" (*(volatile long *)pvBitmap)
                          : "Ir" (iBit)
                          : "memory");
# else
    __asm
    {
#  ifdef __AMD64__
        mov     rax, [pvBitmap]
        mov     edx, [iBit]
        btc     [rax], edx
#  else
        mov     eax, [pvBitmap]
        mov     edx, [iBit]
        btc     [eax], edx
#  endif
    }
# endif
}
#endif


/**
 * Atomically toggles a bit in a bitmap.
 *
 * @param   pvBitmap    Pointer to the bitmap.
 * @param   iBit        The bit to test and set.
 */
#if RT_INLINE_ASM_EXTERNAL
DECLASM(void) ASMAtomicBitToggle(volatile void *pvBitmap, int32_t iBit);
#else
DECLINLINE(void) ASMAtomicBitToggle(volatile void *pvBitmap, int32_t iBit)
{
# if RT_INLINE_ASM_GNU_STYLE
    __asm__ __volatile__ ("lock; btcl %1, %0"
                          : "=m" (*(volatile long *)pvBitmap)
                          : "Ir" (iBit)
                          : "memory");
# else
    __asm
    {
#  ifdef __AMD64__
        mov     rax, [pvBitmap]
        mov     edx, [iBit]
        lock btc [rax], edx
#  else
        mov     eax, [pvBitmap]
        mov     edx, [iBit]
        lock btc [eax], edx
#  endif
    }
# endif
}
#endif


/**
 * Tests and sets a bit in a bitmap.
 *
 * @returns true if the bit was set.
 * @returns false if the bit was clear.
 * @param   pvBitmap    Pointer to the bitmap.
 * @param   iBit        The bit to test and set.
 */
#if RT_INLINE_ASM_EXTERNAL && !RT_INLINE_ASM_USES_INTRIN
DECLASM(bool) ASMBitTestAndSet(volatile void *pvBitmap, int32_t iBit);
#else
DECLINLINE(bool) ASMBitTestAndSet(volatile void *pvBitmap, int32_t iBit)
{
    union { bool f; uint32_t u32; uint8_t u8; } rc;
# if RT_INLINE_ASM_USES_INTRIN
    rc.u8 = _bittestandset((long *)pvBitmap, iBit);

# elif RT_INLINE_ASM_GNU_STYLE
    __asm__ __volatile__ ("btsl %2, %1\n\t"
                          "setc %b0\n\t"
                          "andl $1, %0\n\t"
                          : "=q" (rc.u32),
                            "=m" (*(volatile long *)pvBitmap)
                          : "Ir" (iBit)
                          : "memory");
# else
    __asm
    {
        mov     edx, [iBit]
#  ifdef __AMD64__
        mov     rax, [pvBitmap]
        bts     [rax], edx
#  else
        mov     eax, [pvBitmap]
        bts     [eax], edx
#  endif
        setc    al
        and     eax, 1
        mov     [rc.u32], eax
    }
# endif
    return rc.f;
}
#endif


/**
 * Atomically tests and sets a bit in a bitmap.
 *
 * @returns true if the bit was set.
 * @returns false if the bit was clear.
 * @param   pvBitmap    Pointer to the bitmap.
 * @param   iBit        The bit to set.
 */
#if RT_INLINE_ASM_EXTERNAL && !RT_INLINE_ASM_USES_INTRIN
DECLASM(bool) ASMAtomicBitTestAndSet(volatile void *pvBitmap, int32_t iBit);
#else
DECLINLINE(bool) ASMAtomicBitTestAndSet(volatile void *pvBitmap, int32_t iBit)
{
    union { bool f; uint32_t u32; uint8_t u8; } rc;
# if RT_INLINE_ASM_USES_INTRIN
    rc.u8 = _interlockedbittestandset((long *)pvBitmap, iBit);
# elif RT_INLINE_ASM_GNU_STYLE
    __asm__ __volatile__ ("lock; btsl %2, %1\n\t"
                          "setc %b0\n\t"
                          "andl $1, %0\n\t"
                          : "=q" (rc.u32),
                            "=m" (*(volatile long *)pvBitmap)
                          : "Ir" (iBit)
                          : "memory");
# else
    __asm
    {
        mov     edx, [iBit]
#  ifdef __AMD64__
        mov     rax, [pvBitmap]
        lock bts [rax], edx
#  else
        mov     eax, [pvBitmap]
        lock bts [eax], edx
#  endif
        setc    al
        and     eax, 1
        mov     [rc.u32], eax
    }
# endif
    return rc.f;
}
#endif


/**
 * Tests and clears a bit in a bitmap.
 *
 * @returns true if the bit was set.
 * @returns false if the bit was clear.
 * @param   pvBitmap    Pointer to the bitmap.
 * @param   iBit        The bit to test and clear.
 */
#if RT_INLINE_ASM_EXTERNAL && !RT_INLINE_ASM_USES_INTRIN
DECLASM(bool) ASMBitTestAndClear(volatile void *pvBitmap, int32_t iBit);
#else
DECLINLINE(bool) ASMBitTestAndClear(volatile void *pvBitmap, int32_t iBit)
{
    union { bool f; uint32_t u32; uint8_t u8; } rc;
# if RT_INLINE_ASM_USES_INTRIN
    rc.u8 = _bittestandreset((long *)pvBitmap, iBit);

# elif RT_INLINE_ASM_GNU_STYLE
    __asm__ __volatile__ ("btrl %2, %1\n\t"
                          "setc %b0\n\t"
                          "andl $1, %0\n\t"
                          : "=q" (rc.u32),
                            "=m" (*(volatile long *)pvBitmap)
                          : "Ir" (iBit)
                          : "memory");
# else
    __asm
    {
        mov     edx, [iBit]
#  ifdef __AMD64__
        mov     rax, [pvBitmap]
        btr     [rax], edx
#  else
        mov     eax, [pvBitmap]
        btr     [eax], edx
#  endif
        setc    al
        and     eax, 1
        mov     [rc.u32], eax
    }
# endif
    return rc.f;
}
#endif


/**
 * Atomically tests and clears a bit in a bitmap.
 *
 * @returns true if the bit was set.
 * @returns false if the bit was clear.
 * @param   pvBitmap    Pointer to the bitmap.
 * @param   iBit        The bit to test and clear.
 * @remark  No memory barrier, take care on smp.
 */
#if RT_INLINE_ASM_EXTERNAL && !RT_INLINE_ASM_USES_INTRIN
DECLASM(bool) ASMAtomicBitTestAndClear(volatile void *pvBitmap, int32_t iBit);
#else
DECLINLINE(bool) ASMAtomicBitTestAndClear(volatile void *pvBitmap, int32_t iBit)
{
    union { bool f; uint32_t u32; uint8_t u8; } rc;
# if RT_INLINE_ASM_USES_INTRIN
    rc.u8 = _interlockedbittestandreset((long *)pvBitmap, iBit);

# elif RT_INLINE_ASM_GNU_STYLE
    __asm__ __volatile__ ("lock; btrl %2, %1\n\t"
                          "setc %b0\n\t"
                          "andl $1, %0\n\t"
                          : "=q" (rc.u32),
                            "=m" (*(volatile long *)pvBitmap)
                          : "Ir" (iBit)
                          : "memory");
# else
    __asm
    {
        mov     edx, [iBit]
#  ifdef __AMD64__
        mov     rax, [pvBitmap]
        lock btr [rax], edx
#  else
        mov     eax, [pvBitmap]
        lock btr [eax], edx
#  endif
        setc    al
        and     eax, 1
        mov     [rc.u32], eax
    }
# endif
    return rc.f;
}
#endif


/**
 * Tests and toggles a bit in a bitmap.
 *
 * @returns true if the bit was set.
 * @returns false if the bit was clear.
 * @param   pvBitmap    Pointer to the bitmap.
 * @param   iBit        The bit to test and toggle.
 */
#if RT_INLINE_ASM_EXTERNAL && !RT_INLINE_ASM_USES_INTRIN
DECLINLINE(bool) ASMBitTestAndToggle(volatile void *pvBitmap, int32_t iBit);
#else
DECLINLINE(bool) ASMBitTestAndToggle(volatile void *pvBitmap, int32_t iBit)
{
    union { bool f; uint32_t u32; uint8_t u8; } rc;
# if RT_INLINE_ASM_USES_INTRIN
    rc.u8 = _bittestandcomplement((long *)pvBitmap, iBit);

# elif RT_INLINE_ASM_GNU_STYLE
    __asm__ __volatile__ ("btcl %2, %1\n\t"
                          "setc %b0\n\t"
                          "andl $1, %0\n\t"
                          : "=q" (rc.u32),
                            "=m" (*(volatile long *)pvBitmap)
                          : "Ir" (iBit)
                          : "memory");
# else
    __asm
    {
        mov   edx, [iBit]
#  ifdef __AMD64__
        mov   rax, [pvBitmap]
        btc   [rax], edx
#  else
        mov   eax, [pvBitmap]
        btc   [eax], edx
#  endif
        setc  al
        and   eax, 1
        mov   [rc.u32], eax
    }
# endif
    return rc.f;
}
#endif


/**
 * Atomically tests and toggles a bit in a bitmap.
 *
 * @returns true if the bit was set.
 * @returns false if the bit was clear.
 * @param   pvBitmap    Pointer to the bitmap.
 * @param   iBit        The bit to test and toggle.
 */
#if RT_INLINE_ASM_EXTERNAL
DECLASM(bool) ASMAtomicBitTestAndToggle(volatile void *pvBitmap, int32_t iBit);
#else
DECLINLINE(bool) ASMAtomicBitTestAndToggle(volatile void *pvBitmap, int32_t iBit)
{
    union { bool f; uint32_t u32; uint8_t u8; } rc;
# if RT_INLINE_ASM_GNU_STYLE
    __asm__ __volatile__ ("lock; btcl %2, %1\n\t"
                          "setc %b0\n\t"
                          "andl $1, %0\n\t"
                          : "=q" (rc.u32),
                            "=m" (*(volatile long *)pvBitmap)
                          : "Ir" (iBit)
                          : "memory");
# else
    __asm
    {
        mov     edx, [iBit]
#  ifdef __AMD64__
        mov     rax, [pvBitmap]
        lock btc [rax], edx
#  else
        mov     eax, [pvBitmap]
        lock btc [eax], edx
#  endif
        setc    al
        and     eax, 1
        mov     [rc.u32], eax
    }
# endif
    return rc.f;
}
#endif


/**
 * Tests if a bit in a bitmap is set.
 *
 * @returns true if the bit is set.
 * @returns false if the bit is clear.
 * @param   pvBitmap    Pointer to the bitmap.
 * @param   iBit        The bit to test.
 */
#if RT_INLINE_ASM_EXTERNAL && !RT_INLINE_ASM_USES_INTRIN
DECLASM(bool) ASMBitTest(volatile void *pvBitmap, int32_t iBit);
#else
DECLINLINE(bool) ASMBitTest(volatile void *pvBitmap, int32_t iBit)
{
    union { bool f; uint32_t u32; uint8_t u8; } rc;
# if RT_INLINE_ASM_USES_INTRIN
    rc.u32 = _bittest((long *)pvBitmap, iBit);
# elif RT_INLINE_ASM_GNU_STYLE

    __asm__ __volatile__ ("btl %2, %1\n\t"
                          "setc %b0\n\t"
                          "andl $1, %0\n\t"
                          : "=q" (rc.u32),
                            "=m" (*(volatile long *)pvBitmap)
                          : "Ir" (iBit)
                          : "memory");
# else
    __asm
    {
        mov   edx, [iBit]
#  ifdef __AMD64__
        mov   rax, [pvBitmap]
        bt    [rax], edx
#  else
        mov   eax, [pvBitmap]
        bt    [eax], edx
#  endif
        setc  al
        and   eax, 1
        mov   [rc.u32], eax
    }
# endif
    return rc.f;
}
#endif


/**
 * Clears a bit range within a bitmap.
 *
 * @param   pvBitmap    Pointer to the bitmap.
 * @param   iBitStart   The First bit to clear.
 * @param   iBitEnd     The first bit not to clear.
 */
DECLINLINE(void) ASMBitClearRange(volatile void *pvBitmap, int32_t iBitStart, int32_t iBitEnd)
{
    if (iBitStart < iBitEnd)
    {
        volatile uint32_t *pu32 = (volatile uint32_t *)pvBitmap + (iBitStart >> 5);
        int iStart = iBitStart & ~31;
        int iEnd   = iBitEnd & ~31;
        if (iStart == iEnd)
            *pu32 &= ((1 << (iBitStart & 31)) - 1) | ~((1 << (iBitEnd & 31)) - 1);
        else
        {
            /* bits in first dword. */
            if (iBitStart & 31)
            {
                *pu32 &= (1 << (iBitStart & 31)) - 1;
                pu32++;
                iBitStart = iStart + 32;
            }

            /* whole dword. */
            if (iBitStart != iEnd)
                ASMMemZero32(pu32, (iEnd - iBitStart) >> 3);

            /* bits in last dword. */
            if (iBitEnd & 31)
            {
                pu32 = (volatile uint32_t *)pvBitmap + (iBitEnd >> 5);
                *pu32 &= ~((1 << (iBitEnd & 31)) - 1);
            }
        }
    }
}


/**
 * Finds the first clear bit in a bitmap.
 *
 * @returns Index of the first zero bit.
 * @returns -1 if no clear bit was found.
 * @param   pvBitmap    Pointer to the bitmap.
 * @param   cBits       The number of bits in the bitmap. Multiple of 32.
 */
#if RT_INLINE_ASM_EXTERNAL
DECLASM(int) ASMBitFirstClear(volatile void *pvBitmap, uint32_t cBits);
#else
DECLINLINE(int) ASMBitFirstClear(volatile void *pvBitmap, uint32_t cBits)
{
    if (cBits)
    {
        int32_t iBit;
# if RT_INLINE_ASM_GNU_STYLE
        RTCCUINTREG uEAX, uECX, uEDI;
        cBits = RT_ALIGN_32(cBits, 32);
        __asm__ __volatile__("repe; scasl\n\t"
                             "je    1f\n\t"
#  ifdef __AMD64__
                             "lea   -4(%%rdi), %%rdi\n\t"
                             "xorl  (%%rdi), %%eax\n\t"
                             "subq  %5, %%rdi\n\t"
#  else
                             "lea   -4(%%edi), %%edi\n\t"
                             "xorl  (%%edi), %%eax\n\t"
                             "subl  %5, %%edi\n\t"
#  endif
                             "shll  $3, %%edi\n\t"
                             "bsfl  %%eax, %%edx\n\t"
                             "addl  %%edi, %%edx\n\t"
                             "1:\t\n"
                             : "=d" (iBit),
                               "=&c" (uECX),
                               "=&D" (uEDI),
                               "=&a" (uEAX)
                             : "0" (0xffffffff),
                               "mr" (pvBitmap),
                               "1" (cBits >> 5),
                               "2" (pvBitmap),
                               "3" (0xffffffff));
# else
        cBits = RT_ALIGN_32(cBits, 32);
        __asm
        {
#  ifdef __AMD64__
            mov     rdi, [pvBitmap]
            mov     rbx, rdi
#  else
            mov     edi, [pvBitmap]
            mov     ebx, edi
#  endif
            mov     edx, 0ffffffffh
            mov     eax, edx
            mov     ecx, [cBits]
            shr     ecx, 5
            repe    scasd
            je      done

#  ifdef __AMD64__
            lea     rdi, [rdi - 4]
            xor     eax, [rdi]
            sub     rdi, rbx
#  else
            lea     edi, [edi - 4]
            xor     eax, [edi]
            sub     edi, ebx
#  endif
            shl     edi, 3
            bsf     edx, eax
            add     edx, edi
        done:
            mov     [iBit], edx
        }
# endif
        return iBit;
    }
    return -1;
}
#endif


/**
 * Finds the next clear bit in a bitmap.
 *
 * @returns Index of the first zero bit.
 * @returns -1 if no clear bit was found.
 * @param   pvBitmap    Pointer to the bitmap.
 * @param   cBits       The number of bits in the bitmap. Multiple of 32.
 * @param   iBitPrev    The bit returned from the last search.
 *                      The search will start at iBitPrev + 1.
 */
#if RT_INLINE_ASM_EXTERNAL && !RT_INLINE_ASM_USES_INTRIN
DECLASM(int) ASMBitNextClear(volatile void *pvBitmap, uint32_t cBits, uint32_t iBitPrev);
#else
DECLINLINE(int) ASMBitNextClear(volatile void *pvBitmap, uint32_t cBits, uint32_t iBitPrev)
{
    int iBit = ++iBitPrev & 31;
    pvBitmap = (volatile char *)pvBitmap + ((iBitPrev >> 5) << 2);
    cBits   -= iBitPrev & ~31;
    if (iBit)
    {
        /* inspect the first dword. */
        uint32_t u32 = (~*(volatile uint32_t *)pvBitmap) >> iBit;
# if RT_INLINE_ASM_USES_INTRIN
        unsigned long ulBit = 0;
        if (_BitScanForward(&ulBit, u32))
            return ulBit + iBitPrev;
        iBit = -1;
# else
#  if RT_INLINE_ASM_GNU_STYLE
        __asm__ __volatile__("bsf %1, %0\n\t"
                             "jnz 1f\n\t"
                             "movl $-1, %0\n\t"
                             "1:\n\t"
                             : "=r" (iBit)
                             : "r" (u32));
#  else
        __asm
        {
            mov     edx, [u32]
            bsf     eax, edx
            jnz     done
            mov     eax, 0ffffffffh
        done:
            mov     [iBit], eax
        }
#  endif
        if (iBit >= 0)
            return iBit + iBitPrev;
# endif
        /* Search the rest of the bitmap, if there is anything. */
        if (cBits > 32)
        {
            iBit = ASMBitFirstClear((volatile char *)pvBitmap + sizeof(uint32_t), cBits - 32);
            if (iBit >= 0)
                return iBit + (iBitPrev & ~31) + 32;
        }
    }
    else
    {
        /* Search the rest of the bitmap. */
        iBit = ASMBitFirstClear(pvBitmap, cBits);
        if (iBit >= 0)
            return iBit + (iBitPrev & ~31);
    }
    return iBit;
}
#endif


/**
 * Finds the first set bit in a bitmap.
 *
 * @returns Index of the first set bit.
 * @returns -1 if no clear bit was found.
 * @param   pvBitmap    Pointer to the bitmap.
 * @param   cBits       The number of bits in the bitmap. Multiple of 32.
 */
#if RT_INLINE_ASM_EXTERNAL
DECLASM(int) ASMBitFirstSet(volatile void *pvBitmap, uint32_t cBits);
#else
DECLINLINE(int) ASMBitFirstSet(volatile void *pvBitmap, uint32_t cBits)
{
    if (cBits)
    {
        int32_t iBit;
# if RT_INLINE_ASM_GNU_STYLE
        RTCCUINTREG uEAX, uECX, uEDI;
        cBits = RT_ALIGN_32(cBits, 32);
        __asm__ __volatile__("repe; scasl\n\t"
                             "je    1f\n\t"
#  ifdef __AMD64__
                             "lea   -4(%%rdi), %%rdi\n\t"
                             "movl  (%%rdi), %%eax\n\t"
                             "subq  %5, %%rdi\n\t"
#  else
                             "lea   -4(%%edi), %%edi\n\t"
                             "movl  (%%edi), %%eax\n\t"
                             "subl  %5, %%edi\n\t"
#  endif
                             "shll  $3, %%edi\n\t"
                             "bsfl  %%eax, %%edx\n\t"
                             "addl  %%edi, %%edx\n\t"
                             "1:\t\n"
                             : "=d" (iBit),
                               "=&c" (uECX),
                               "=&D" (uEDI),
                               "=&a" (uEAX)
                             : "0" (0xffffffff),
                               "mr" (pvBitmap),
                               "1" (cBits >> 5),
                               "2" (pvBitmap),
                               "3" (0));
# else
        cBits = RT_ALIGN_32(cBits, 32);
        __asm
        {
#  ifdef __AMD64__
            mov     rdi, [pvBitmap]
            mov     rbx, rdi
#  else
            mov     edi, [pvBitmap]
            mov     ebx, edi
#  endif
            mov     edx, 0ffffffffh
            xor     eax, eax
            mov     ecx, [cBits]
            shr     ecx, 5
            repe    scasd
            je      done
#  ifdef __AMD64__
            lea     rdi, [rdi - 4]
            mov     eax, [rdi]
            sub     rdi, rbx
#  else
            lea     edi, [edi - 4]
            mov     eax, [edi]
            sub     edi, ebx
#  endif
            shl     edi, 3
            bsf     edx, eax
            add     edx, edi
        done:
            mov   [iBit], edx
        }
# endif
        return iBit;
    }
    return -1;
}
#endif


/**
 * Finds the next set bit in a bitmap.
 *
 * @returns Index of the next set bit.
 * @returns -1 if no set bit was found.
 * @param   pvBitmap    Pointer to the bitmap.
 * @param   cBits       The number of bits in the bitmap. Multiple of 32.
 * @param   iBitPrev    The bit returned from the last search.
 *                      The search will start at iBitPrev + 1.
 */
#if RT_INLINE_ASM_EXTERNAL && !RT_INLINE_ASM_USES_INTRIN
DECLASM(int) ASMBitNextSet(volatile void *pvBitmap, uint32_t cBits, uint32_t iBitPrev);
#else
DECLINLINE(int) ASMBitNextSet(volatile void *pvBitmap, uint32_t cBits, uint32_t iBitPrev)
{
    int iBit = ++iBitPrev & 31;
    pvBitmap = (volatile char *)pvBitmap + ((iBitPrev >> 5) << 2);
    cBits   -= iBitPrev & ~31;
    if (iBit)
    {
        /* inspect the first dword. */
        uint32_t u32 = *(volatile uint32_t *)pvBitmap >> iBit;
# if RT_INLINE_ASM_USES_INTRIN
        unsigned long ulBit = 0;
        if (_BitScanForward(&ulBit, u32))
            return ulBit + iBitPrev;
        iBit = -1;
# else
#  if RT_INLINE_ASM_GNU_STYLE
        __asm__ __volatile__("bsf %1, %0\n\t"
                             "jnz 1f\n\t"
                             "movl $-1, %0\n\t"
                             "1:\n\t"
                             : "=r" (iBit)
                             : "r" (u32));
#  else
        __asm
        {
            mov     edx, u32
            bsf     eax, edx
            jnz     done
            mov     eax, 0ffffffffh
        done:
            mov     [iBit], eax
        }
#  endif
        if (iBit >= 0)
            return iBit + iBitPrev;
# endif
        /* Search the rest of the bitmap, if there is anything. */
        if (cBits > 32)
        {
            iBit = ASMBitFirstSet((volatile char *)pvBitmap + sizeof(uint32_t), cBits - 32);
            if (iBit >= 0)
                return iBit + (iBitPrev & ~31) + 32;
        }

    }
    else
    {
        /* Search the rest of the bitmap. */
        iBit = ASMBitFirstSet(pvBitmap, cBits);
        if (iBit >= 0)
            return iBit + (iBitPrev & ~31);
    }
    return iBit;
}
#endif


/**
 * Finds the first bit which is set in the given 32-bit integer.
 * Bits are numbered from 1 (least significant) to 32.
 *
 * @returns index [1..32] of the first set bit.
 * @returns 0 if all bits are cleared.
 * @param   u32     Integer to search for set bits.
 * @remark  Similar to ffs() in BSD.
 */
DECLINLINE(unsigned) ASMBitFirstSetU32(uint32_t u32)
{
# if RT_INLINE_ASM_USES_INTRIN
    unsigned long iBit;
    if (_BitScanForward(&iBit, u32))
        iBit++;
    else
        iBit = 0;
# elif RT_INLINE_ASM_GNU_STYLE
    uint32_t iBit;
    __asm__ __volatile__("bsf  %1, %0\n\t"
                         "jnz  1f\n\t"
                         "xorl %0, %0\n\t"
                         "jmp  2f\n"
                         "1:\n\t"
                         "incl %0\n"
                         "2:\n\t"
                         : "=r" (iBit)
                         : "rm" (u32));
# else
    uint32_t iBit;
    _asm
    {
        bsf     eax, [u32]
        jnz     found
        xor     eax, eax
        jmp     done
    found:
        inc     eax
    done:
        mov     [iBit], eax
    }
# endif
    return iBit;
}


/**
 * Finds the first bit which is set in the given 32-bit integer.
 * Bits are numbered from 1 (least significant) to 32.
 *
 * @returns index [1..32] of the first set bit.
 * @returns 0 if all bits are cleared.
 * @param   i32     Integer to search for set bits.
 * @remark  Similar to ffs() in BSD.
 */
DECLINLINE(unsigned) ASMBitFirstSetS32(int32_t i32)
{
    return ASMBitFirstSetU32((uint32_t)i32);
}


/**
 * Finds the last bit which is set in the given 32-bit integer.
 * Bits are numbered from 1 (least significant) to 32.
 *
 * @returns index [1..32] of the last set bit.
 * @returns 0 if all bits are cleared.
 * @param   u32     Integer to search for set bits.
 * @remark  Similar to fls() in BSD.
 */
DECLINLINE(unsigned) ASMBitLastSetU32(uint32_t u32)
{
# if RT_INLINE_ASM_USES_INTRIN
    unsigned long iBit;
    if (_BitScanReverse(&iBit, u32))
        iBit++;
    else
        iBit = 0;
# elif RT_INLINE_ASM_GNU_STYLE
    uint32_t iBit;
    __asm__ __volatile__("bsrl %1, %0\n\t"
                         "jnz   1f\n\t"
                         "xorl %0, %0\n\t"
                         "jmp  2f\n"
                         "1:\n\t"
                         "incl %0\n"
                         "2:\n\t"
                         : "=r" (iBit)
                         : "rm" (u32));
# else
    uint32_t iBit;
    _asm
    {
        bsr     eax, [u32]
        jnz     found
        xor     eax, eax
        jmp     done
    found:
        inc     eax
    done:
        mov     [iBit], eax
    }
# endif
    return iBit;
}


/**
 * Finds the last bit which is set in the given 32-bit integer.
 * Bits are numbered from 1 (least significant) to 32.
 *
 * @returns index [1..32] of the last set bit.
 * @returns 0 if all bits are cleared.
 * @param   i32     Integer to search for set bits.
 * @remark  Similar to fls() in BSD.
 */
DECLINLINE(unsigned) ASMBitLastSetS32(int32_t i32)
{
    return ASMBitLastSetS32((uint32_t)i32);
}


/**
 * Reverse the byte order of the given 32-bit integer.
 * @param  u32      Integer
 */
DECLINLINE(uint32_t) ASMByteSwapU32(uint32_t u32)
{
#if RT_INLINE_ASM_USES_INTRIN
    u32 = _byteswap_ulong(u32);
#elif RT_INLINE_ASM_GNU_STYLE
    __asm__ ("bswapl %0" : "=r" (u32) : "0" (u32));
#else
    _asm
    {
        mov     eax, [u32]
        bswap   eax
        mov     [u32], eax
    }
#endif
    return u32;
}

/** @} */


/** @} */
#endif

