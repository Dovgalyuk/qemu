/*
 * Synertek SY6522 Versatile Interface Adapter (VIA)
 * IO mapping corresponds to Macintosh 128k
 * TODO: split VIA structure and IO between two different files
 *
 * Copyright (c) 2015 Pavel Dovgalyuk
 *
 * This code is licensed under the GPL
 */
#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qapi/error.h"
#include "cpu.h"
#include "hw/hw.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "exec/address-spaces.h"
#include "exec/exec-all.h"
#include "mac128k.h"
#include "sysemu/sysemu.h"
#include "hw/irq.h"
#include "sy6522.h"
#include "mac_rtc.h"
#include "mac_keyboard.h"
#include "mac_int_control.h"
#include "mac_sound_generator.h"

#define REGA_OVERLAY_MASK (1 << 4)
/* In Macintosh Plus 1ms is 780 VIA clock cycles 
#define F2_RATE 1282 */
/* Page 22 Volume III (or page 1025) of Inside_Macintosh */
#define F2_RATE 1277

typedef struct timer_state {
    qemu_irq irq;
    QEMUTimer *timer;
    bool prohibit_interrupt_flag; //0 - enabled, 1 - prohibited
} timer_state;

typedef struct via_state {
    M68kCPU *cpu;
    MemoryRegion iomem;
    MemoryRegion rom;
    MemoryRegion ram;
    /* base address */
    target_ulong base;
    /* registers */
    uint8_t regs[VIA_REGS];
    /* real time clock */
    rtc_state *rtc;
    /* vertical blanking interrupt */
    timer_state *vbi;
    /* timer 1 */
    timer_state *t1;
    /* timer 2 */
    timer_state *t2;
    /* timer 2 latch */
    uint8_t T2L_L;
    /* keyboard */
    keyboard_state *keyboard;
    int_state *int_st;
    /*sound generator*/
    sound_generator_state *snd_st;
} via_state;

const char *via_regs[VIA_REGS] = {"vBufB", "---", "vDirB", "vDirA", "vT1C", 
                                  "vT1CH", "vT1L", "vT1LH", "vT2C",
                                  "vT2CH", "vSR", "vACR", "vPCR",
                                  "vIFR", "vIER", "vBufA"};

static void via_set_reg_vBufB(via_state *s, uint8_t val);

static void timer1_interrupt(void * opaque)
{
    via_state *s = opaque;

    switch ((s->regs[vACR] && 0xc0) >> 6) {
        case 0:
            s->regs[vT1CH] = 0xff;
            s->regs[vT1C] = 0xff;
            if (!s->t1->prohibit_interrupt_flag) {
                qemu_log("via: T1 timer interrupt\n");
                qemu_irq_raise(s->t1->irq);
                s->t1->prohibit_interrupt_flag = 1;
            }
            break;
        case 1:
            s->regs[vT1CH] = s->regs[vT1LH];
            s->regs[vT1C] = s->regs[vT1L];
            qemu_log("via: T1 timer interrupt\n");
            qemu_irq_raise(s->t1->irq);
            break;
        case 2:
            s->regs[vT1CH] = 0xff;
            s->regs[vT1C] = 0xff;
            //PB7 should change state to high
            via_set_reg_vBufB(s, s->regs[vBufB] | 0x80);
            if (!s->t1->prohibit_interrupt_flag) {
                qemu_log("via: T1 timer interrupt\n");
                qemu_irq_raise(s->t1->irq);
                s->t1->prohibit_interrupt_flag = 1;
            }
            break;
        case 3:
            s->regs[vT1CH] = s->regs[vT1LH];
            s->regs[vT1C] = s->regs[vT1L];
            //PB7 should inverse its state
            via_set_reg_vBufB(s, s->regs[vBufB] ^ 0x80);
            qemu_log("via: T1 timer interrupt\n");
            qemu_irq_raise(s->t1->irq);
            break;
    }

    timer_mod_ns(s->t1->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + F2_RATE * (256 * s->regs[vT1CH] + s->regs[vT1C]));
}

static timer_state *timer1_init(via_state *via, qemu_irq irq)
{
    timer_state *s = (timer_state *)g_malloc0(sizeof(timer_state));
    
    s->irq = irq;
    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, timer1_interrupt, via);
    s->prohibit_interrupt_flag = 1;
    return s;
}

static void timer2_interrupt(void * opaque)
{
    via_state *s = opaque;

    s->regs[vT2CH] = 0xff;
    s->regs[vT2C] = 0xff;

    if (!s->t2->prohibit_interrupt_flag) {
        qemu_log("via: T2 timer interrupt\n");
        qemu_irq_raise(s->t2->irq);
        s->t2->prohibit_interrupt_flag = 1;
    }
    timer_mod_ns(s->t2->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + F2_RATE * (256 * s->regs[vT2CH] + s->regs[vT2C]));
}

