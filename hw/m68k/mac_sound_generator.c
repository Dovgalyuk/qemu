#include "qemu/osdep.h"
#include "hw/hw.h"
#include "cpu.h"
#include "qemu/log.h"
#include "exec/address-spaces.h"
#include "audio/audio.h"
#include "exec/ram_addr.h"
#include "mac128k.h"
#include <math.h>

#include "mac_sound_generator.h"

#define BUF_SIZE 370

static const char *s_sg = "mac sound generator";

typedef struct sound_generator_state {
    MemoryRegion iomem;
    QEMUSoundCard card;
    SWVoiceOut *voice;
    uint32_t current_memory;
    uint16_t sample_buf[BUF_SIZE];
} sound_generator_state;

static inline void convert_mem_buf(sound_generator_state *s)
{
    uint32_t i;
    uint8_t *src;

    src = mac_get_ram_ptr() + s->current_memory;

    for (i = 0; i < BUF_SIZE; i++, src+=2) {
        s->sample_buf[i] = (uint16_t)(32768-32768*(*src)/255.0);
    }
}

static void mac_sound_generator_callback(void *opaque, int free)
{
    sound_generator_state *s = opaque;

    convert_mem_buf(s);
    AUD_write(s->voice, s->sample_buf, BUF_SIZE);
}

void mac_sound_generator_set_enable(sound_generator_state *s, int on)
{
    AUD_set_active_out(s->voice, on);
    if (on) {
        qemu_log("Sound generator enable\n");
    } else {
        qemu_log("Sound generator disable\n");
    } 
}

void mac_sound_generator_set_volume(sound_generator_state *s, uint8_t volume_level)
{
    AUD_set_volume_out (s->voice, 0, (uint8_t)(255*(8-volume_level)/8.0), (uint8_t)(255*(8-volume_level)/8.0));
    qemu_log("Sound generator volume %d\n",(uint8_t)(255*(8-volume_level)/8.0));
}

void mac_sound_generator_change_mem_buffer(sound_generator_state *s, int on)
{
    if (on) {
        s->current_memory = 0x1FD00;
    } else {
        s->current_memory = 0x1A100;
    }
    qemu_log("Sound generator memory buffer set to %x\n", s->current_memory);
}

static void sound_generator_reset(void *opaque)
{
    sound_generator_state *s = opaque;

    AUD_set_volume_out (s->voice, 0, 255, 255);
    AUD_set_active_out(s->voice, 0);
    s->current_memory = 0x1FD00;
}

sound_generator_state *mac_sound_generator_init(void)
{
    sound_generator_state *s = (sound_generator_state *)g_malloc0(sizeof(sound_generator_state));
    struct audsettings as = {44100, 1, AUD_FMT_U16, 0};

    AUD_register_card(s_sg, &s->card);

    s->voice = AUD_open_out(&s->card, s->voice, s_sg, s, mac_sound_generator_callback, &as);
    if (!s->voice) {
        AUD_log(s_sg, "Could not open voice\n");
    }

    qemu_register_reset(sound_generator_reset, s);
    sound_generator_reset(s);

    return s;
}
