// Compatibility Support Module (CSM) for UEFI / EDK-II
//
// Copyright © 2013 Intel Corporation
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "bregs.h" // struct bregs
#include "config.h" // CONFIG_*
#include "e820map.h" // e820_add
#include "farptr.h" // MAKE_FLATPTR
#include "hw/pci.h" // pci_to_bdf
#include "hw/pcidevice.h" // pci_probe_devices
#include "hw/pic.h" // pic_irqmask_read
#include "malloc.h" // malloc_csm_preinit
#include "memmap.h" // SYMBOL
#include "output.h" // dprintf
#include "paravirt.h" // qemu_preinit
#include "stacks.h" // wait_threads
#include "std/acpi.h" // RSDP_SIGNATURE
#include "std/bda.h" // struct bios_data_area_s
#include "std/optionrom.h" // struct rom_header
#include "util.h" // copy_smbios_21
#include "hw/usb.h"
#include "string.h"

#define UINT8 u8
#define UINT16 u16
#define UINT32 u32
#include "std/LegacyBios.h"

struct rsdp_descriptor csm_rsdp VARFSEG __aligned(16);

EFI_COMPATIBILITY16_TABLE csm_compat_table VARFSEG __aligned(16) = {
    .Signature = 0x24454649,
    .TableChecksum = 0 /* Filled in by checkrom.py */,
    .TableLength = sizeof(csm_compat_table),
    .Compatibility16CallSegment = SEG_BIOS,
    .Compatibility16CallOffset = 0 /* Filled in by checkrom.py */,
    .OemIdStringPointer = (u32)"SeaBIOS",
    .AcpiRsdPtrPointer = (u32)&csm_rsdp,
};

EFI_TO_COMPATIBILITY16_INIT_TABLE *csm_init_table;
EFI_TO_COMPATIBILITY16_BOOT_TABLE *csm_boot_table;

static u16 PICMask = PIC_IRQMASK_DEFAULT;

extern void __csm_return(struct bregs *regs) __noreturn;

static void
csm_return(struct bregs *regs)
{
    u32 rommax = rom_get_max();

    dprintf(3, "handle_csm returning AX=%04x\n", regs->ax);

    csm_compat_table.UmaAddress = rommax;
    csm_compat_table.UmaSize = SYMBOL(final_readonly_start) - rommax;

    PICMask = pic_irqmask_read();
    __csm_return(regs);
}

static void
csm_maininit(struct bregs *regs)
{
    interface_init();
    pci_probe_devices();

    csm_compat_table.PnPInstallationCheckSegment = SEG_BIOS;
    csm_compat_table.PnPInstallationCheckOffset = get_pnp_offset();

    regs->ax = 0;

    csm_return(regs);
}

/* Legacy16InitializeYourself */
static void
handle_csm_0000(struct bregs *regs)
{
    qemu_preinit();

    dprintf(3, "Legacy16InitializeYourself table %04x:%04x\n", regs->es,
            regs->bx);

    csm_init_table = MAKE_FLATPTR(regs->es, regs->bx);

    dprintf(3, "BiosLessThan1MB %08x\n", csm_init_table->BiosLessThan1MB);
    dprintf(3, "HiPmmMemory     %08x\n", csm_init_table->HiPmmMemory);
    dprintf(3, "HiPmmMemorySize %08x\n", csm_init_table->HiPmmMemorySizeInBytes);
    dprintf(3, "ReverseThunk    %04x:%04x\n", csm_init_table->ReverseThunkCallSegment,
            csm_init_table->ReverseThunkCallOffset);
    dprintf(3, "NumE820Entries  %08x\n", csm_init_table->NumberE820Entries);
    dprintf(3, "OsMemoryAbove1M %08x\n", csm_init_table->OsMemoryAbove1Mb);
    dprintf(3, "ThunkStart      %08x\n", csm_init_table->ThunkStart);
    dprintf(3, "ThunkSize       %08x\n", csm_init_table->ThunkSizeInBytes);
    dprintf(3, "LoPmmMemory     %08x\n", csm_init_table->LowPmmMemory);
    dprintf(3, "LoPmmMemorySize %08x\n", csm_init_table->LowPmmMemorySizeInBytes);

    malloc_csm_preinit(csm_init_table->LowPmmMemory,
                       csm_init_table->LowPmmMemorySizeInBytes,
                       csm_init_table->HiPmmMemory,
                       csm_init_table->HiPmmMemorySizeInBytes);
    reloc_preinit(csm_maininit, regs);
}

