# Roadmap

## User mode and syscalls

- own GDT with ring 3 code/data descriptors instead of the minimal boot one
- update `RSP0` in the TSS on every switch
- `syscall`/`sysret`: `EFER.SCE`, `STAR`, `LSTAR`, `SFMASK`, an asm entry that
  moves to the task's kernel stack and calls a C dispatcher
- first syscalls: `write`, `exit`, `yield`, `getpid`
- per-process address spaces (one CR3 each), user pages marked USER
- a first ring 3 program embedded in the kernel to test the whole path

## VFS and ramfs

- VFS nodes with `open`/`read`/`write`/`readdir`, mount points
- ramfs on the heap as the first filesystem
- `ls`, `cat`, `write`, `mkdir` in the shell
- an initrd loaded by GRUB as a multiboot2 module

With both done the initrd can carry real user binaries, and Sparks can finally
run programs that aren't compiled into it.

## Later

- ATA/AHCI and an on-disk filesystem
- APIC, then SMP
- per-window dirty rectangles in the wm
- apps in ring 3 talking to the wm through syscalls
- a real clock from the RTC instead of uptime in the taskbar
