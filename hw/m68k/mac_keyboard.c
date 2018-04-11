#include "qemu/osdep.h"
#include "qemu-common.h"
#include "hw/hw.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "exec/address-spaces.h"
#include "hw/irq.h"
#include "ui/input.h"
#include "sy6522.h"
#include "mac_keyboard.h"

typedef struct keyboard_state {
    qemu_irq irq;
    uint8_t cmd;
    uint8_t model_number_flag;
    QEMUTimer *timer;
    via_state *via;
} keyboard_state;

static const unsigned char macintosh128k_raw_keycode[128] = {
   0,    0, 0x25, 0x27, 0x29, 0x2b, 0x2f, 0x2d, 0x35, 0x39, 0x33, 0x3b, 0x37, 0x31, 0x67, 0x61,
0x19, 0x1b, 0x1d, 0x1f, 0x23, 0x21, 0x41, 0x45, 0x3f, 0x47, 0x43, 0x3d, 0x49, 0x75,  0x1,  0x3,
 0x5,  0x7,  0xb,  0x9, 0x4d, 0x51, 0x4b, 0x53, 0x4f, 0x65, 0x71, 0x55,  0xd,  0xf, 0x11, 0x13,
0x17, 0x5b, 0x5d, 0x57, 0x5f, 0x59, 0x71,    0, 0x6f, 0x63, 0x73,    0,    0,    0,    0,    0,
   0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
   0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
   0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
   0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
};

static const uint8_t NullKeycode = 0x7b;

static void keyboard_event(DeviceState *dev, QemuConsole *src, InputEvent *evt);

static QemuInputHandler keyboard_handler = {
    .name  = "QEMU Macintosh 128K Keyboard",
    .mask  = INPUT_EVENT_MASK_KEY,
    .event = keyboard_event,
};


static void put_value_vSR(void *opaque, uint8_t value)
{
    keyboard_state *s = (keyboard_state *)opaque;
    qemu_log("Sending byte from keyboard to SR %x\n", value);
    s->cmd = 0;
    via_set_reg(s->via, vSR, value);
    qemu_irq_raise(s->irq);
}


static void keyboard_event(DeviceState *dev, QemuConsole *src,
                               InputEvent *evt)
{
    keyboard_state *s = (keyboard_state *)dev;
    int scancodes[3], count;
    InputKeyEvent *key = evt->u.key.data;

    count = qemu_input_key_value_to_scancode(key->key,
                                             key->down,
                                             scancodes);
    timer_del(s->timer);
    if (s->cmd == 0x10 || s->cmd == 0x14) {
        if (count == 3) {
            put_value_vSR(s, NullKeycode);
        } else {
            uint8_t keycode_mac;
            keycode_mac = macintosh128k_raw_keycode[scancodes[count - 1] & 0x7f];
            keycode_mac = keycode_mac | (scancodes[count - 1] & 0x80);
            put_value_vSR(s, keycode_mac);
        }
    }
}

static void keyboard_reset(void *opaque)
{
    keyboard_state *kbd_state = (keyboard_state *)opaque;
    kbd_state->cmd = 0;
    kbd_state->model_number_flag = 0;
}

static void timer_callback(void *opaque)
{
    put_value_vSR(opaque, NullKeycode);
}

void keyboard_handle_cmd(keyboard_state *s)
{
    s->cmd = via_get_reg(s->via, vSR);
    qemu_log("Keyboard receives command %x\n", s->cmd);
    switch (s->cmd) {
        case 0x10:
            /* Inquiry */
            if (s->model_number_flag) {
                timer_mod_ns(s->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL)
                    + NANOSECONDS_PER_SECOND / 4);
            }
            break;
        case 0x14:
            /* Instant */
            hw_error("Command Instant is unavailable for this keyboard. Somebody need to fix that.\n");
            break;
        case 0x16:
            keyboard_reset(s);
            s->model_number_flag = 1;
            put_value_vSR(s, 0x0f);
            break;
        case 0x36:
            if (s->model_number_flag) {
                put_value_vSR(s, 0x7d);
            }
            break;
    }
}

keyboard_state *keyboard_init(via_state *via, qemu_irq irq)
{
    keyboard_state *s = (keyboard_state *)g_malloc0(sizeof(keyboard_state));
    
    s->via = via;
    qemu_input_handler_register((DeviceState *)s,
                                &keyboard_handler);
    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, timer_callback, s);
    s->irq = irq;
    qemu_register_reset(keyboard_reset, s);
    keyboard_reset(s);

    return s;
}