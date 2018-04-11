#ifndef HW_MAC128K_H
#define HW_MAC128K_H

typedef struct via_state via_state;
typedef struct int_state int_state;
typedef struct sound_generator_state sound_generator_state;

struct MemoryRegion;

uint8_t *mac_get_ram_ptr(void);

/* sy6522.c */
via_state *sy6522_init(MemoryRegion *rom, MemoryRegion *ram, uint32_t base, int_state *int_st, sound_generator_state *snd_st, M68kCPU *cpu);
/* iwm.c */
void iwm_init(MemoryRegion *sysmem, uint32_t base, M68kCPU *cpu, via_state *via);
/* z8530.c */
void *z8530_init(hwaddr base, via_state *via, int_state *int_st, M68kCPU *cpu);

#endif
