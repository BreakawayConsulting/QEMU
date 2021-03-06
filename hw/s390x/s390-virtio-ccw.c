/*
 * virtio ccw machine
 *
 * Copyright 2012 IBM Corp.
 * Author(s): Cornelia Huck <cornelia.huck@de.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#include "hw/boards.h"
#include "exec/address-spaces.h"
#include "s390-virtio.h"
#include "hw/s390x/sclp.h"
#include "ioinst.h"
#include "css.h"
#include "virtio-ccw.h"

static int virtio_ccw_hcall_notify(const uint64_t *args)
{
    uint64_t subch_id = args[0];
    uint64_t queue = args[1];
    SubchDev *sch;
    int cssid, ssid, schid, m;

    if (ioinst_disassemble_sch_ident(subch_id, &m, &cssid, &ssid, &schid)) {
        return -EINVAL;
    }
    sch = css_find_subch(m, cssid, ssid, schid);
    if (!sch || !css_subch_visible(sch)) {
        return -EINVAL;
    }
    if (queue >= VIRTIO_PCI_QUEUE_MAX) {
        return -EINVAL;
    }
    virtio_queue_notify(virtio_ccw_get_vdev(sch), queue);
    return 0;

}

static int virtio_ccw_hcall_early_printk(const uint64_t *args)
{
    uint64_t mem = args[0];

    if (mem < ram_size) {
        /* Early printk */
        return 0;
    }
    return -EINVAL;
}

static void virtio_ccw_register_hcalls(void)
{
    s390_register_virtio_hypercall(KVM_S390_VIRTIO_CCW_NOTIFY,
                                   virtio_ccw_hcall_notify);
    /* Tolerate early printk. */
    s390_register_virtio_hypercall(KVM_S390_VIRTIO_NOTIFY,
                                   virtio_ccw_hcall_early_printk);
}

static void ccw_init(QEMUMachineInitArgs *args)
{
    ram_addr_t my_ram_size = args->ram_size;
    MemoryRegion *sysmem = get_system_memory();
    MemoryRegion *ram = g_new(MemoryRegion, 1);
    int shift = 0;
    uint8_t *storage_keys;
    int ret;
    VirtualCssBus *css_bus;

    /* s390x ram size detection needs a 16bit multiplier + an increment. So
       guests > 64GB can be specified in 2MB steps etc. */
    while ((my_ram_size >> (20 + shift)) > 65535) {
        shift++;
    }
    my_ram_size = my_ram_size >> (20 + shift) << (20 + shift);

    /* lets propagate the changed ram size into the global variable. */
    ram_size = my_ram_size;

    /* get a BUS */
    css_bus = virtual_css_bus_init();
    s390_sclp_init();
    s390_init_ipl_dev(args->kernel_filename, args->kernel_cmdline,
                      args->initrd_filename);

    /* register hypercalls */
    virtio_ccw_register_hcalls();

    /* allocate RAM */
    memory_region_init_ram(ram, "s390.ram", my_ram_size);
    vmstate_register_ram_global(ram);
    memory_region_add_subregion(sysmem, 0, ram);

    /* allocate storage keys */
    storage_keys = g_malloc0(my_ram_size / TARGET_PAGE_SIZE);

    /* init CPUs */
    s390_init_cpus(args->cpu_model, storage_keys);

    if (kvm_enabled()) {
        kvm_s390_enable_css_support(s390_cpu_addr2state(0));
    }
    /*
     * Create virtual css and set it as default so that non mcss-e
     * enabled guests only see virtio devices.
     */
    ret = css_create_css_image(VIRTUAL_CSSID, true);
    assert(ret == 0);

    /* Create VirtIO network adapters */
    s390_create_virtio_net(BUS(css_bus), "virtio-net-ccw");
}

static QEMUMachine ccw_machine = {
    .name = "s390-ccw-virtio",
    .alias = "s390-ccw",
    .desc = "VirtIO-ccw based S390 machine",
    .init = ccw_init,
    .block_default_type = IF_VIRTIO,
    .no_cdrom = 1,
    .no_floppy = 1,
    .no_serial = 1,
    .no_parallel = 1,
    .no_sdcard = 1,
    .use_sclp = 1,
    .max_cpus = 255,
    DEFAULT_MACHINE_OPTIONS,
};

static void ccw_machine_init(void)
{
    qemu_register_machine(&ccw_machine);
}

machine_init(ccw_machine_init)
