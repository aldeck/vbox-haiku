; $Id$
;; @file
; innotek Portable Runtime - No-CRT llrintl - AMD64 & X86.
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

%ifdef RT_ARCH_AMD64
 %define _SP rsp
 %define _BP rbp
 %define _S  8
%else
 %define _SP esp
 %define _BP ebp
 %define _S  4
%endif

;;
; Round rd to the nearest integer value, rounding according to the current rounding direction.
; @returns 32-bit: edx:eax  64-bit: rax
; @param    lrd     [rbp + _S*2]
BEGINPROC RT_NOCRT(llrintl)
    push    _BP
    mov     _BP, _SP
    sub     _SP, 10h

    fld     tword [_BP + _S*2]
    fistp   qword [_SP]
    fwait
%ifdef RT_ARCH_AMD64
    mov     rax, [_SP]
%else
    mov     eax, [_SP]
    mov     edx, [_SP + 4]
%endif

    leave
    ret
ENDPROC   RT_NOCRT(llrintl)

