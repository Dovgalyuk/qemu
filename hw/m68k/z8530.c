#include "qemu/osdep.h"
#include "hw/hw.h"
#include "cpu.h"
#include "hw/sysbus.h"
#include "chardev/char.h"
#include "ui/console.h"
#include "ui/input.h"
#include "qemu/log.h"
#include "exec/address-spaces.h"
#include "mac128k.h"
#include "mac_mouse.h"
#include "z8530.h"
#include "mac_int_control.h"

typedef enum {
    chn_a, chn_b,
} ChnID;

#define CHN_C(s) ((s)->chn == chn_b? 'b' : 'a')

typedef enum {
    ser, mouse,
} ChnType;

#define SERIAL_REGS 16
typedef struct ChannelState {
    qemu_irq irq;
    struct ChannelState *otherchn;
    uint32_t reg;
    uint8_t wregs[SERIAL_REGS], rregs[SERIAL_REGS];
    ChnID chn; // this channel, A (base+4) or B (base+0)
    ChnType type;
} ChannelState;

typedef struct Z8530State {
    struct ChannelState chn[2];
    MemoryRegion iomem;
    M68kCPU *cpu;
    int_state *int_st;
} Z8530State;

#define SERIAL_CTRL 0
#define SERIAL_DATA 1

#define W_CMD     0
#define CMD_PTR_MASK   0x07
#define CMD_CMD_MASK   0x38
#define CMD_HI         0x08
#define CMD_CLR_TXINT  0x28
#define CMD_CLR_IUS    0x38
#define W_INTR    1
#define INTR_INTALL    0x01
#define INTR_TXINT     0x02
#define INTR_RXMODEMSK 0x18
#define INTR_RXINT1ST  0x08
#define INTR_RXINTALL  0x10
#define W_IVEC    2
#define W_RXCTRL  3
#define RXCTRL_RXEN    0x01
#define W_TXCTRL1 4
#define TXCTRL1_PAREN  0x01
#define TXCTRL1_PAREV  0x02
#define TXCTRL1_1STOP  0x04
#define TXCTRL1_1HSTOP 0x08
#define TXCTRL1_2STOP  0x0c
#define TXCTRL1_STPMSK 0x0c
#define TXCTRL1_CLK1X  0x00
#define TXCTRL1_CLK16X 0x40
#define TXCTRL1_CLK32X 0x80
#define TXCTRL1_CLK64X 0xc0
#define TXCTRL1_CLKMSK 0xc0
#define W_TXCTRL2 5
#define TXCTRL2_TXEN   0x08
#define TXCTRL2_BITMSK 0x60
#define TXCTRL2_5BITS  0x00
#define TXCTRL2_7BITS  0x20
#define TXCTRL2_6BITS  0x40
#define TXCTRL2_8BITS  0x60
#define W_SYNC1   6
#define W_SYNC2   7
#define W_TXBUF   8
#define W_MINTR   9
#define MINTR_STATUSHI 0x10
#define MINTR_RST_MASK 0xc0
#define MINTR_RST_B    0x40
#define MINTR_RST_A    0x80
#define MINTR_RST_ALL  0xc0
#define W_MISC1  10
#define W_CLOCK  11
#define CLOCK_TRXC     0x08
#define W_BRGLO  12
#define W_BRGHI  13
#define W_MISC2  14
#define MISC2_PLLDIS   0x30
#define W_EXTINT 15
#define EXTINT_DCD     0x08
#define EXTINT_SYNCINT 0x10
#define EXTINT_CTSINT  0x20
#define EXTINT_TXUNDRN 0x40
#define EXTINT_BRKINT  0x80

