; $Id$
;; @file
; InnoTek Portable Runtime - No-CRT setjmp & longjmp - AMD64 & X86.
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


BEGINCODE


BEGINPROC RT_NOCRT(setjmp)
%ifdef __AMD64__
        mov     rax, [rsp]
        mov     [rdi + 00h], rax        ; rip
        lea     rcx, [rsp + 8]
        mov     [rdi + 08h], rcx        ; rsp
        mov     [rdi + 10h], rbp
        mov     [rdi + 18h], r15
        mov     [rdi + 20h], r14
        mov     [rdi + 28h], r13
        mov     [rdi + 30h], r12
        mov     [rdi + 38h], rbx
%else
        mov     edx, [esp + 4h]
        mov     eax, [esp]
        mov     [edx + 00h], eax        ; eip
        lea     ecx, [esp + 4h]
        mov     [edx + 04h], ecx        ; esp
        mov     [edx + 08h], ebp
        mov     [edx + 0ch], ebx
        mov     [edx + 10h], edi
        mov     [edx + 14h], esi
%endif
        xor     eax, eax
        ret
ENDPROC RT_NOCRT(setjmp)


BEGINPROC RT_NOCRT(longjmp)
%ifdef __AMD64__
        mov     rbx, [rdi + 38h]
        mov     r12, [rdi + 30h]
        mov     r13, [rdi + 28h]
        mov     r14, [rdi + 20h]
        mov     r15, [rdi + 18h]
        mov     rbp, [rdi + 10h]
        mov     eax, esi
        test    eax, eax
        jnz     .fine
        inc     al
.fine:
        mov     rsp, [rdi + 08h]
        jmp     qword [rdi + 00h]
%else
        mov     edx, [esp + 4h]
        mov     eax, [esp + 8h]
        mov     esi, [edx + 14h]
        mov     edi, [edx + 10h]
        mov     ebx, [edx + 0ch]
        mov     ebp, [edx + 08h]
        test    eax, eax
        jnz     .fine
        inc     al
.fine:
        mov     esp, [edx + 04h]
        jmp     dword [edx + 00h]
%endif
ENDPROC RT_NOCRT(longjmp)