static timer_state *timer2_init(via_state *via, qemu_irq irq)
{
    timer_state *s = (timer_state *)g_malloc0(sizeof(timer_state));
    
    s->irq = irq;
    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, timer2_interrupt, via);
    s->prohibit_interrupt_flag = 1;
    return s;
}

static void via_set_reg_vBufA(via_state *s, uint8_t val)
{
    uint8_t old = s->regs[vBufA];

    /* Switch vOverlay bit */
    if ((old & REGA_OVERLAY_MASK) != (val & REGA_OVERLAY_MASK)) {
        if (val & REGA_OVERLAY_MASK) {
            /* map ROM and RAM */
            memory_region_add_subregion_overlap(get_system_memory(),
                                                0x0, &s->rom, 1);
            memory_region_add_subregion_overlap(get_system_memory(),
                                                0x600000, &s->ram, 1);
            qemu_log("via: Map ROM at 0x0\n");
        } else {
            /* unmap ROM and RAM */
            memory_region_del_subregion(get_system_memory(), &s->rom);
            memory_region_del_subregion(get_system_memory(), &s->ram);
            qemu_log("via: Unmap ROM from 0x0\n");
        }
        tlb_flush(CPU(s->cpu));
    }

    if ((old & REGA_SEL_MASK) != (val & REGA_SEL_MASK)) {
        qemu_log("via: SEL bit set to %x\n", !!(val & REGA_SEL_MASK));
    }

    /*bit 0-2 sound generator volume*/
    if ((old & REGA_SNDVOL_MASK) != (val & REGA_SNDVOL_MASK)) {
        mac_sound_generator_set_volume(s->snd_st, val & REGA_SNDVOL_MASK);
    }

    /*bit 3 sound generator main/alternative buffer*/
    if ((old & REGA_SNDPG2_MASK) != (val & REGA_SNDPG2_MASK)) {
        mac_sound_generator_change_mem_buffer(s->snd_st, val & REGA_SNDPG2_MASK);
    }

    /* TODO: other bits */

    s->regs[vBufA] = val;
    qemu_log("via: vBufA set to 0x%x\n", s->regs[vBufA]);
}

static void via_set_reg_vDirB(via_state *s, uint8_t val)
{
    s->regs[vDirB] = val;
    qemu_log("via: vDirB set to 0x%x\n", s->regs[vDirB]);
}

static void via_set_reg_vBufB(via_state *s, uint8_t val)
{
    uint8_t old = s->regs[vBufB];
    if (!(val & REGB_RTCENB_MASK)) {
        if (!(old & REGB_RTCCLK_MASK) && (val & REGB_RTCCLK_MASK)
            && (s->regs[vDirB] & REGB_RTCDATA_MASK)) {
            rtc_receive(s->rtc, val);
        } else if ((old & REGB_RTCCLK_MASK) && !(val & REGB_RTCCLK_MASK)
                   && !(s->regs[vDirB] & REGB_RTCDATA_MASK)) {
            val = rtc_send(s->rtc, val);
        }
    } else if ((val & REGB_RTCENB_MASK) && !(s->regs[vBufB] & REGB_RTCENB_MASK)) {
        rtc_param_reset(s->rtc);
    }

    //bit 7 - sound generator enable/disable
    if ((old & REGB_SNDENB_MASK) != (val & REGB_SNDENB_MASK)) {
        mac_sound_generator_set_enable(s->snd_st, !(val & REGB_SNDENB_MASK));
    }

    /* TODO: other bits */

    s->regs[vBufB] = val;
    qemu_log("via: vBufB set to 0x%x\n", s->regs[vBufB]);
}

static void via_set_reg_vIFR(via_state *s, uint8_t val)
{
    if (val & 0x80) {
        s->regs[vIFR] |= val;
    } else {
        s->regs[vIFR] &= ~val;
    }
    s->regs[vIFR] = (!!(s->regs[vIER] & s->regs[vIFR] & 0x7f) << 7) | (s->regs[vIFR] & 0x7f);

    if (s->regs[vIFR] & 0x80) {
        set_hw_irq(s->cpu, s->int_st, 1, 0x64 >> 2);
    } else {
        set_hw_irq(s->cpu, s->int_st, 0, 0x64 >> 2);
    }

    qemu_log("via: vIFR set to 0x%x\n", s->regs[vIFR]);
}

static void via_set_reg_vIER(via_state *s, uint8_t val)
{
    if (val & 0x80) {
        s->regs[vIER] |= val;
    } else {
        s->regs[vIER] &= ~val;
    }

    s->regs[vIER] |= 0x80;

    qemu_log("via: vIER set to 0x%x\n", s->regs[vIER]);
}

