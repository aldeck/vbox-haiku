; $Id: EMAllA.asm 20278 2007-04-09 11:56:29Z sandervl $
;; @file
; EM Assembly Routines.
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
%include "VBox/asmdefs.mac"
%include "VBox/err.mac"
%include "VBox/x86.mac"

BEGINCODE

;;
; Emulate lock CMPXCHG instruction, CDECL calling conv.
; EMGCDECL(uint32_t) EMGCEmulateLockCmpXchg(RTGCPTR pu32Param1, uint32_t *pu32Param2, uint32_t u32Param3, size_t cbSize);
;
; @returns EFLAGS after the operation, only arithmetic flags is valid.
; @param    [esp + 04h]    Param 1 - First parameter - pointer to first parameter
; @param    [esp + 08h]    Param 2 - Second parameter - pointer to second parameter (eax)
; @param    [esp + 0ch]    Param 3 - Third parameter - third parameter
; @param    [esp + 10h]    Param 4 - Size of parameters, only 1/2/4 is valid.
; @uses     eax, ecx, edx
;
align 16
BEGINPROC   EMGCEmulateLockCmpXchg
    push    ebx
    mov     ecx, [esp + 04h + 4]        ; ecx = first parameter
    mov     ebx, [esp + 08h + 4]        ; ebx = 2nd parameter (eax)
    mov     edx, [esp + 0ch + 4]        ; edx = third parameter
    mov     eax, [esp + 10h + 4]        ; eax = size of parameters

    cmp     al, 4
    je short .do_dword                  ; 4 bytes variant
    cmp     al, 2
    je short .do_word                   ; 2 byte variant
    cmp     al, 1
    je short .do_byte                   ; 1 bytes variant
    int3

.do_dword:
    ; load 2nd parameter's value
    mov     eax, dword [ebx]

    lock cmpxchg dword [ecx], edx            ; do 4 bytes CMPXCHG
    mov     dword [ebx], eax
    jmp     short .done

.do_word:
    ; load 2nd parameter's value
    mov     eax, dword [ebx]

    lock cmpxchg word [ecx], dx              ; do 2 bytes CMPXCHG
    mov     word [ebx], ax
    jmp     short .done

.do_byte:
    ; load 2nd parameter's value
    mov     eax, dword [ebx]

    lock cmpxchg byte [ecx], dl              ; do 1 bytes CMPXCHG
    mov     byte [ebx], al

.done:
    ; collect flags and return.
    pushf
    pop     eax

    pop     ebx
    retn
ENDPROC     EMGCEmulateLockCmpXchg

;;
; Emulate CMPXCHG instruction, CDECL calling conv.
; EMGCDECL(uint32_t) EMGCEmulateCmpXchg(RTGCPTR pu32Param1, uint32_t *pu32Param2, uint32_t u32Param3, size_t cbSize);
;
; @returns EFLAGS after the operation, only arithmetic flags is valid.
; @param    [esp + 04h]    Param 1 - First parameter - pointer to first parameter
; @param    [esp + 08h]    Param 2 - Second parameter - pointer to second parameter (eax)
; @param    [esp + 0ch]    Param 3 - Third parameter - third parameter
; @param    [esp + 10h]    Param 4 - Size of parameters, only 1/2/4 is valid.
; @uses     eax, ecx, edx
;
align 16
BEGINPROC   EMGCEmulateCmpXchg
    push    ebx
    mov     ecx, [esp + 04h + 4]        ; ecx = first parameter
    mov     ebx, [esp + 08h + 4]        ; ebx = 2nd parameter (eax)
    mov     edx, [esp + 0ch + 4]        ; edx = third parameter
    mov     eax, [esp + 10h + 4]        ; eax = size of parameters

    cmp     al, 4
    je short .do_dword                  ; 4 bytes variant
    cmp     al, 2
    je short .do_word                   ; 2 byte variant
    cmp     al, 1
    je short .do_byte                   ; 1 bytes variant
    int3

.do_dword:
    ; load 2nd parameter's value
    mov     eax, dword [ebx]

    cmpxchg dword [ecx], edx            ; do 4 bytes CMPXCHG
    mov     dword [ebx], eax
    jmp     short .done

.do_word:
    ; load 2nd parameter's value
    mov     eax, dword [ebx]

    cmpxchg word [ecx], dx              ; do 2 bytes CMPXCHG
    mov     word [ebx], ax
    jmp     short .done

.do_byte:
    ; load 2nd parameter's value
    mov     eax, dword [ebx]

    cmpxchg byte [ecx], dl              ; do 1 bytes CMPXCHG
    mov     byte [ebx], al

.done:
    ; collect flags and return.
    pushf
    pop     eax

    pop     ebx
    retn
ENDPROC     EMGCEmulateCmpXchg