/* Legacy16UpdateBbs */
static void
handle_csm_0001(struct bregs *regs)
{
    if (!CONFIG_BOOT) {
        regs->ax = 1;
        return;
    }

    dprintf(3, "Legacy16UpdateBbs table %04x:%04x\n", regs->es, regs->bx);

    csm_boot_table = MAKE_FLATPTR(regs->es, regs->bx);
    dprintf(3, "MajorVersion %04x\n", csm_boot_table->MajorVersion);
    dprintf(3, "MinorVersion %04x\n", csm_boot_table->MinorVersion);
    dprintf(3, "AcpiTable %08x\n", csm_boot_table->AcpiTable);
    dprintf(3, "SmbiosTable %08x\n", csm_boot_table->SmbiosTable);
    dprintf(3, "SmbiosTableLength %08x\n", csm_boot_table->SmbiosTableLength);
//    dprintf(3, "SioData %08x\n", csm_boot_table->SioData);
    dprintf(3, "DevicePathType %04x\n", csm_boot_table->DevicePathType);
    dprintf(3, "PciIrqMask %04x\n", csm_boot_table->PciIrqMask);
    dprintf(3, "NumberE820Entries %08x\n", csm_boot_table->NumberE820Entries);
//    dprintf(3, "HddInfo %08x\n", csm_boot_table->HddInfo);
    dprintf(3, "NumberBbsEntries %08x\n", csm_boot_table->NumberBbsEntries);
    dprintf(3, "BBsTable %08x\n", csm_boot_table->BbsTable);
    dprintf(3, "SmmTable %08x\n", csm_boot_table->SmmTable);
    dprintf(3, "OsMemoryAbove1Mb %08x\n", csm_boot_table->OsMemoryAbove1Mb);
    dprintf(3, "UnconventionalDeviceTable %08x\n", csm_boot_table->UnconventionalDeviceTable);

    regs->ax = 0;
}

/* PrepareToBoot */
static void
handle_csm_0002(struct bregs *regs)
{
    if (!CONFIG_BOOT) {
        regs->ax = 1;
        return;
    }

    dprintf(3, "PrepareToBoot table %04x:%04x\n", regs->es, regs->bx);

    struct e820entry *p = (void *)csm_compat_table.E820Pointer;
    int i;
    for (i=0; i < csm_compat_table.E820Length / sizeof(struct e820entry); i++)
        e820_add(p[i].start, p[i].size, p[i].type);

    if (csm_init_table->HiPmmMemorySizeInBytes > BUILD_MIN_HIGHTABLE) {
        u32 hi_pmm_end = csm_init_table->HiPmmMemory + csm_init_table->HiPmmMemorySizeInBytes;
        e820_add(hi_pmm_end - BUILD_MIN_HIGHTABLE, BUILD_MIN_HIGHTABLE, E820_RESERVED);
    }

    // For PCIBIOS 1ab10e
    if (csm_compat_table.IrqRoutingTablePointer &&
        csm_compat_table.IrqRoutingTableLength) {
        PirAddr = (void *)csm_compat_table.IrqRoutingTablePointer;
        dprintf(3, "CSM PIRQ table at %p\n", PirAddr);
    }

    // For find_resume_vector()... and find_acpi_features()
    if (csm_rsdp.signature == RSDP_SIGNATURE) {
        RsdpAddr = &csm_rsdp;
        dprintf(3, "CSM ACPI RSDP at %p\n", RsdpAddr);

        find_acpi_features();
    }

    // SMBIOS table needs to be copied into the f-seg
    // XX: OVMF doesn't seem to set SmbiosTableLength so don't check it
    if (csm_boot_table->SmbiosTable)
        copy_smbios_21((void *)csm_boot_table->SmbiosTable);

    // MPTABLE is just there; we don't care where.

    // EFI may have reinitialised the video using its *own* driver.
    enable_vga_console();

    // EFI fills this in for us. Zero it for now...
    struct bios_data_area_s *bda = MAKE_FLATPTR(SEG_BDA, 0);
    bda->hdcount = 0;

    thread_setup();
    mathcp_setup();
    timer_setup();
    clock_setup();
    device_hardware_setup();
    wait_threads();
    interactive_bootmenu();

    prepareboot();

    regs->ax = 0;
}