static void via_set_reg_vT1C(via_state *s, uint8_t val)
{
    hw_error("TODO unknown. There is no mode which clearly writes value into low-byte counter. Page 6 of VIA documentation");
/*
    s->regs[vT1C] = val;
    qemu_log("via: vT1C set to 0x%x\n", s->regs[vT1C]);
    timer_mod_ns(s->t1->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + F2_RATE * (256 * s->regs[vT1CH] + s->regs[vT1C]));
*/
}

/* Made according to RS3 = L, RS2 = H, RS1 = L, RS0 = H. Page 6 of VIA documentation */
static void via_set_reg_vT1CH(via_state *s, uint8_t val)
{
    s->regs[vT1LH] = val;
    s->regs[vT1CH] = val;
    s->regs[vT1C] = s->regs[vT1L];
    s->t1->prohibit_interrupt_flag = 0;
    via_set_reg_vIFR(s, 0x40);
    if (s->regs[vACR] & 0x80) {
        via_set_reg_vBufB(s, s->regs[vBufB] & 0x7f);
    }
    qemu_log("via: vT1CH set to 0x%x\n", s->regs[vT1CH]);
    timer_mod_ns(s->t1->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + F2_RATE * (256 * s->regs[vT1CH] + s->regs[vT1C]));
}

static void via_set_reg_vT1L(via_state *s, uint8_t val)
{
    s->regs[vT1L] = val;
    qemu_log("via: vT1L set to 0x%x\n", s->regs[vT1L]);
}

static void via_set_reg_vT1LH(via_state *s, uint8_t val)
{
    s->regs[vT1LH] = val;
    s->t1->prohibit_interrupt_flag = 0;
    via_set_reg_vIFR(s, 0x40);
    qemu_log("via: vT1LH set to 0x%x\n", s->regs[vT1LH]);
}

static void via_set_reg_vT2C(via_state *s, uint8_t val)
{
    s->T2L_L = val;
    qemu_log("via: vT2C set to 0x%x\n", s->regs[vT2C]);
}

static void via_set_reg_vT2CH(via_state *s, uint8_t val)
{
    s->regs[vT2CH] = val;
    s->regs[vT2C] = s->T2L_L;
    s->t2->prohibit_interrupt_flag = 0;
    via_set_reg_vIFR(s, 0x20);
    qemu_log("via: ACR T2 mode: %x\n", !!(s->regs[vACR] & 0x20));
    qemu_log("via: vT2CH set to 0x%x\n", s->regs[vT2CH]);
    timer_mod_ns(s->t2->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + F2_RATE * (256 * s->regs[vT2CH] + s->regs[vT2C]));    
}

static void via_set_reg_vSR(via_state *s, uint8_t val)
{
    s->regs[vSR] = val;
    qemu_log("via: vSR set to 0x%x\n", s->regs[vSR]);
    /* bit 4 of ACR is SR input/output control */
    if (via_get_reg(s, vACR) & 0x10) {
        keyboard_handle_cmd(s->keyboard);
    }
}

static void via_writeb(void *opaque, hwaddr offset,
                              uint32_t value)
{
    via_state *s = (via_state *)opaque;
    offset = (offset - (s->base & ~TARGET_PAGE_MASK)) >> 9;
    if (offset >= VIA_REGS) {
        hw_error("Bad VIA write offset 0x%x", (int)offset);
    }
    qemu_log("via: write in %s 0x%x\n", via_regs[offset], value);
    via_set_reg(s, offset, value);
}

static uint32_t via_readb(void *opaque, hwaddr offset)
{
    via_state *s = (via_state *)opaque;
    uint32_t ret = 0;
    offset = (offset - (s->base & ~TARGET_PAGE_MASK)) >> 9;
    if (offset >= VIA_REGS) {
        hw_error("Bad VIA read offset 0x%x", (int)offset);
    }

    ret = via_get_reg(s, offset);
    qemu_log("via: read from %s 0x%x\n", via_regs[offset], ret);
    return ret;
}

static uint8_t via_get_reg_vT1C(via_state *via)
{
    uint64_t end, current, diff;
    via->t1->prohibit_interrupt_flag = 0;
    end = timer_expire_time_ns(via->t1->timer);
    current = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    diff = end - current;
    return (diff / F2_RATE) % 256;
}

static uint8_t via_get_reg_vT1CH(via_state *via)
{
    uint64_t end, current, diff;
    end = timer_expire_time_ns(via->t1->timer);
    current = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    diff = end - current;
    return (diff / F2_RATE) / 256;
}

static uint8_t via_get_reg_vT2C(via_state *via)
{
    uint64_t end, current, diff;
    via->t2->prohibit_interrupt_flag = 0;
    end = timer_expire_time_ns(via->t2->timer);
    current = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    diff = end - current;
    return (diff / F2_RATE) % 256;
}

