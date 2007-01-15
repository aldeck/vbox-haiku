; $Id$
;; @file
; InnoTek Portable Runtime - No-CRT memset - AMD64 & X86.
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

%include "iprt/asmdefs.mac"

;;
; @param    pvDst   gcc: rdi  msc: ecx  x86:[esp+4]
; @param    ch      gcc: esi  msc: edx  x86:[esp+8]
; @param    cb      gcc: rdx  msc: r8   x86:[esp+0ch]
BEGINPROC RT_NOCRT(memset)
        cld
%ifdef __AMD64__
 %ifdef ASM_CALL64_MSC
        int3
  %error "Port me"
 %else
        movzx   eax, sil
        cmp     rdx, 32
        jb      .dobytes

        ; eax = (al << 24) | (al << 16) | (al << 8) | al;
        ; rdx = (eax << 32) | eax
        movzx   esi, sil
        mov     rax, qword 0101010101010101h
        imul    rax, rsi

        ; todo: alignment.

        mov     rcx, rdx
        shr     rcx, 3
        rep stosq

        and     rdx, 7
.dobytes:
        mov     rcx, rdx
        rep stosb
 %endif

%else
        push    edi

        mov     ecx, [esp + 0ch + 4]
        movzx   eax, byte [esp + 08h + 4]
        mov     edi, [esp + 04h + 4]
        cmp     ecx, 12
        jb      .dobytes

        ; eax = (al << 24) | (al << 16) | (al << 8) | al;
        mov     ah, al
        mov     edx, eax
        shr     edx, 16
        or      eax, edx

        mov     edx, ecx
        shr     ecx, 2
        rep stosd

        and     edx, 3
        mov     ecx, edx
.dobytes:
        rep stosb

        pop     edi
%endif
        ret
ENDPROC RT_NOCRT(memset)

