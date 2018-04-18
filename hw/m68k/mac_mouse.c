#include "qemu/osdep.h"
#include "hw/hw.h"
#include "exec/address-spaces.h"
#include "hw/irq.h"
#include "ui/input.h"
#include "qemu/log.h"
#include "qemu/timer.h"
#include "sy6522.h"
#include "z8530.h"
#include "mac_mouse.h"

#define FREQUENCY 300000LL
#define MOUSE_LIMIT 3

typedef struct mouse_state {
    QEMUTimer *timer;
    via_state *via;
    Z8530State *z8530;
    int32_t dx, curr_dx;
    int32_t dy, curr_dy;
} mouse_state;

static void mac_mouse_event(DeviceState *dev, QemuConsole *src, InputEvent *evt);

static QemuInputHandler mouse_handler = {
    .name  = "QEMU Macintosh 128K mouse",
    .mask  = INPUT_EVENT_MASK_BTN | INPUT_EVENT_MASK_REL,
    .event = mac_mouse_event,
};

static void mac_mouse_event(DeviceState *dev, QemuConsole *src,
                               InputEvent *evt)
{
    mouse_state *s = (mouse_state *)dev;
    InputBtnEvent *btn;
    InputMoveEvent *move;

    switch (evt->type) {
    case INPUT_EVENT_KIND_REL:
        move = evt->u.rel.data;
        if (move->axis == INPUT_AXIS_X) {
            if (move->value == 0) {
                break;
            }
            s->dx += move->value;                     
            if (abs(s->dx - s->curr_dx) > MOUSE_LIMIT-1) {
                timer_mod_ns(s->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL)
                     + FREQUENCY);                
            }
        } else if (move->axis == INPUT_AXIS_Y) {
            if (move->value == 0) {
                break;
            }
            s->dy -= move->value;            
            if (abs(s->dy - s->curr_dy) > MOUSE_LIMIT-1) {
                timer_mod_ns(s->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL)
                    + FREQUENCY);
            }        
        }
        break;

    case INPUT_EVENT_KIND_BTN:
        btn = evt->u.btn.data;
        if (btn->down) {
            qemu_log("mouse: button down\n");
            via_set_reg(s->via, vBufB, via_get_reg(s->via, vBufB) & 0xf7);
        } else {
            qemu_log("mouse: button up\n");
            via_set_reg(s->via, vBufB, via_get_reg(s->via, vBufB) | 0x08);
        }
        break;

    default:
        /* keep gcc happy */
        break;
    }
}

static void timer_callback(void *opaque)
{
    mouse_state *s = opaque;
    uint8_t dcd;

    if (abs(s->dx - s->curr_dx) < MOUSE_LIMIT) {
        s->dx = s->curr_dx = 0;
    }
    if (abs(s->dy - s->curr_dy) < MOUSE_LIMIT) {
        s->dy = s->curr_dy = 0;
    }

    if (abs(s->dx - s->curr_dx) > MOUSE_LIMIT - 1
        && abs(s->dy * s->curr_dx) <= abs(s->dx * s->curr_dy)) {
        dcd = z8530_get_reg(s->z8530, 0, 0);
        z8530_set_reg(s->z8530, 0, 0, dcd ^ 0x08);
        if (s->dx - s->curr_dx > 0) {
            if ((dcd & 0x08) == 0) {
                via_set_reg(s->via, vBufB, via_get_reg(s->via, vBufB) & 0xef);
            } else {
                via_set_reg(s->via, vBufB, via_get_reg(s->via, vBufB) | 0x10);
            }
            s->curr_dx += MOUSE_LIMIT;
        } else {
            if ((dcd & 0x08) == 0) {
                via_set_reg(s->via, vBufB, via_get_reg(s->via, vBufB) | 0x10);
            } else {
                via_set_reg(s->via, vBufB, via_get_reg(s->via, vBufB) & 0xef);
            }
            s->curr_dx -= MOUSE_LIMIT;
        } 
        mouse_interrupt(s->z8530, 0);
    } else if (abs(s->dy - s->curr_dy) > MOUSE_LIMIT-1) {
        dcd = z8530_get_reg(s->z8530, 1, 0);
        z8530_set_reg(s->z8530, 1, 0, dcd ^ 0x08);
        if (s->dy - s->curr_dy > 0) {
            if ((dcd & 0x08) == 0) {
                via_set_reg(s->via, vBufB, via_get_reg(s->via, vBufB) & 0xdf);
            } else {
                via_set_reg(s->via, vBufB, via_get_reg(s->via, vBufB) | 0x20);
            }
            s->curr_dy += MOUSE_LIMIT;
        } else {
            if ((dcd & 0x08) == 0) {
                via_set_reg(s->via, vBufB, via_get_reg(s->via, vBufB) | 0x20);
            } else {
                via_set_reg(s->via, vBufB, via_get_reg(s->via, vBufB) & 0xdf);
            }
            s->curr_dy -= MOUSE_LIMIT;
        } 
        mouse_interrupt(s->z8530, 1);
    }

    if ((abs(s->dx - s->curr_dx) > MOUSE_LIMIT-1)
        || (abs(s->dy - s->curr_dy) > MOUSE_LIMIT-1)) {
        timer_mod_ns(s->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + FREQUENCY);
    }
}

mouse_state *mouse_init(Z8530State *z8530, via_state *via)
{
    mouse_state *s = (mouse_state *)g_malloc0(sizeof(mouse_state));
    
    s->via = via;
    s->z8530 = z8530;
    qemu_input_handler_register((DeviceState *)s,
                                &mouse_handler);
    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, timer_callback, s);

    return s;
}
