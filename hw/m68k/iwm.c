/*
 * Integrated Woz Machine (IWM) chip for disk control
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qapi/error.h"
#include "cpu.h"
#include "hw/hw.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "mac128k.h"
#include "sy6522.h"

#define HIGHBIT 7
#define HIGHBIT_MASK (1 << HIGHBIT)
#define LOWBIT 0
#define LOWBIT_MASK (1 << LOWBIT)
#define MODE_RBITS_MASK 0xE0
/* 60 pulses per turn. 10 turns per second */
#define TACH_PULSE (1000000000LL / 1200)

enum
{
    CA0       = 0,
    CA1       = 1,
    CA2       = 2,
    LSTRB     = 3,
    ENABLE    = 4,
    SELECT    = 5, /* 0 - internal, 1 - external */
    Q6        = 6,
    Q7        = 7,
    IWM_LINES = 8
};

const char *iwm_lines[IWM_LINES] = {"CA0", "CA1", "CA2", "LSTRB",
                                    "ENABLE", "SELECT", "Q6", "Q7"};

enum
{
    DIRTN    = 0,
    CSTIN    = 1,
    STEP     = 2,
    WRTPRT   = 3,
    MOTORON  = 4,
    TK0      = 5,
    EIECT    = 6,
    TACH     = 7,
    RDDATA0  = 8,
    RDDATA1  = 9,
    SIDES    = 12,
    DRVIN    = 15,
    IWM_REGS = 16
};

const char *iwm_regs[IWM_REGS] = {"DIRTN", "CSTIN", "STEP", "WRTPRT", "MOTORON",
                                  "TK0", "EJECT", "TACH", "RDDATA0", "RDDATA1",
                                  "---", "---", "SIDES", "---", "---", "DRVIN"};

enum
{
    INTERNAL = 0,
    EXTERNAL = 1,
    DRIVE    = 2,
};

const char *drive_regs[DRIVE] = {"INTERNAL", "EXTERNAL"};

typedef struct {
    M68kCPU *cpu;
    MemoryRegion iomem;
    /* base address */
    target_ulong base;
    via_state *via;
    uint8_t lines[IWM_LINES];
    uint8_t regs[DRIVE][IWM_REGS];
    uint8_t status_reg[DRIVE];
    uint8_t mode_reg[DRIVE];
    uint8_t data_reg[DRIVE];
    uint8_t handshake_reg[DRIVE];
    QEMUTimer *timerTACH;
} iwm_state;

static uint32_t iwm_get_drive(iwm_state *s)
{
    if (s->lines[SELECT]) {
        return EXTERNAL;
    } else {
        return INTERNAL;
    }
}

static void iwm_tach_tick(void *opaque)
{
    iwm_state *s = opaque;
    timer_mod_ns(s->timerTACH,
                 qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + TACH_PULSE);
    s->regs[INTERNAL][TACH] ^= 1;
    s->regs[EXTERNAL][TACH] ^= 1;
}

static void cmd_handw(iwm_state *s)
{
    uint8_t sel = (via_get_reg(s->via, vBufA) & REGA_SEL_MASK) >> SELBIT;
    uint8_t cmd = 0;
    uint8_t value = 0;
    uint8_t *reg = s->regs[iwm_get_drive(s)];
    cmd |= s->lines[CA1] & LOWBIT_MASK;
    cmd = (cmd << 1) | (s->lines[CA0] & LOWBIT_MASK);
    cmd = (cmd << 1) | sel;
    cmd = (cmd << 1) | (s->lines[CA2] & LOWBIT_MASK);
    /* Commands description is taken from Neil Parker's
       Controlling the 3.5 Drive Hardware on the Apple IIGS */
    switch (cmd) {
    case 0:
    case 1:
        value = cmd & LOWBIT_MASK;
        qemu_log("iwm: set DIRTN register to %x\n", value);
        reg[DIRTN] = value;
        break;
    case 3:
        qemu_log("iwm: reset disk-switched flag\n");
        break;
    case 4:
        /* TODO: support switching STEP register while stepping */
        qemu_log("iwm: step one track in the current direction\n");
        break;
    case 8:
    case 9:
        value = cmd & LOWBIT_MASK;
        qemu_log("iwm: set MOTORON register to %x\n", value);
        reg[MOTORON] = value;
        break;
    case 13:
        /* TODO: support ejecting */
        qemu_log("iwm: eject the disk\n");
        break;
    default:
        qemu_log("iwm: unknown command 0x%x\n", cmd);
        break;
    }
}

static void cmd_handr(iwm_state *s)
{
    uint8_t sel = via_get_reg(s->via, vBufA);
    uint8_t cmd = 0;
    uint8_t *reg = s->regs[iwm_get_drive(s)];
    cmd |= s->lines[CA2] & LOWBIT_MASK;
    cmd = (cmd << 1) | (s->lines[CA1] & LOWBIT_MASK);
    cmd = (cmd << 1) | (s->lines[CA0] & LOWBIT_MASK);
    cmd = (cmd << 1) | ((sel & REGA_SEL_MASK) >> SELBIT);
    if ((cmd == 6) || (cmd == 10) || (cmd == 11) || (cmd == 13) || (cmd == 14)) {
        qemu_log("iwm read error: unknown command 0x%x\n", cmd);
    } else {
        s->lines[Q7] &= ~HIGHBIT_MASK;
        s->lines[Q7] |= (reg[cmd] << HIGHBIT) & HIGHBIT_MASK;
        qemu_log("iwm: read %s register, value = %d\n", iwm_regs[cmd], reg[cmd]);
    }
}