#define R_STATUS  0
#define STATUS_RXAV    0x01
#define STATUS_ZERO    0x02
#define STATUS_TXEMPTY 0x04
#define STATUS_DCD     0x08
#define STATUS_SYNC    0x10
#define STATUS_CTS     0x20
#define STATUS_TXUNDRN 0x40
#define STATUS_BRK     0x80
#define R_SPEC    1
#define SPEC_ALLSENT   0x01
#define SPEC_BITS8     0x06
#define R_IVEC    2
#define IVEC_TXINTB    0x00
#define IVEC_LONOINT   0x06
#define IVEC_LORXINTA  0x0c
#define IVEC_LORXINTB  0x04
#define IVEC_LOTXINTA  0x08
#define IVEC_LOEXTINTB 0x02
#define IVEC_LOEXTINTA 0x0a
#define IVEC_HINOINT   0x60
#define IVEC_HIRXINTA  0x30
#define IVEC_HIRXINTB  0x20
#define IVEC_HITXINTA  0x10
#define IVEC_HIEXTINTB 0x40
#define IVEC_HIEXTINTA 0x50
#define R_INTR    3
#define INTR_EXTINTB   0x01
#define INTR_TXINTB    0x02
#define INTR_RXINTB    0x04
#define INTR_EXTINTA   0x08
#define INTR_TXINTA    0x10
#define INTR_RXINTA    0x20
#define R_IPEN    4
#define R_TXCTRL1 5
#define R_TXCTRL2 6
#define R_BC      7
#define R_RXBUF   8
#define R_RXCTRL  9
#define R_MISC   10
#define R_MISC1  11
#define R_BRGLO  12
#define R_BRGHI  13
#define R_MISC1I 14
#define R_EXTINT 15

static void z8530_reset_chn(ChannelState *s);
static void z8530_reset(void *opaque);

static int z8530_update_irq_chn(ChannelState *s)
{
    return 0;
}

static void z8530_update_irq(ChannelState *s)
{
    int irq;

    irq = z8530_update_irq_chn(s);
    irq |= z8530_update_irq_chn(s->otherchn);

    qemu_set_irq(s->irq, irq);
}

static void z8530_mem_write(void *opaque, hwaddr addr,
                           uint64_t val, unsigned size)
{
    Z8530State *state = opaque;
    ChannelState *s;
    uint32_t offset;
    int newreg;

    val &= 0xff;
    offset = addr;

    if (offset & 0x2) {
        s = &state->chn[chn_a];
    } else {
        s = &state->chn[chn_b];
    }
    qemu_log("z8530_mem_write %llx\n", (long long unsigned int)val);
    switch ((offset >> 2) & 1) {
    case SERIAL_CTRL:
        newreg = 0;
        switch (s->reg) {
            case W_CMD:
                newreg = val & CMD_PTR_MASK;
                val &= CMD_CMD_MASK;
                switch (val) {
                    case CMD_HI:
                        newreg |= CMD_HI;
                        break;
                    case CMD_CLR_TXINT:
                        break;
                    case CMD_CLR_IUS:
                        z8530_update_irq(s);
                        break;
                    case 0x10:
                        set_hw_irq(state->cpu, state->int_st, 0, 0x68 >> 2);
                        break;
                    default:
                        break;
                }
                break;
            case W_INTR ... W_RXCTRL:
            case W_SYNC1 ... W_TXBUF:
            case W_MISC1 ... W_CLOCK:
            case W_MISC2 ... W_EXTINT:
                s->wregs[s->reg] = val;
                break;
            case W_TXCTRL1:
            case W_TXCTRL2:
                s->wregs[s->reg] = val;
                break;
            case W_BRGLO:
            case W_BRGHI:
                s->wregs[s->reg] = val;
                s->rregs[s->reg] = val;
                break;
            case W_MINTR:
                switch (val & MINTR_RST_MASK) {
                case 0:
                default:
                    break;
                case MINTR_RST_B:
                    z8530_reset_chn(&state->chn[0]);
                    return;
                case MINTR_RST_A:
                    z8530_reset_chn(&state->chn[1]);
                    return;
                case MINTR_RST_ALL:
                    z8530_reset(DEVICE(state));
                    return;
                }
                break;
            default:
                break;
        }
        if (s->reg == 0)
            s->reg = newreg;
        else
            s->reg = 0;
        break;
    case SERIAL_DATA:
        break;
    }
}

