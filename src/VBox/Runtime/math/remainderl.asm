; $Id$
;; @file
; InnoTek Portable Runtime - No-CRT remainderl - AMD64 & X86.
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

%ifdef __AMD64__
 %define _SP rsp
 %define _BP rbp
%else
 %define _SP esp
 %define _BP ebp
%endif

;;
; See SUS.
; @returns st(0)
; @param    lrd1    [rbp + 10h]
; @param    lrd2    [rbp + 20h]
BEGINPROC RT_NOCRT(remainderl)
    push    _BP
    mov     _BP, _SP

%ifdef __AMD64__
    fld     tword [rbp + 10h + RTLRD_CB]
    fld     tword [rbp + 10h]
%else
    fld     tword [ebp + 8h + RTLRD_CB]
    fld     tword [ebp + 8h]
%endif

    fprem1
    fstsw   ax
    test    ah, 04h
    jnz     .done
    fstp    st1

.done:
    leave
    ret
ENDPROC   RT_NOCRT(remainderl)

