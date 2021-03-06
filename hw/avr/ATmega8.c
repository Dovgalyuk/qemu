/*
 * QEMU AVR CPU
 *
 * Copyright (c) 2016 Michael Rolnik
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * <http://www.gnu.org/licenses/lgpl-2.1.html>
 */

/*
 *  NOTE:
 *      This is not a real AVR board !!! This is an example !!!
 *
 *        This example can be used to build a real AVR board.
 *
 *      This example board loads provided binary file into flash memory and
 *      executes it from 0x00000000 address in the code memory space.
 *
 *      Currently used for AVR CPU validation
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "cpu.h"
#include "hw/hw.h"
#include "sysemu/sysemu.h"
#include "sysemu/qtest.h"
#include "ui/console.h"
#include "hw/boards.h"
#include "hw/devices.h"
#include "hw/loader.h"
#include "qemu/error-report.h"
#include "exec/address-spaces.h"
#include "include/hw/sysbus.h"
#include "hw/avr/atmega8_soc.h"

#define VIRT_BASE_FLASH 0x00000000
#define VIRT_BASE_ISRAM 0x00000100
#define VIRT_BASE_EXMEM 0x00001100
#define VIRT_BASE_EEPROM 0x00000000

#define SIZE_FLASH 0x00020000
#define SIZE_ISRAM 0x00001000
#define SIZE_EXMEM 0x00010000
#define SIZE_EEPROM 0x00001000
#define SIZE_IOREG SIZE_REGS

#define PHYS_BASE_FLASH (PHYS_BASE_CODE)

#define PHYS_BASE_ISRAM (PHYS_BASE_DATA)
#define PHYS_BASE_EXMEM (PHYS_BASE_ISRAM + SIZE_ISRAM)
#define PHYS_BASE_EEPROM (PHYS_BASE_EXMEM + SIZE_EXMEM)

#define PHYS_BASE_IOREG (PHYS_BASE_REGS + 0x20)



static void Atmega8_init(MachineState *machine)
{
	
    DeviceState *dev;
    const char *firmware = NULL;
    const char *filename;
    dev = qdev_create(NULL, TYPE_Atmega8_SOC);
    qdev_prop_set_string(dev, "cpu-type", AVR_CPU_TYPE_NAME("atmega8"));
    object_property_set_bool(OBJECT(dev), true, "realized", &error_fatal);

    if (machine->firmware) {
        firmware = machine->firmware;
    }

    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, firmware);
    if (filename) {
        load_image_targphys(filename, PHYS_BASE_FLASH, SIZE_FLASH);
    }
}

static void Atmega8_machine_init(MachineClass *mc)
{
    mc->desc = "Atmega8";
    mc->init = Atmega8_init;
}

DEFINE_MACHINE("Atmega8", Atmega8_machine_init)
