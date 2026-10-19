; =============================================================================
;  DELTA - stage 2
;  Real mode: collect the firmware memory map, load the kernel, enable A20.
;  Protected mode: copy the kernel to its link address, build identity page
;  tables, enter long mode, and hand control to the kernel entry stub.
; =============================================================================
BITS 16
ORG 0x7E00

%define E820_ADDR     0xA000
%define BOOTINFO_ADDR 0xB000
%define HEADER_ADDR   0x5000
%define STAGING_ADDR  0x10000
%define PAGE_PML4     0x80000
%define PAGE_PDPT     0x81000
%define PAGE_PD       0x82000
%define KERNEL_LBA    18
%define HEADER_LBA    17
%define MAX_KERNEL    0x70000          ; staging must end before page tables

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

    mov si, msg_stage2
    call puts

    call enable_a20
    mov si, msg_a20
    call puts

    call get_memory_map
    mov si, msg_mem
    call puts

    ; The stage-1 sector holds a build flag at offset 508. Preserve it for the
    ; kernel so the same image can boot into self-test or interactive mode.
    mov al, [0x7C00 + 508]
    mov [build_flags], al

    ; Read the kernel image header (one sector).
    mov word  [dap_count], 1
    mov word  [dap_offset], HEADER_ADDR & 0xFFFF
    mov word  [dap_segment], 0
    mov dword [dap_lba], HEADER_LBA
    call disk_read
    jc  disk_error

    cmp dword [HEADER_ADDR], 'DLTK'
    jne bad_header
    cmp dword [HEADER_ADDR + 4], 1
    jne bad_header

    ; Reject images that would overlap the page tables at 0x80000.
    mov eax, [HEADER_ADDR + 16]
    mov edx, [HEADER_ADDR + 20]
    test edx, edx
    jnz image_too_big
    test eax, eax
    jz  bad_header
    cmp eax, MAX_KERNEL
    ja  image_too_big
    mov [kernel_size], eax

    mov ecx, eax
    add ecx, 511
    shr ecx, 9
    mov [remaining], ecx
    cmp ecx, 0
    jz  bad_header

    ; Load the flat kernel image at 0x10000.
    mov dword [dap_lba], KERNEL_LBA
    mov word  [dap_segment], 0x1000
    mov word  [dap_offset], 0
.load_loop:
    mov ecx, [remaining]
    test ecx, ecx
    jz  .loaded
    cmp ecx, 64
    jbe .chunk_ok
    mov ecx, 64
.chunk_ok:
    mov [dap_count], cx
    call disk_read
    jc  disk_error

    movzx eax, word [dap_count]
    sub [remaining], eax

    ; Advance the destination segment by 32 paragraphs per sector and LBA by
    ; the number of sectors just read.
    mov bx, ax
    shl bx, 5
    add [dap_segment], bx
    add dword [dap_lba], eax
    jmp .load_loop

.loaded:
    mov si, msg_kernel
    call puts

    ; Build the boot information block consumed by kmain.
    mov dword [BOOTINFO_ADDR + 0], 'DLTA'
    mov dword [BOOTINFO_ADDR + 4], 1
    movzx eax, byte [build_flags]
    mov [BOOTINFO_ADDR + 8], eax
    movzx eax, word [e820_count]
    mov [BOOTINFO_ADDR + 12], eax
    mov dword [BOOTINFO_ADDR + 16], E820_ADDR
    mov dword [BOOTINFO_ADDR + 20], 0
    mov dword [BOOTINFO_ADDR + 24], 0x100000
    mov dword [BOOTINFO_ADDR + 28], 0
    mov eax, [kernel_size]
    mov [BOOTINFO_ADDR + 32], eax
    mov dword [BOOTINFO_ADDR + 36], 0
    mov dword [BOOTINFO_ADDR + 40], 0xB8000
    mov dword [BOOTINFO_ADDR + 44], 0
    mov dword [BOOTINFO_ADDR + 48], 80
    mov dword [BOOTINFO_ADDR + 52], 25

    ; Enter 32-bit protected mode.
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or  eax, 1
    mov cr0, eax
    jmp 0x08:pm32

BITS 32
pm32:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x7000

    ; Copy the staging buffer to the kernel's physical link address.
    mov esi, STAGING_ADDR
    mov edi, 0x100000
    mov ecx, [kernel_size]
    rep movsb

    ; Clear and build three page-table pages: PML4 -> PDPT -> PD[512] with
    ; 2 MiB identity pages covering the first 1 GiB of physical memory.
    mov edi, PAGE_PML4
    xor eax, eax
    mov ecx, (3 * 4096) / 4
    rep stosd

    mov dword [PAGE_PML4 + 0], PAGE_PDPT | 3
    mov dword [PAGE_PML4 + 4], 0
    mov dword [PAGE_PDPT + 0], PAGE_PD | 3
    mov dword [PAGE_PDPT + 4], 0

    mov edi, PAGE_PD
    mov eax, 0x00000083             ; present + writable + 2 MiB page
    mov ecx, 512
