/*
 * Synertek SY6522 Versatile Interface Adapter (VIA)
 * IO mapping corresponds to Macintosh 128k
 * TODO: split VIA structure and IO between two different files
 *
 * Copyright (c) 2015 Pavel Dovgalyuk
 *
 * This code is licensed under the GPL
 */
#include "hw/hw.h"
#include "qemu/timer.h"
#include "hw/m68k/mac128k.h"

typedef struct {
    M68kCPU *cpu;
    MemoryRegion iomem;
    /* base address */
    target_ulong base;
    char value; 
} bia_state;

static void bia_writeb(void *opaque, hwaddr offset,
                              uint32_t value)
{
    bia_state *s = (bia_state *)opaque;
    offset = (offset - (s->base & ~TARGET_PAGE_MASK)) >> 9;
    if (offset > 0xF) {
        hw_error("Bad BIA write offset 0x%x", (int)offset);
    }
    qemu_log("bia_write offset=0x%x value=0x%x\n", (int)offset, value);
    s->value = value;
}

static uint32_t bia_readb(void *opaque, hwaddr offset)
{
    bia_state *s = (bia_state *)opaque;
    offset = (offset - (s->base & ~TARGET_PAGE_MASK)) >> 9;
    if (offset > 0xF) {
        hw_error("Bad BIA read offset 0x%x", (int)offset);
    }
    qemu_log("bia_read offset=0x%x\n", (int)offset);
    return s->value;
}

static const MemoryRegionOps bia_ops = {
    .old_mmio = {
        .read = {
            bia_readb,
            bia_readb,
            bia_readb,
        },
        .write = {
            bia_writeb,
            bia_writeb,
            bia_writeb,
        },
    },
    .endianness = DEVICE_NATIVE_ENDIAN,
};

void test_init(MemoryRegion *sysmem, uint32_t base, M68kCPU *cpu)
{
    bia_state *s;

    s = (bia_state *)g_malloc0(sizeof(bia_state));

    s->base = base;
    memory_region_init_io(&s->iomem, NULL, &bia_ops, s,
                          "test bia", 0x2000);
    memory_region_add_subregion(sysmem, base & TARGET_PAGE_MASK, &s->iomem);

    s->cpu = cpu;
}