static uint8_t via_get_reg_vT2CH(via_state *via)
{
    uint64_t end, current, diff;
    end = timer_expire_time_ns(via->t2->timer);
    current = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    diff = end - current;
    return (diff / F2_RATE) / 256;
}

uint8_t via_get_reg(via_state *s, uint8_t offset)
{
    uint8_t ret = s->regs[offset];
    if (offset == vSR) {
        via_set_reg_vIFR(s, 0x04);
    } else if (offset == vT1C) {
        ret = via_get_reg_vT1C(s);
        via_set_reg_vIFR(s, 0x40);
    } else if (offset == vT1CH) {
        ret = via_get_reg_vT1CH(s);
    } else if (offset == vT2C) {
        ret = via_get_reg_vT2C(s);
        via_set_reg_vIFR(s, 0x20);
    } else if (offset == vT2CH) {
        ret = via_get_reg_vT2CH(s);
    }

    return ret;
}

void via_set_reg(via_state *via, uint8_t offset, uint8_t value) 
{
    switch (offset) {
    case vBufA:
        via_set_reg_vBufA(via, value);
        break;
    case vBufB:
        via_set_reg_vBufB(via, value);
        break;
    case vDirB:
        via_set_reg_vDirB(via, value);
        break;
    case vSR:
        via_set_reg_vSR(via, value);
        break;
    case vIFR:
        via_set_reg_vIFR(via, value);
        break;
    case vIER:
        via_set_reg_vIER(via, value);
        break;
    case vT1C:
        via_set_reg_vT1C(via, value);
        break;
    case vT1CH:
        via_set_reg_vT1CH(via, value);
        break;
    case vT1L:
        via_set_reg_vT1L(via, value);
        break;    
    case vT1LH:
        via_set_reg_vT1LH(via, value);
        break;    
    case vT2C:
        via_set_reg_vT2C(via, value);
        break;
    case vT2CH:
        via_set_reg_vT2CH(via, value);
        break;
    default: 
        if (offset < VIA_REGS) {
            via->regs[offset] = value;
        }
        break;
    }
}

static const MemoryRegionOps via_ops = {
    .old_mmio = {
        .read = {
            via_readb,
            via_readb,
            via_readb,
        },
        .write = {
            via_writeb,
            via_writeb,
            via_writeb,
        },
    },
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void via_set_irq(void *opaque, int irq, int level)
{
    via_state *s = (via_state *)opaque;
    via_set_reg_vIFR(s, (0x01 << irq) | 0x80);
}

static void sy6522_reset(void *opaque)
{
    via_state *s = opaque;
    /* Init registers */
    via_set_reg_vBufA(s, REGA_OVERLAY_MASK);
    via_set_reg_vBufB(s, 0);
    via_set_reg_vDirB(s, 0);
    via_set_reg_vIFR(s, 0);
    via_set_reg_vIER(s, 0);
}

static void vbi_interrupt(void * opaque)
{
    timer_state *vbi = opaque;
    
    timer_mod_ns(vbi->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 16625800);
    qemu_irq_raise(vbi->irq);
}

static timer_state *vbi_init(qemu_irq irq)
{
    timer_state *s = (timer_state *)g_malloc0(sizeof(timer_state));
    
    s->irq = irq;
    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, vbi_interrupt, s);
    timer_mod_ns(s->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 16625800);
    return s;
}

via_state *sy6522_init(MemoryRegion *rom, MemoryRegion *ram,
                 uint32_t base, int_state *int_st, sound_generator_state *snd_st, M68kCPU *cpu)
{
    via_state *s;
    qemu_irq *pic;

    s = (via_state *)g_malloc0(sizeof(via_state));
    pic = qemu_allocate_irqs(via_set_irq, s, 8);

    s->base = base;
    s->cpu = cpu;
    memory_region_init_io(&s->iomem, NULL, &via_ops, s,
                          "sy6522 via", 0x2000);
    memory_region_add_subregion(get_system_memory(),
                                base & TARGET_PAGE_MASK, &s->iomem);
    /* TODO: Magic! */
    memory_region_init_alias(&s->rom, NULL, "ROM overlay", rom, 0x0, 0x10000);
    memory_region_set_readonly(&s->rom, true);
    memory_region_init_alias(&s->ram, NULL, "RAM overlay", ram, 0x0, 0x20000);

    s->rtc = rtc_init(pic[0]);
    s->vbi = vbi_init(pic[1]);
    s->keyboard = keyboard_init(s, pic[2]);
    s->t2 = timer2_init(s, pic[5]);
    s->t1 = timer1_init(s, pic[6]);

    s->int_st = int_st;
    s->snd_st = snd_st;

    qemu_register_reset(sy6522_reset, s);
    sy6522_reset(s);
    return s;
}