.pd_fill:
    mov [edi], eax
    mov dword [edi + 4], 0
    add eax, 0x200000
    add edi, 8
    dec ecx
    jnz .pd_fill

    ; PAE + SSE state + long mode + paging.  The x86-64 ABI allows the
    ; compiler to use SSE for block moves even in freestanding C, so OSFXSR
    ; and OSXMMEXCPT must be enabled before calling C.
    mov eax, cr4
    or  eax, (1 << 5) | (1 << 9) | (1 << 10)
    mov cr4, eax
    mov eax, cr0
    and eax, ~(1 << 2)              ; clear EM
    or  eax, (1 << 1)               ; set MP
    mov cr0, eax
    mov ecx, 0xC0000080             ; IA32_EFER
    rdmsr
    or  eax, (1 << 8) | (1 << 11)   ; LME | NXE
    wrmsr
    mov eax, PAGE_PML4
    mov cr3, eax
    mov eax, cr0
    or  eax, 0x80000001             ; PG | PE
    mov cr0, eax

    jmp 0x18:long_entry

BITS 64
long_entry:
    mov ax, 0x20
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov rsp, 0x9F000
    xor rbp, rbp
    mov rdi, BOOTINFO_ADDR
    mov rax, 0x100000
    jmp rax

BITS 16
; ---------------------------------------------------------------------------
; Firmware helpers
; ---------------------------------------------------------------------------
enable_a20:
    ; Fast A20 through System Control Port A.
    in  al, 0x92
    or  al, 0x02
    and al, 0xFE
    out 0x92, al
    ret

get_memory_map:
    xor ebx, ebx
    xor bp, bp
    mov di, E820_ADDR
.loop:
    mov eax, 0xE820
    mov edx, 0x534D4150
    mov ecx, 24
    mov dword [di + 20], 1
    int 0x15
    jc  .done
    cmp eax, 0x534D4150
    jne .done
    cmp ecx, 20
    jb  .skip
    inc bp
    add di, 24
    cmp bp, 64
    jae .done
.skip:
    test ebx, ebx
    jz  .done
    jmp .loop
.done:
    mov [e820_count], bp
    test bp, bp
    jnz .have
    ; Fallback map for firmware that does not implement E820: low 640 KiB and
    ; a conservative 127 MiB above 1 MiB. This is only a last resort.
    mov dword [E820_ADDR + 0], 0
    mov dword [E820_ADDR + 4], 0
    mov dword [E820_ADDR + 8], 0x9FC00
    mov dword [E820_ADDR + 12], 0
    mov dword [E820_ADDR + 16], 1
    mov dword [E820_ADDR + 24], 0x100000
    mov dword [E820_ADDR + 28], 0
    mov dword [E820_ADDR + 32], 0x7F00000
    mov dword [E820_ADDR + 36], 0
    mov dword [E820_ADDR + 40], 1
    mov word [e820_count], 2
.have:
    ret

; Read sectors described by the DAP. Sets carry on failure.
disk_read:
    mov si, dap
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jc  .fail
    clc
    ret
.fail:
    stc
    ret

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

disk_error:
    mov si, msg_disk_err
    call puts
    jmp halt16
bad_header:
    mov si, msg_bad_header
    call puts
    jmp halt16
image_too_big:
    mov si, msg_too_big
    call puts
halt16:
    cli
.h:
    hlt
    jmp .h

; ---------------------------------------------------------------------------
; Data
; ---------------------------------------------------------------------------
boot_drive:   db 0
build_flags:  db 0
kernel_size:  dd 0
remaining:    dd 0
e820_count:   dw 0

align 4
dap:
    db 0x10
    db 0
dap_count:    dw 0
dap_offset:   dw 0
dap_segment:  dw 0
dap_lba:      dq 0

align 8
gdt_start:
    dq 0
    dq 0x00CF9A000000FFFF           ; 0x08: 32-bit code
    dq 0x00CF92000000FFFF           ; 0x10: 32-bit data
    dq 0x00AF9A000000FFFF           ; 0x18: 64-bit code
    dq 0x00CF92000000FFFF           ; 0x20: 64-bit data
gdt_end:
gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

msg_stage2:      db 'DELTA stage2', 13, 10, 0
msg_a20:         db '  A20 unlocked', 13, 10, 0
msg_mem:         db '  memory map captured', 13, 10, 0
msg_kernel:      db '  kernel staged', 13, 10, 0
msg_disk_err:    db 'DELTA: stage2 disk error', 13, 10, 0
msg_bad_header:  db 'DELTA: bad kernel header', 13, 10, 0
msg_too_big:     db 'DELTA: kernel image too large', 13, 10, 0

times 16 * 512 - ($ - $$) db 0