static void iwm_writeb(void *opaque, hwaddr offset,
                              uint32_t value)
{
    iwm_state *s = (iwm_state *)opaque;
    offset = (offset - (s->base & ~TARGET_PAGE_MASK)) >> 9;
    if (offset > 0xF) {
        hw_error("Bad IWM write offset 0x%x", (int)offset);
    }
    s->lines[offset >> 1] = offset % 2;
    qemu_log("iwm: line %s set to %d\n", iwm_lines[offset >> 1], (int)offset % 2);
    if ((offset % 2) && (offset >> 1 == LSTRB)) {
        cmd_handw(s);
    }
    if (s->lines[Q6] && s->lines[Q7]) {
        if (!s->lines[ENABLE]) {
            s->mode_reg  [iwm_get_drive(s)] = value;
            s->status_reg[iwm_get_drive(s)] = (s->status_reg[iwm_get_drive(s)] & MODE_RBITS_MASK)
                                            | (value & ~MODE_RBITS_MASK);
            qemu_log("iwm: write mode_reg: %x\n",   s->mode_reg  [iwm_get_drive(s)]);
            qemu_log("iwm: write status_reg: %x\n", s->status_reg[iwm_get_drive(s)]);
        } else {
            s->data_reg  [iwm_get_drive(s)] = value;
            qemu_log("iwm: write data_reg: %x\n",   s->data_reg  [iwm_get_drive(s)]);
        }
    }
}

static uint32_t iwm_readb(void *opaque, hwaddr offset)
{
    iwm_state *s = (iwm_state *)opaque;
    offset = (offset - (s->base & ~TARGET_PAGE_MASK)) >> 9;
    if (offset >= IWM_REGS) {
        hw_error("Bad iwm read offset 0x%x", (int)offset);
    }
    s->lines[offset >> 1] = offset % 2;
    qemu_log("iwm: line %s set to %d\n", iwm_lines[offset >> 1], (int)offset % 2);
    if (s->lines[Q6] && (offset >> 1 == Q7) && !s->lines[LSTRB]) {
        cmd_handr(s);
    }
    if ((offset % 2) && (offset >> 1 == LSTRB)) {
        cmd_handw(s);
    }
    if (s->lines[Q6] && !(s->lines[Q7] & ~HIGHBIT_MASK)) {
        qemu_log("iwm: read status_reg: %x\n", s->status_reg[iwm_get_drive(s)]);
        return (s->lines[offset >> 1] & HIGHBIT_MASK) |
               (s->status_reg[iwm_get_drive(s)] & ~HIGHBIT_MASK);
    } else if (!s->lines[Q6]) {
        if (s->lines[Q7] & ~HIGHBIT_MASK) {
            qemu_log("iwm: read handshake_reg: %x\n", s->handshake_reg[iwm_get_drive(s)]);
            return (s->lines[offset >> 1] & HIGHBIT_MASK) |
                   (s->handshake_reg[iwm_get_drive(s)] & ~HIGHBIT_MASK);
        } else {
            qemu_log("iwm: read data_reg: %x\n", s->data_reg[iwm_get_drive(s)]);
            return (s->lines[offset >> 1] & HIGHBIT_MASK) |
                   (s->data_reg[iwm_get_drive(s)] & ~HIGHBIT_MASK);
        }
    }
    qemu_log("iwm: read unk_reg: %x, Q6(%x), Q7(%x)\n",
             s->lines[offset >> 1], s->lines[Q6], s->lines[Q7]);
    return s->lines[offset >> 1];
}

static const MemoryRegionOps iwm_ops = {
    .old_mmio = {
        .read = {
            iwm_readb,
            iwm_readb,
            iwm_readb,
        },
        .write = {
            iwm_writeb,
            iwm_writeb,
            iwm_writeb,
        },
    },
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void iwm_reset(void *opaque)
{
    iwm_state *s = opaque;
    uint8_t i;
    for (i = 0; i < IWM_LINES; i++) {
        s->lines[i] = 0;
    }
    s->regs[INTERNAL][MOTORON] = 1;
    s->regs[EXTERNAL][MOTORON] = 1;
    s->regs[INTERNAL][WRTPRT]  = 1;
    s->regs[EXTERNAL][WRTPRT]  = 1;
    /* Disk head is alway not stepping */
    s->regs[INTERNAL][STEP]  = 1;
    s->regs[EXTERNAL][STEP]  = 1;
}

void iwm_init(MemoryRegion *sysmem, uint32_t base, M68kCPU *cpu, via_state *via)
{
    iwm_state *s;
    s = (iwm_state *)g_malloc0(sizeof(iwm_state));

    s->base = base;
    memory_region_init_io(&s->iomem, NULL, &iwm_ops, s,
                          "iwm", 0x2000);
    memory_region_add_subregion(sysmem, base & TARGET_PAGE_MASK, &s->iomem);

    s->cpu = cpu;
    s->via = via;

    s->timerTACH = timer_new_ns(QEMU_CLOCK_VIRTUAL, iwm_tach_tick, s);
    timer_mod_ns(s->timerTACH, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL));

    iwm_reset(s);
}