/* Boot */
static void
handle_csm_0003(struct bregs *regs)
{
    if (!CONFIG_BOOT) {
        regs->ax = 1;
        return;
    }

    dprintf(3, "Boot\n");

    startBoot();

    regs->ax = 1;
}

/* Legacy16DispatchOprom */
static void
handle_csm_0005(struct bregs *regs)
{
    EFI_DISPATCH_OPROM_TABLE *table = MAKE_FLATPTR(regs->es, regs->bx);
    struct rom_header *rom;
    u16 bdf;

    if (!CONFIG_OPTIONROMS) {
        regs->ax = 1;
        return;
    }

    dprintf(3, "Legacy16DispatchOprom rom %p\n", table);

    dprintf(3, "OpromSegment   %04x\n", table->OpromSegment);
    dprintf(3, "RuntimeSegment %04x\n", table->RuntimeSegment);
    dprintf(3, "PnPInstallationCheck %04x:%04x\n",
            table->PnPInstallationCheckSegment,
            table->PnPInstallationCheckOffset);
    dprintf(3, "RuntimeSegment %04x\n", table->RuntimeSegment);

    rom = MAKE_FLATPTR(table->OpromSegment, 0);
    bdf = pci_bus_devfn_to_bdf(table->PciBus, table->PciDeviceFunction);

    rom_reserve(rom->size * 512);

    // XX PnP seg/ofs should never be other than default
    callrom(rom, bdf);

    rom_confirm(rom->size * 512);

    regs->bx = 0; // FIXME
    regs->ax = 0;
}

/* Legacy16GetTableAddress */
static void
handle_csm_0006(struct bregs *regs)
{
    u16 size = regs->cx;
    u16 align = regs->dx;
    u16 region = regs->bx; // (1 for F000 seg, 2 for E000 seg, 0 for either)
    void *chunk = NULL;

    dprintf(3, "Legacy16GetTableAddress size %x align %x region %d\n",
        size, align, region);

    if (!region)
        region = 3;

    // DX = Required address alignment. Bit mapped.
    // First non-zero bit from the right is the alignment.*/
    if (align) {
        align = 1 << __ffs(align);
        if (align < MALLOC_MIN_ALIGN)
            align = MALLOC_MIN_ALIGN;
    } else {
        align = MALLOC_MIN_ALIGN;
    }

    if (region & 2)
        chunk = _malloc(&ZoneLow, size, align);
    if (!chunk && (region & 1))
        chunk = _malloc(&ZoneFSeg, size, align);

    dprintf(3, "Legacy16GetTableAddress size %x align %x region %d yields %p\n",
        size, align, region, chunk);
    if (chunk) {
        regs->ds = FLATPTR_TO_SEG(chunk);
        regs->bx = FLATPTR_TO_OFFSET(chunk);
        regs->ax = 0;
    } else {
        regs->ax = 1;
    }
}

