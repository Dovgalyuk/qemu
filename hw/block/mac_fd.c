#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qapi/error.h"
#include "cpu.h"
#include "hw/hw.h"
#include "mac_fd.h"
#include "hw/m68k/mac128k.h"
#include "sysemu/block-backend.h"
#include "hw/qdev.h"
#include "qemu/log.h"

#define TYPE_MAC_FD "mac_fd"

#define MAC_FD(obj) OBJECT_CHECK(MAC_FDFlashState, (obj), TYPE_MAC_FD)


typedef struct MAC_FDFlashState MAC_FDFlashState;
struct MAC_FDFlashState {
    DeviceState parent_obj;
    BlockBackend *blk;
};

MAC_FDFlashState *MAC_FDState = NULL;

void mac_fd_read(int blk_offset, int buf_offset, int blk_count)
{
    void *ptr = mac_get_ram_ptr();
    if (MAC_FDState->blk) {
        if (blk_pread(MAC_FDState->blk, blk_offset, ptr + buf_offset, blk_count) < 0) {
            qemu_log("Error: mac_fd read error\n");
            exit(-1);
        }
    }
    qemu_log("mac_fd read: blk_offset = 0x%x, ram_offset = 0x%x, blk_count = 0x%x\n", blk_offset, buf_offset, blk_count);
}

void mac_fd_write(int blk_offset, int buf_offset, int blk_count)
{
    void *ptr = mac_get_ram_ptr();
    if (MAC_FDState->blk) {
        if (blk_pwrite(MAC_FDState->blk, blk_offset, ptr + buf_offset, blk_count, 0) < 0) {
            qemu_log("Error: mac_fd write error\n");
            exit(-1);
        }
    }
    qemu_log("mac_fd write: blk_offset = 0x%x, ram_offset = 0x%x, blk_count = 0x%x\n", blk_offset, buf_offset, blk_count);
}

static void mac_fd_reset(DeviceState *dev)
{

}

static const VMStateDescription vmstate_mac_fd = {
    .name = "mac_fd",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        /* XXX: do we want to save s->storage too? */
        VMSTATE_END_OF_LIST()
    }
};

static void mac_fd_realize(DeviceState *dev, Error **errp)
{
    MAC_FDFlashState *s = MAC_FD(dev);

    if (s->blk) {
        if (blk_is_read_only(s->blk)) {
            error_setg(errp, "Can't use a read-only drive");
            return;
        }
    }
    if (MAC_FDState != NULL) {
        exit(-1);
    }
    MAC_FDState = s;
}

static Property mac_fd_properties[] = {
    DEFINE_PROP_DRIVE("drive", MAC_FDFlashState, blk),
    DEFINE_PROP_END_OF_LIST(),
};

static void mac_fd_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = mac_fd_realize;
    dc->reset = mac_fd_reset;
    dc->vmsd = &vmstate_mac_fd;
    dc->props = mac_fd_properties;
}

static const TypeInfo mac_fd_info = {
    .name          = TYPE_MAC_FD,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(MAC_FDFlashState),
    .class_init    = mac_fd_class_init,
};

static void mac_fd_register_types(void)
{
    type_register_static(&mac_fd_info);
}

type_init(mac_fd_register_types)
