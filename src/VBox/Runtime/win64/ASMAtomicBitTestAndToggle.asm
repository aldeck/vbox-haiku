;; @file
; innotek Portable Runtime - ASMAtomicBitTestAndToggle().
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


;*******************************************************************************
;* Header Files                                                                *
;*******************************************************************************
%include "iprt/asmdefs.mac"

BEGINCODE

;;
; Atomically tests and toggles a bit in a bitmap.
;
; @returns al(rax)      true if the bit was set.
; @returns al(rax)      false if the bit was clear.
; @param   rcx          pvBitmap    Pointer to the bitmap.
; @param   edx          iBit        The bit to test and toggle.
BEGINPROC_EXPORTED ASMAtomicBitTestAndToggle
        lock btc [rcx], edx
        sbb     eax, eax
        ret
ENDPROC ASMAtomicBitTestAndToggle