void VISIBLE32INIT
handle_csm(struct bregs *regs)
{
    ASSERT32FLAT();

    if (!CONFIG_CSM)
        return;

    dprintf(3, "handle_csm regs %p AX=%04x\n", regs, regs->ax);

    code_mutable_preinit();
    pic_irqmask_write(PICMask);

    switch(regs->ax) {
    case 0000: handle_csm_0000(regs); break;
    case 0001: handle_csm_0001(regs); break;
    case 0002: handle_csm_0002(regs); break;
    case 0003: handle_csm_0003(regs); break;
//    case 0004: handle_csm_0004(regs); break;
    case 0005: handle_csm_0005(regs); break;
    case 0006: handle_csm_0006(regs); break;
//    case 0007: handle_csm_0007(regs); break;
//    case 0008: hamdle_csm_0008(regs); break;
    default: regs->al = 1;
    }

    csm_return(regs);
}

static int csm_prio_to_seabios(u16 csm_prio)
{
    switch (csm_prio) {
    case BBS_DO_NOT_BOOT_FROM:
    case BBS_IGNORE_ENTRY:
        return -1;

    case BBS_LOWEST_PRIORITY:
    case BBS_UNPRIORITIZED_ENTRY:
    default:
        // SeaBIOS default priorities start at 1, with 0 being used for
        // an item explicitly selected from interactive_bootmenu().
        // As in find_prio(), add 1 to the value being returned.
        return csm_prio + 1;
    }
}

int csm_bootprio_ata(struct pci_device *pci, int chanid, int slave)
{
    if (!csm_boot_table)
        return -1;
    BBS_TABLE *bbs = (void *)csm_boot_table->BbsTable;
    int index = 1 + (chanid * 2) + slave;
    dprintf(3, "CSM bootprio for ATA%d,%d (index %d) is %d\n", chanid, slave,
            index, bbs[index].BootPriority);
    return csm_prio_to_seabios(bbs[index].BootPriority);
}

int csm_bootprio_fdc(struct pci_device *pci, int port, int fdid)
{
    if (!csm_boot_table)
        return -1;
    BBS_TABLE *bbs = (void *)csm_boot_table->BbsTable;
    dprintf(3, "CSM bootprio for FDC is %d\n", bbs[0].BootPriority);
    return csm_prio_to_seabios(bbs[0].BootPriority);
}

int csm_bootprio_pci(struct pci_device *pci)
{
    if (!csm_boot_table)
        return -1;
    BBS_TABLE *bbs = (void *)csm_boot_table->BbsTable;
    int i;

    for (i = 5; i < csm_boot_table->NumberBbsEntries; i++) {
        if (pci->bdf == pci_to_bdf(bbs[i].Bus, bbs[i].Device, bbs[i].Function)) {
            dprintf(3, "CSM bootprio for PCI(%d,%d,%d) is %d\n", bbs[i].Bus,
                    bbs[i].Device, bbs[i].Function, bbs[i].BootPriority);
            return csm_prio_to_seabios(bbs[i].BootPriority);
        }
    }
    return -1;
}

static int parse_hex(const char *p)
{
    int value = 0;

    if (!p)
        return -1;

    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
    }

    for (;;) {
        char c = *p;
        int digit;

        if (c >= '0' && c <= '9')
            digit = c - '0';
        else if (c >= 'a' && c <= 'f')
            digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')
            digit = c - 'A' + 10;
        else
            break;

        value = (value << 4) | digit;
        p++;
    }
    return value;
}

#define MAX_USB_DEPTH 8

static int uefi_usb_dev_chain(const char *p, int *ports,
                                    int max_ports)
{
    if (!p || !*p || max_ports <= 0)
        return 0;

    // Skip leading slashes
    while (*p == '/')
        p++;

    // End of string
    if (!*p)
        return 0;

    int count = 0;

    // Check for USB(...) node
    if (p[0] == 'U' &&
        p[1] == 'S' &&
        p[2] == 'B' &&
        p[3] == '(') {

        const char *hex = p + 4;

        ports[0] = parse_hex(hex);
        count = 1;

        // Skip to end of this USB(...) node
        while (*hex && *hex != '/')
            hex++;

        // Recurse into the rest of the path
        return count + uefi_usb_dev_chain(hex,
                                            ports + 1,
                                            max_ports - 1);
    }

    // Not a USB(...) node, skip this segment
    while (*p && *p != '/')
        p++;

    return uefi_usb_dev_chain(p, ports, max_ports);
}

