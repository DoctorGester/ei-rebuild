; The coverage build's function entries (harness_coverage.h): src/ is compiled with /Gh, so every function calls
; _penter once at its start, often inside its prologue (between a test and the cmov that reads its flags), so this
; keeps every register and the flags. It marks the 16 bytes of the image the call returns to in g_coverageEntries
; (tools/coverage.py: a function was entered when a slot inside it is marked). Once harness_coverage.cpp has made the
; code writable (g_coveragePatch) it turns the call's first two bytes into "jmp +3" (EB 03, over the rest of the call)
; with one 2-byte store, so a function pays for its first entry only; a call whose first two bytes straddle a cache
; line stays (such a store is not atomic for a thread that runs the code meanwhile).

EXTERN __ImageBase:BYTE
EXTERN g_coverageEntries:BYTE
EXTERN g_coveragePatch:BYTE

ENTRY_SLOTS EQU 400000h          ; harness_coverage.cpp's kEntrySlots

.code
_penter PROC FRAME
    pushfq
    .allocstack 8
    push rax
    .pushreg rax
    push rcx
    .pushreg rcx
    push rdx
    .pushreg rdx
    .endprolog
    mov rax, [rsp+32]            ; the return address: the call + 5
    lea rcx, __ImageBase
    mov rdx, rax
    sub rdx, rcx
    shr rdx, 4
    cmp rdx, ENTRY_SLOTS
    jae done
    lea rcx, g_coverageEntries
    mov byte ptr [rcx+rdx], 1
    cmp byte ptr g_coveragePatch, 0
    je done
    sub rax, 5                   ; the call
    mov edx, eax
    and edx, 63
    cmp edx, 63
    je done
    cmp byte ptr [rax], 0E8h
    jne done
    mov word ptr [rax], 03EBh
done:
    pop rdx
    pop rcx
    pop rax
    popfq
    ret
_penter ENDP

END
