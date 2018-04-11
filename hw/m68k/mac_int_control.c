#include "qemu/osdep.h"
#include "hw/hw.h"
#include "cpu.h"
#include "qemu/log.h"
#include "mac_int_control.h"

typedef struct int_state {
	uint8_t level;
	uint8_t vector;
} int_state;

void set_hw_irq(void *cpu, void *int_st, bool bit, uint8_t vector)
{
    int_state *s = (int_state *)int_st;
    
    qemu_log("IRQ bit %x, vector %x\n", bit, vector);
    /*Set the bit on requred position = 0 and then assign it to required value
    Position is (vector - (0x64 >> 2)). It is offset from level 1 interrupt*/
    s->level &= ~(1 << (vector - 0x19));
    s->level |= (bit << (vector - 0x19));
    s->vector = 0x18 + s->level;
    qemu_log("IRQ level %x, vector %x\n", s->level, s->vector);

    /*This code needed because of some error in interrupt system in other files.
    In case of level 3 interrupt the bits of SCC and VIA interrupts 
    never became equal 0 again. So, we artificially do SCC interrupt, the bit 
    then changes to 0 and VIA interrupt proceeds.*/
    if (s->level == 3) {
        m68k_set_irq_level(cpu, 2, 0x68 >> 2);
    } else {
        m68k_set_irq_level(cpu, s->level, s->vector);
    }
}

int_state *int_init(void)
{
    int_state *s;

    s = (int_state *)g_malloc0(sizeof(int_state));

    s->level = 0;
    s->vector = 0;

    return s;
}