static int usb_dev_chain(struct usbdevice_s *dev, int *hub_ports, 
                            int max_ports)
{
    if (!dev || !dev->hub)
        return 0;

    if (max_ports <= 0)
        return -1;

    int depth = usb_dev_chain(dev->hub->usbdev,
                                hub_ports,
                                max_ports - 1);

    if (depth < 0)
        return -1;

    hub_ports[depth] = dev->port;

    return depth + 1;
}

static int usb_dev_chain_equal(const int *chain1, int len1,
                                const int *chain2, int len2)
{
    if (len1 != len2)
        return 0;

    for (int i = 0; i < len1; i++)
        if (chain1[i] != chain2[i])
            return 0;

    return 1;
}

int csm_bootprio_usb(struct usbdevice_s *usbdev)
{
    if (!csm_boot_table) {
        dprintf(3, "CSM-USB: no BBS table\n");
        return -1;
    }

    if (!usbdev || !usbdev->hub || !usbdev->hub->cntl) {
        dprintf(3, "CSM-USB: invalid usbdev or hub\n");
        return -1;
    }

    // Only support PCI based USB controllers for now
    struct pci_device *pci = usbdev->hub->cntl->pci;
    if (!pci) {
        dprintf(3, "CSM-USB: device has no PCI controller\n");
        return -1;
    }

    u8 bus  = pci_bdf_to_bus(pci->bdf);
    u8 dev  = pci_bdf_to_dev(pci->bdf);
    u8 func = pci_bdf_to_fn(pci->bdf);

    dprintf(3, "CSM-USB: matching PCI %02x:%02x.%x (usbdev port=%u addr=%u)\n",
            bus, dev, func, usbdev->port, usbdev->devaddr);

    int usbdev_ports[MAX_USB_DEPTH] = {};
    int usbdev_count = usb_dev_chain(usbdev, usbdev_ports, MAX_USB_DEPTH);

    if (usbdev_count <= 0) {
        dprintf(3, "CSM-USB: no USB chain from usbdev\n");
        return -1;
    }

    dprintf(3, "CSM-USB: SeaBIOS USB chain (%d ports): ", usbdev_count);
    for (int i = 0; i < usbdev_count; i++)
        dprintf(3, "%d ", usbdev_ports[i]);
    dprintf(3, "\n");

    BBS_TABLE *bbs = (void *)csm_boot_table->BbsTable;
    int entry_count = csm_boot_table->NumberBbsEntries;

    for (int i = 0; i < entry_count; i++) {

        if (bbs[i].Bus      != bus ||
            bbs[i].Device   != dev ||
            bbs[i].Function != func)
            continue;

        const char *desc = (const char *)(
            (bbs[i].DescStringSegment << 4) + bbs[i].DescStringOffset
        );

        if (!desc || !*desc)
            continue;

        dprintf(3, "CSM-USB: checking BBS[%d] DevicePath: %s\n", i, desc);

        int dp_ports[MAX_USB_DEPTH] = {};
        int dp_count = uefi_usb_dev_chain(desc, dp_ports, MAX_USB_DEPTH);

        if (dp_count <= 0) {
            dprintf(3, "CSM-USB:   no USB() nodes in DevicePath\n");
            continue;
        }

        dprintf(3, "CSM-USB:   UEFI USB chain (%d ports): ", dp_count);
        for (int j = 0; j < dp_count; j++)
            dprintf(3, "%d ", dp_ports[j]);
        dprintf(3, "\n");

        if (usb_dev_chain_equal(usbdev_ports, usbdev_count,
                                 dp_ports, dp_count)) {

            dprintf(3, "CSM-USB:   MATCHED priority 0x%04x\n",
                    bbs[i].BootPriority);

            return csm_prio_to_seabios(bbs[i].BootPriority);
        }

        dprintf(3, "CSM-USB:   no match\n");
    }

    dprintf(3, "CSM-USB: no matching USB device found\n");
    return -1;
}
