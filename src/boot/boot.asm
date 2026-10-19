; =============================================================================
;  DELTA - stage 1 boot sector
;  Loads stage 2 from LBA 1..16 to 0x7E00, then transfers control.
;  Disk layout (512-byte sectors):
;    0        this boot sector
;    1..16    stage 2 (max 8 KiB)
;    17       kernel image header ("DLTK")
;    18..     kernel payload (flat binary loaded at 0x100000)
; =============================================================================
BITS 16
ORG 0x7C00

%define STAGE2_LBA   1
%define STAGE2_SECS  16
%define STAGE2_ADDR  0x7E00

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    cld
    mov [boot_drive], dl

    mov si, msg_boot
    call puts

    ; Reset the disk system first. Some BIOSes need this after POST.
    xor ah, ah
    mov dl, [boot_drive]
    int 0x13
    jc  disk_error

    ; Extended read: 16 sectors, LBA 1 -> 0000:7E00
    mov word  [dap_count], STAGE2_SECS
    mov word  [dap_offset], STAGE2_ADDR
    mov word  [dap_segment], 0
    mov dword [dap_lba], STAGE2_LBA
    mov si, dap
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jc  disk_error

    mov si, msg_ok
    call puts
    mov dl, [boot_drive]
    jmp 0x0000:STAGE2_ADDR

disk_error:
    mov si, msg_err
    call puts
    cli
.hang:
    hlt
    jmp .hang

; Print a zero-terminated string with BIOS teletype.
puts:
    push ax
    push si
.next:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    jmp .next
.done:
    pop si
    pop ax
    ret

boot_drive: db 0

; Disk Address Packet (must stay within this 512-byte sector).
align 4
dap:
    db 0x10
    db 0
dap_count:    dw 0
dap_offset:   dw 0
dap_segment:  dw 0
dap_lba:      dq 0

msg_boot: db 'DELTA stage1', 13, 10, 0
msg_ok:   db ' -> stage2', 13, 10, 0
msg_err:  db 'DELTA: disk read error', 13, 10, 0

; Offset 508 is a build-time flag byte. The boot signature occupies 510..511.
times 508 - ($ - $$) db 0
build_flags: db 0
times 510 - ($ - $$) db 0
dw 0xAA55
