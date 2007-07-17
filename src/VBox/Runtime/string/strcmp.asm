; $Id$
;; @file
; innotek Portable Runtime - No-CRT strcmp - AMD64 & X86.
;

;
; Copyright (C) 2006-2007 innotek GmbH
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

;;
; @param    psz1   gcc: rdi  msc: rcx  x86:[esp+4]
; @param    psz2   gcc: rsi  msc: rdx  x86:[esp+8]
BEGINPROC RT_NOCRT(strcmp)
        ; input
%ifdef RT_ARCH_AMD64
 %ifdef ASM_CALL64_MSC
  %define psz1 rcx
  %define psz2 rdx
 %else
  %define psz1 rdi
  %define psz2 rsi
 %endif
%else
        mov     ecx, [esp + 4]
        mov     edx, [esp + 8]
  %define psz1 ecx
  %define psz2 edx
%endif

        ;
        ; The loop.
        ;
.next:
        mov     al, [psz1]
        mov     ah, [psz2]
        cmp     al, ah
        jne     .not_equal
        test    al, al
        jz      .equal

        mov     al, [psz1 + 1]
        mov     ah, [psz2 + 1]
        cmp     al, ah
        jne     .not_equal
        test    al, al
        jz      .equal
        inc     psz1
        inc     psz2

        mov     al, [psz1 + 2]
        mov     ah, [psz2 + 2]
        cmp     al, ah
        jne     .not_equal
        test    al, al
        jz      .equal
        inc     psz1
        inc     psz2

        mov     al, [psz1 + 3]
        mov     ah, [psz2 + 3]
        cmp     al, ah
        jne     .not_equal
        test    al, al
        jz      .equal

        add     psz1, 4
        add     psz2, 4
        jmp     .next

.equal:
        xor     eax, eax
        ret

.not_equal:
        movzx   ecx, ah
        and     eax, 0ffh
        sub     eax, ecx
        ret
ENDPROC RT_NOCRT(strcmp)

