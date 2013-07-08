/*
 * ARMV7M System emulation.
 *
 * Copyright (c) 2006-2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GPL.
 */

#include "hw/sysbus.h"
#include "hw/arm-misc.h"
#include "hw/loader.h"
#include "sysemu/sysemu.h"
#include "elf.h"

/* FIXME: make this local state.  */
static qemu_irq pic[64];
static Notifier machine_ready;
static ARMCPU *cpu;

static void armv7m_bitband_init(void)
{
    DeviceState *dev;

    dev = qdev_create(NULL, "ARM,bitband-memory");
    qdev_prop_set_uint32(dev, "base", 0x20000000);
    qdev_init_nofail(dev);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, 0x22000000);

    dev = qdev_create(NULL, "ARM,bitband-memory");
    qdev_prop_set_uint32(dev, "base", 0x40000000);
    qdev_init_nofail(dev);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, 0x42000000);
}

/* Board init.  */

static void armv7m_reset(void *opaque)
{
    ARMCPU *cpu = opaque;

    cpu_reset(CPU(cpu));
}

static void armv7m_machine_ready(Notifier *n, void *opaque)
{
    qemu_register_reset(armv7m_reset, cpu);
}

/* Init CPU and memory for a v7-M based board.
   flash_size and sram_size are in kb.
   Returns the NVIC array.  */

qemu_irq *armv7m_init(MemoryRegion *address_space_mem,
                      int flash_size, int sram_size,
                      const char *kernel_filename, const char *cpu_model)
{
    CPUARMState *env;
    DeviceState *nvic;
    qemu_irq *cpu_pic;
    int image_size;
    uint64_t entry;
    uint64_t lowaddr;
    int i;
    int big_endian;
    MemoryRegion *sram = g_new(MemoryRegion, 1);
    MemoryRegion *flash = g_new(MemoryRegion, 1);
    MemoryRegion *hack = g_new(MemoryRegion, 1);

    flash_size *= 1024;
    sram_size *= 1024;

    if (cpu_model == NULL) {
	cpu_model = "cortex-m3";
    }

#if 0
    /* > 32Mb SRAM gets complicated because it overlaps the bitband area.
       We don't have proper commandline options, so allocate half of memory
       as SRAM, up to a maximum of 32Mb, and the rest as code.  */
    if (ram_size > (512 + 32) * 1024 * 1024)
        ram_size = (512 + 32) * 1024 * 1024;
    sram_size = (ram_size / 2) & TARGET_PAGE_MASK;
    if (sram_size > 32 * 1024 * 1024)
        sram_size = 32 * 1024 * 1024;
    code_size = ram_size - sram_size;
#endif

    /* Flash programming is done via the SCU, so pretend it is ROM.  */
    memory_region_init_ram(flash, "armv7m.flash", flash_size);
    vmstate_register_ram_global(flash);
    memory_region_set_readonly(flash, true);
    memory_region_add_subregion(address_space_mem, 0, flash);
    memory_region_init_ram(sram, "armv7m.sram", sram_size);
    vmstate_register_ram_global(sram);
    memory_region_add_subregion(address_space_mem, 0x20000000, sram);
    armv7m_bitband_init();

    cpu = cpu_arm_init(cpu_model);
    if (cpu == NULL) {
        fprintf(stderr, "Unable to find CPU definition\n");
        exit(1);
    }
    env = &cpu->env;

    nvic = qdev_create(NULL, "armv7m_nvic");
    env->nvic = nvic;
    qdev_init_nofail(nvic);
    cpu_pic = arm_pic_init_cpu(cpu);
    sysbus_connect_irq(SYS_BUS_DEVICE(nvic), 0, cpu_pic[ARM_PIC_CPU_IRQ]);
    for (i = 0; i < 64; i++) {
        pic[i] = qdev_get_gpio_in(nvic, i);
    }

#ifdef TARGET_WORDS_BIGENDIAN
    big_endian = 1;
#else
    big_endian = 0;
#endif

    if (kernel_filename) {
        image_size = load_elf(kernel_filename, NULL, NULL, &entry, &lowaddr,
                              NULL, big_endian, ELF_MACHINE, 1);
        if (image_size < 0) {
            image_size = load_image_targphys(kernel_filename, 0, flash_size);
            lowaddr = 0;
        }
        if (image_size < 0) {
            fprintf(stderr, "qemu: could not load kernel '%s'\n",
                    kernel_filename);
            exit(1);
        }
    }
    /* Hack to map an additional page of ram at the top of the address
       space.  This stops qemu complaining about executing code outside RAM
       when returning from an exception.  */
    memory_region_init_ram(hack, "armv7m.hack", 0x1000);
    vmstate_register_ram_global(hack);
    memory_region_add_subregion(address_space_mem, 0xfffff000, hack);

    /* When the machine is ready, the notify function will register
       the armv7m_reset function. This is done indirectly to ensure
       that the rom_reset function is registered before the
       armv7m_reset function is registered.

       This ensures that ROMs are loaded in to memory *before* the
       CPU resets, which is essential to ensure that the initial PC
       and SP are correctly read from address 0x0.
    */
    machine_ready.notify = armv7m_machine_ready;
    qemu_add_machine_init_done_notifier(&machine_ready);

    return pic;
}
