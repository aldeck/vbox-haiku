;; @file
; InnoTek Portable Runtime - ASMBitFirstClear().
;

;
; Copyright (C) 2006 InnoTek Systemberatung GmbH
;
; This file is part of VirtualBox Open Source Edition (OSE), as
; available from http://www.virtualbox.org. This file is free software;
; you can redistribute it and/or modify it under the terms of the GNU
; General Public License as published by the Free Software Foundation,
; in version 2 as it comes in the "COPYING" file of the VirtualBox OSE
; distribution. VirtualBox OSE is distributed in the hope that it will
; be useful, but WITHOUT ANY WARRANTY of any kind.
;
; If you received this file as part of a commercial VirtualBox
; distribution, then only the terms of your commercial VirtualBox
; license agreement apply instead of the previous paragraph.
;


;*******************************************************************************
;* Header Files                                                                *
;*******************************************************************************
%include "iprt/asmdefs.mac"

BEGINCODE

;;
; Finds the first clear bit in a bitmap.
;
; @returns eax  Index of the first zero bit.
; @returns eax  -1 if no clear bit was found.
; @param   rcx  pvBitmap    Pointer to the bitmap.
; @param   edx  cBits       The number of bits in the bitmap. Multiple of 32.
;
BEGINPROC_EXPORTED ASMBitFirstClear

        ;if (cBits)
        or      edx, edx
        jz      short @failed
        ;{
        push    rdi

        ;    asm {...}
        mov     rdi, rcx                ; rdi = start of scasd
        mov     ecx, edx
        add     ecx, 31                 ; 32 bit aligned
        shr     ecx, 5                  ; number of dwords to scan.
        mov     rdx, rdi                ; rdx = saved pvBitmap
        mov     eax, 0ffffffffh
        repe    scasd                   ; Scan for the first dword with any clear bit.
        je      @failed_restore

        ; find the bit in question
        lea     rdi, [rdi - 4]          ; one step back.
        xor     eax, [rdi]              ; eax = NOT [rdi]
        sub     rdi, rdx
        shl     edi, 3                  ; calc bit offset.

        mov     ecx, 0ffffffffh
        bsf     ecx, eax
        add     ecx, edi
        mov     eax, ecx

        ; return success
        pop     rdi
        ret

        ; failure
        ;}
        ;return -1;
@failed_restore:
        pop     rdi
        ret
@failed:
        mov     eax, 0ffffffffh
        ret
ENDPROC ASMBitFirstClear