static uint64_t z8530_mem_read(void *opaque, hwaddr addr,
                              unsigned size)
{
    Z8530State *state = opaque;
    ChannelState *s;
    uint32_t offset;
    uint32_t ret = 0;

    offset = addr - 0x1FFFF8;

    if (offset & 0x2) {
        s = &state->chn[chn_a];
    } else {
        s = &state->chn[chn_b];
    }
    
    switch ((offset >> 2) & 0x1) {
    case SERIAL_CTRL:
        ret = s->rregs[s->reg];
        qemu_log("z8530_mem_read : offset = %x, reg = %u, ret = %x\n", offset, s->reg, ret);
        s->reg = 0;
        break;

    case SERIAL_DATA:
        s->rregs[R_STATUS] &= ~STATUS_RXAV;
        break;
    }
    return ret;
}

uint8_t z8530_get_reg(Z8530State *s, uint8_t chn_id, uint8_t number) 
{
    return s->chn[chn_id].rregs[number];
}

void z8530_set_reg(Z8530State *s, uint8_t chn_id, uint8_t number, uint8_t value) 
{
    s->chn[chn_id].rregs[number] = value;
}

void mouse_interrupt(void * opaque, uint8_t chn_id)
{
    Z8530State *s = opaque;

    if (s->chn[chn_id].wregs[W_EXTINT] & EXTINT_DCD) {
        if (chn_id == chn_a) {
            s->chn[0].rregs[R_INTR] |= INTR_EXTINTA;
            if (s->chn[0].wregs[W_MINTR] & MINTR_STATUSHI)
                s->chn[1].rregs[R_IVEC] = IVEC_HIEXTINTA;
            else
                s->chn[1].rregs[R_IVEC] = IVEC_LOEXTINTA;
        } else {
            s->chn[0].rregs[R_INTR] |= INTR_EXTINTB;
            if (s->chn[0].wregs[W_MINTR] & MINTR_STATUSHI)
                s->chn[1].rregs[R_IVEC] = IVEC_HIEXTINTB;
            else
                s->chn[1].rregs[R_IVEC] = IVEC_LOEXTINTB;
        }
        set_hw_irq(s->cpu, s->int_st, 1, 0x68 >> 2);
    } else {
        set_hw_irq(s->cpu, s->int_st, 0, 0x68 >> 2);
    }
}

static void z8530_reset_chn(ChannelState *s)
{
    int i;

    s->reg = 0;
    for (i = 0; i < SERIAL_REGS; i++) {
        s->rregs[i] = 0;
        s->wregs[i] = 0;
    }
    s->wregs[W_TXCTRL1] = TXCTRL1_1STOP; // 1X divisor, 1 stop bit, no parity
    s->wregs[W_MINTR] = MINTR_RST_ALL;
    s->wregs[W_CLOCK] = CLOCK_TRXC; // Synch mode tx clock = TRxC
    s->wregs[W_MISC2] = MISC2_PLLDIS; // PLL disabled
    s->wregs[W_EXTINT] = EXTINT_DCD | EXTINT_SYNCINT | EXTINT_CTSINT |
        EXTINT_TXUNDRN | EXTINT_BRKINT; // Enable most interrupts
}

static void z8530_reset(void *opaque)
{
    Z8530State *s = opaque;

    z8530_reset_chn(&s->chn[0]);
    z8530_reset_chn(&s->chn[1]);
}

static const MemoryRegionOps z8530_mem_ops = {
    .read = z8530_mem_read,
    .write = z8530_mem_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

void *z8530_init(hwaddr base, via_state *via, int_state *int_st, M68kCPU *cpu)
{
    Z8530State *s;
    unsigned int i;

    s = (Z8530State *)g_malloc0(sizeof(Z8530State));

    s->cpu = cpu;

    for (i = 0; i < 2; i++) {
        s->chn[i].chn = 1 - i;
    }
    s->chn[0].otherchn = &s->chn[1];
    s->chn[1].otherchn = &s->chn[0];
    s->int_st = int_st;

    mouse_init(s, via);

    memory_region_init_io(&s->iomem, NULL, &z8530_mem_ops, s, "z8530", 0x400000);
    memory_region_add_subregion(get_system_memory(), base, &s->iomem);
    return 0;
}
