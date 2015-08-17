#ifndef HW_MAC128K_H
#define HW_MAC128K_H

struct MemoryRegion;

/* iwm.c */
void iwm_init(MemoryRegion *sysmem, uint32_t base, M68kCPU *cpu);

/* sy6522.c */
void sy6522_init(MemoryRegion *rom, MemoryRegion *ram, uint32_t base, M68kCPU *cpu);

void keyboard_init(void);

//void via_set_reg_vSR(via_state *s, uint8_t val);

#endif
