Welcome to GazOS
GazOS is developed by Gareth Owenson (gareth.owenson@gmail.com)

GazOS boots from a SATA (AHCI) hard disk with a FAT32 partition, via
UEFI (the default) or a legacy BIOS; the same disk image works with
both. The kernel is an ordinary file on that filesystem (/KERNEL.ELF).

Quick start
-----------
    ./build.sh      check the build tools are installed, build disk.img
    ./run.sh        build and boot it in QEMU via UEFI
    ./run.sh --bios ... or via legacy BIOS
    ./test.sh       build, then run the end-to-end tests via UEFI and BIOS

./run.sh --serial also prints the serial console in your terminal,
--snapshot leaves disk.img unchanged, and --gdb waits for a debugger.
./test.sh --headless runs the tests without a QEMU window, and
--firmware uefi (or bios) tests one boot mode.

Requirements
------------
gcc (must be able to build 32-bit code with -m32; no 32-bit libc
needed), GNU ld, make, nasm, MinGW-w64 gcc for x86_64 (to build the
UEFI loader), dosfstools (mkfs.fat, fsck.fat), mtools, fdisk (sfdisk),
python3 for the tests, qemu-system-x86 and OVMF (UEFI firmware for
QEMU). On Debian/Ubuntu:

    sudo apt install build-essential nasm gcc-mingw-w64-x86-64 \
                     dosfstools mtools fdisk python3 qemu-system-x86 ovmf

The plain make targets also work: make, make run, make run-bios,
make test, make clean.

How it works
------------
UEFI: firmware -> \EFI\BOOT\BOOTX64.EFI -> /KERNEL.ELF -> shell
BIOS: MBR -> FAT32 boot sector -> stage 2  -> /KERNEL.ELF -> shell

The FAT32 partition is also the EFI System Partition. Under UEFI, the
64-bit loader reads KERNEL.ELF through the firmware, copies it to 1MB,
records the framebuffer and memory size, exits boot services, and drops
from 64-bit long mode to 32-bit protected mode to start the kernel.
Under a BIOS, the MBR chain-loads the partition's boot sector, which
loads stage 2 from the reserved sectors. Stage 2 reads KERNEL.ELF from
FAT32 itself and switches to protected mode. Either way the same 32-bit
kernel starts, and then uses its own PCI, AHCI (SATA) and FAT32 drivers.
Its console is VGA text mode, or a framebuffer text console under UEFI.

See docs/ARCHITECTURE.md for the full details: both boot sequences step
by step, the boot protocol, disk layout, memory map, interrupts,
drivers, testing and debugging.

The files in rootfs/ are copied onto the FAT32 partition, along with
/KERNEL.ELF and the kernel sources in /src. Shell commands: ls/dir,
cat/type, cd, pwd, disk, cls, reboot, help.

To boot real hardware, write disk.img to a disk with dd (this destroys
what is on that disk) and set the SATA controller to AHCI mode. Under
UEFI, turn Secure Boot off (the loader is not signed). The keyboard
driver is PS/2 only; see the limitations in docs/ARCHITECTURE.md.

If you modify the code in anyway, I would appreciate it if you would share
your modifications with me, and perhaps I will include them in the next
release. Also, if you use any code in GazOS for you OS or another project I
would be grateful if you would tell me too, just so I know about it.

Thanks
Gareth
