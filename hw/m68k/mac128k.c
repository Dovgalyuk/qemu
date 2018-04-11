/*
 * Macintosh 128K system emulation.
 *
 * This code is licensed under the GPL
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qapi/error.h"
#include "cpu.h"
#include "hw/hw.h"
#include "mac128k.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "elf.h"
#include "exec/address-spaces.h"
#include "exec/ram_addr.h"
#include "exec/helper-proto.h"
#include "exec/cpu_ldst.h"
#include "sysemu/qtest.h"
#include "ui/console.h"
#include "ui/pixel_ops.h"
#include "mac_int_control.h"
#include "mac_sound_generator.h"

#define ROM_LOAD_ADDR 0x400000
#define MAX_ROM_SIZE  0x20000
#define IWM_BASE_ADDR 0xDFE1FF // dBase
#define VIA_BASE_ADDR 0xEFE1FE // vBase
#define SCREEN_WIDTH  512
#define SCREEN_HEIGHT 342

typedef struct {
    QemuConsole *con;
    int invalidate;
} mac_display;

/* Display controller */

typedef void (*drawfn)(uint8_t *, const uint8_t *, int);

#define DEPTH 8
#include "hw/display/mac_display_template.h"
#define DEPTH 15
#include "hw/display/mac_display_template.h"
#define DEPTH 16
#include "hw/display/mac_display_template.h"
#define DEPTH 32
#include "hw/display/mac_display_template.h"

static drawfn draw_line_table[33] = {
    [0 ... 32]	= NULL,
    [8]		= draw_line_8,
    [15]	= draw_line_15,
    [16]	= draw_line_16,
    [32]	= draw_line_32,
};

static MemoryRegion *ram;

uint8_t *mac_get_ram_ptr(void)
{
    return memory_region_get_ram_ptr(ram);
}

static void mac_update_display(void *opaque)
{
    mac_display *s = (mac_display*)opaque;
    DisplaySurface *surface = qemu_console_surface(s->con);
    uint8_t *dest;
    uint8_t *src;
    int line;

    if (!surface_bits_per_pixel(surface)) {
        return;
    }

    drawfn draw_line = draw_line_table[surface_bits_per_pixel(surface)];
    dest = surface_data(surface);
    src = mac_get_ram_ptr() + 0x1a700;
    for (line = 0 ; line < SCREEN_HEIGHT ; ++line) {
        draw_line(dest, src, SCREEN_WIDTH);
        dest += surface_stride(surface);
        src += SCREEN_WIDTH / 8;
    }
    dpy_gfx_update(s->con, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    s->invalidate = 0;
}

static void mac_invalidate_display(void *opaque) {
    mac_display *s = opaque;
    s->invalidate = 1;
}


static const GraphicHwOps mac_display_ops = {
    .invalidate  = mac_invalidate_display,
    .gfx_update  = mac_update_display,
};

/* Board init.  */

static void mac128k_init(MachineState *machine)
{
    ram_addr_t ram_size = 0x20000;//machine->ram_size;
    hwaddr ramOffset;
    const char *kernel_filename = machine->kernel_filename;
    M68kCPU *cpu;
    int kernel_size;
    MemoryRegion *address_space_mem = get_system_memory();
    ram = g_new(MemoryRegion, 1);
    MemoryRegion *rom = g_new(MemoryRegion, 1);
    via_state *via;
    int_state *int_st;
    sound_generator_state *snd_st;
    mac_display *display = (mac_display *)g_malloc0(sizeof(mac_display));

    cpu = M68K_CPU(cpu_create(machine->cpu_type));
    if (!cpu) {
        hw_error("Unable to find m68k CPU definition\n");
    }

    /* RAM at address zero */
    memory_region_allocate_system_memory(ram, NULL, "mac128k.ram", ram_size);
    memory_region_add_subregion(address_space_mem, 0, ram);
    /* RAM mirroring (address wrap) */
#define MAP_RAM_LOOP(start, base) do { \
        for (ramOffset = (start) ; ramOffset <= ROM_LOAD_ADDR - ram_size \
             ; ramOffset += ram_size) {                            \
            MemoryRegion *alias = g_new(MemoryRegion, 1);          \
            memory_region_init_alias(alias, NULL, "RAM mirror",    \
                ram, 0x0, ram_size);                               \
            memory_region_add_subregion(address_space_mem,         \
                (base) + ramOffset, alias);                        \
        } } while(0)

    MAP_RAM_LOOP(ram_size, 0);
    /* HACK: RAM mirroring for unused address bits 28-31 
       TODO: bits 24-27 are still not mapped
       TODO: ROM and RAM should switch with overlay
     */
    uint32_t high;
    for (high = 1 ; high <= 0xf ; ++high) {
        MAP_RAM_LOOP(0, high * 0x10000000UL);
    }
#undef MAP_RAM_LOOP
    /* RAM mirroring for overlay (address wrap) */
    for (ramOffset = ram_size ; ramOffset <= 0x800000 - ram_size
         ; ramOffset += ram_size)
    {
        /* hack: should be enabled only when overlay is on */
        MemoryRegion *alias = g_new(MemoryRegion, 1);
        memory_region_init_alias(alias, NULL, "RAM mirror", ram, 0x0, ram_size);
        memory_region_add_subregion(address_space_mem, 0x600000 + ramOffset, alias);
    }

    /* ROM */
    memory_region_init_ram(rom, NULL, "mac128k.rom", MAX_ROM_SIZE, &error_fatal);
    memory_region_add_subregion(address_space_mem, ROM_LOAD_ADDR, rom);
    memory_region_set_readonly(rom, true);

    int_st = int_init();
    snd_st = mac_sound_generator_init();
    via = sy6522_init(rom, ram, VIA_BASE_ADDR, int_st, snd_st, cpu);
    iwm_init(address_space_mem, IWM_BASE_ADDR, cpu, via);
    z8530_init(0x800000, via, int_st, cpu);

    /* Display */
    display->con = graphic_console_init(NULL, 0, &mac_display_ops, display);
    qemu_console_resize(display->con, SCREEN_WIDTH, SCREEN_HEIGHT);

    /* Load kernel.  */
    if (kernel_filename) {
        kernel_size = load_image_targphys(kernel_filename,
                                          ROM_LOAD_ADDR,
                                          MAX_ROM_SIZE);
        if (kernel_size < 0) {
            fprintf(stderr, "qemu: could not load kernel '%s'\n",
                    kernel_filename);
            exit(1);
        }
    }
}

static void m128k_machine_init(MachineClass *mc)
{
    mc->desc = "Macintosh 128k";
    mc->init = mac128k_init;
    mc->default_cpu_type = M68K_CPU_TYPE_NAME("m68000");
}

DEFINE_MACHINE("mac128k", m128k_machine_init)
