#include "qemu/osdep.h"
#include "hw/irq.h"
#include "sysemu/sysemu.h"
#include "qemu/log.h"
#include "sy6522.h"
#include "mac_rtc.h"

#define HOST_TO_MAC_RTC (66 * 365 + 17) * 24 * 3600

#define RTC_BUF_MASK 0x03
#define RTCRWBIT_MASK (1 << 7)

typedef struct rtc_state {
    qemu_irq irq;
    uint8_t count;
    uint8_t rw_flag;
    uint8_t cmd;
    uint8_t param;
    uint8_t buf_RAM[32];
    QEMUTimer *timer;
} rtc_state;

const char *rtc_regs[32] = {"sec_reg1", "sec_reg2", "sec_reg3", "sec_reg4", 
                                "------", "------", "------", "------",
                                "RAM_10", "RAM_11", "RAM_12", "RAM_13",
                                "test_reg", "wp_reg", "------", "------", 
                                "RAM_00", "RAM_01", "RAM_02", "RAM_03",
                                "RAM_04", "RAM_05", "RAM_06", "RAM_07",
                                "RAM_08", "RAM_09", "RAM_0A", "RAM_0B",
                                "RAM_0C", "RAM_0D", "RAM_0E", "RAM_0F"};


static void rtc_write_handler(rtc_state *rtc)
{
    if (rtc->cmd == 0x35) {
        rtc->buf_RAM[13] = rtc->param;
        qemu_log("rtc: write to wpReg 0x%x\n", rtc->param);
    } else if (!(rtc->buf_RAM[13] & 0x80)) {
        if ((rtc->cmd & RTC_BUF_MASK) == 0x01)  {
            rtc->buf_RAM[(rtc->cmd & ~RTC_BUF_MASK & 0x7F) >> 2] = rtc->param;
            qemu_log("rtc: write to %s: 0x%x\n", 
                   rtc_regs[(rtc->cmd & ~RTC_BUF_MASK & 0x7F) >> 2], rtc->param);
        } else {
            qemu_log("rtc: write error: unknown command 0x%x\n", rtc->cmd);
        }
    } else {
        qemu_log("rtc: write error: write-protect enabled\n");
    }
}

static void rtc_read_handler(rtc_state *rtc)
{
    if (rtc->cmd == 0x35) {
        qemu_log("rtc: read error: wp_reg\n");
    } else if (rtc->cmd == 0x31) {
        qemu_log("rtc: read error: test_reg\n");
    } else if ((rtc->cmd & RTC_BUF_MASK) == 0x01)  {
        rtc->param = rtc->buf_RAM[(rtc->cmd & ~RTC_BUF_MASK & 0x7F) >> 2];
        qemu_log("rtc: read to %s: 0x%x\n",
               rtc_regs[(rtc->cmd & ~RTC_BUF_MASK & 0x7F) >> 2], rtc->param );
    } else {
        qemu_log("rtc: read error: unknown command 0x%x\n", rtc->cmd);
    }
}

void rtc_receive(rtc_state *rtc, uint8_t val)
{
    if (!(rtc->rw_flag)) {
        rtc->cmd |= (val & REGB_RTCDATA_MASK) << (7 - rtc->count);
    } else {
        rtc->param |= (val & REGB_RTCDATA_MASK) << (7 - rtc->count);
    }
    rtc->count++;
    if (rtc->count == 8) {
        if (!(rtc->cmd & RTCRWBIT_MASK) && !(rtc->rw_flag)) {
            rtc->rw_flag = 1;
            rtc->count = 0;
        } else if (rtc->rw_flag) {
            rtc_write_handler(rtc);
            rtc_param_reset(rtc);
        } else {
            rtc_read_handler(rtc);
            rtc->count = 0;
        }
    }
}

uint8_t rtc_send(rtc_state *rtc, uint8_t val)
{
    val &= ~REGB_RTCDATA_MASK;
    val |= (rtc->param >> (7 - rtc->count)) & REGB_RTCDATA_MASK;
    rtc->count++;
    if (rtc->count == 8) {
        rtc_param_reset(rtc);
    }
    return val;
}

void rtc_param_reset(rtc_state *rtc)
{
    rtc->param = 0;
    rtc->cmd = 0;
    rtc->rw_flag = 0;
    rtc->count = 0;
}

static void rtc_interrupt(void * opaque)
{
    rtc_state *rtc = opaque;
    timer_mod_ns(rtc->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL)
             + NANOSECONDS_PER_SECOND);

    if (rtc->buf_RAM[0] == 0xFF) {
        if (rtc->buf_RAM[1] == 0xFF) {
            if (rtc->buf_RAM[2] == 0xFF) {
                rtc->buf_RAM[3]++;
            }
            rtc->buf_RAM[2]++;
        }
        rtc->buf_RAM[1]++;
    }
    rtc->buf_RAM[0]++;

    qemu_irq_raise(rtc->irq);
}

static void rtc_reset(rtc_state *rtc)
{
    uint64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL)
                        / NANOSECONDS_PER_SECOND
                 + HOST_TO_MAC_RTC;
    uint8_t i;
    for (i = 0; i < 4; ++i) {
        rtc->buf_RAM[i] = (now >> (8 * i)) & 0xFF;
    }
    rtc->buf_RAM[13] = 0x80;
    timer_mod_ns(rtc->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL)
             + NANOSECONDS_PER_SECOND);
}

rtc_state *rtc_init(qemu_irq irq)
{
    rtc_state *s = (rtc_state *)g_malloc0(sizeof(rtc_state));
    s->irq = irq;
    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, rtc_interrupt, s);
    rtc_reset(s);

    return s;
}