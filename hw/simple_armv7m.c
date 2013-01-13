/*
 * A very simple ARMv7M device.
 *
 * This does not represent any 'real' physical device, but is rather
 * an idealised machine that is designed for simple device driver
 * programming.
 *
 * Currently, this ideal machine has a single device, which is a debug
 * output.
 *
 * Originally based on the stellaris.c implementation.
 *
 * Copyright (c) 2006 CodeSourcery.
 * Copyright (c) 2013 Breakaway Consulting Pty. Ltd.
 *
 * Original author: Paul Brook
 * Author: Benno Leslie
 *
 * This code is licensed under the GPL.
 */

#include "sysbus.h"
#include "arm-misc.h"
#include "devices.h"
#include "boards.h"
#include "exec/address-spaces.h"
#include "char/char.h"
#include "serial.h"

/* Simple debug. */
typedef struct simple_debug_state {
    SysBusDevice busdev;
    MemoryRegion iomem;
    CharDriverState *chr;
} simple_debug_state;

static uint64_t simple_debug_read(void *opaque, hwaddr offset, unsigned size)
{
    hw_error("simple_debug_read: Bad offset 0x%x\n", (int)offset);
    return 0;
}

static void simple_debug_write(void *opaque, hwaddr offset,
                              uint64_t value, unsigned size)
{
    simple_debug_state *s = (simple_debug_state *) opaque;
    uint8_t ch;

    switch (offset) {
    case 0x00: /* OUTPUT */
        ch = (uint8_t) value;
        qemu_chr_fe_write(s->chr, &ch, 1);
        break;
    default:
        hw_error("simple_debug_write: Bad offset 0x%x\n", (int)offset);
    }
}

static const MemoryRegionOps simple_debug_ops = {
    .read = simple_debug_read,
    .write = simple_debug_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static int simple_debug_init(SysBusDevice *dev)
{
    simple_debug_state *s = FROM_SYSBUS(simple_debug_state, dev);

    memory_region_init_io(&s->iomem, &simple_debug_ops, s, "simple_debug", 0x1000);
    sysbus_init_mmio(dev, &s->iomem);
    s->chr = serial_hds[0];

    return 0;
}

static void simple_debug_class_init(ObjectClass *klass, void *data)
{
    SysBusDeviceClass *sdc = SYS_BUS_DEVICE_CLASS(klass);
    sdc->init = simple_debug_init;
}

static TypeInfo simple_debug_info = {
    .name          = "simple_debug",
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(simple_debug_state),
    .class_init    = simple_debug_class_init,
};

static void simple_armv7m_register_types(void)
{
    type_register_static(&simple_debug_info);
}

type_init(simple_armv7m_register_types);

static void simple_armv7m_init(QEMUMachineInitArgs *args)
{

    MemoryRegion *address_space_mem = get_system_memory();
    int sram_size = 0x100000; /* 1MB */
    int flash_size = 0x100000; /* 1MB */

    armv7m_init(address_space_mem, flash_size, sram_size, args->kernel_filename, args->cpu_model);
    sysbus_create_simple("simple_debug", 0x40050000, NULL);
}

static QEMUMachine simple_armv7m_machine = {
    .name = "simple-armv7m",
    .desc = "Simple ARMv7M",
    .init = simple_armv7m_init,
    .no_cdrom = 1,
    .no_floppy = 1,
    .no_parallel = 1,
    .no_sdcard = 1,
    .max_cpus = 1
};

static void simple_armv7m_machine_init(void)
{
    qemu_register_machine(&simple_armv7m_machine);
}

machine_init(simple_armv7m_machine_init);
