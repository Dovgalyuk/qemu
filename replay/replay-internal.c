/*
 * replay-internal.c
 *
 * Copyright (c) 2010-2015 Institute for System Programming
 *                         of the Russian Academy of Sciences.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu-common.h"
#include "replay-internal.h"

unsigned int replay_data_kind = -1;
static unsigned int replay_has_unread_data;

/* Mutex to protect reading and writing events to the log.
   replay_data_kind and replay_has_unread_data are also protected
   by this mutex. 
   It also protects replay events queue which stores events to be
   written or read to the log. */
static QemuMutex lock;

/* File for replay writing */
FILE *replay_file;

void replay_put_byte(uint8_t byte)
{
    if (replay_file) {
        putc(byte, replay_file);
    }
}

void replay_put_event(uint8_t event)
{
    replay_put_byte(event);
}


void replay_put_word(uint16_t word)
{
    replay_put_byte(word >> 8);
    replay_put_byte(word);
}

void replay_put_dword(uint32_t dword)
{
    replay_put_word(dword >> 16);
    replay_put_word(dword);
}

void replay_put_qword(int64_t qword)
{
    replay_put_dword(qword >> 32);
    replay_put_dword(qword);
}

void replay_put_array(const uint8_t *buf, size_t size)
{
    if (replay_file) {
        replay_put_dword(size);
        fwrite(buf, 1, size, replay_file);
    }
}

uint8_t replay_get_byte(void)
{
    uint8_t byte = 0;
    if (replay_file) {
        byte = getc(replay_file);
    }
    return byte;
}

uint16_t replay_get_word(void)
{
    uint16_t word = 0;
    if (replay_file) {
        word = replay_get_byte();
        word = (word << 8) + replay_get_byte();
    }

    return word;
}

uint32_t replay_get_dword(void)
{
    uint32_t dword = 0;
    if (replay_file) {
        dword = replay_get_word();
        dword = (dword << 16) + replay_get_word();
    }

    return dword;
}

int64_t replay_get_qword(void)
{
    int64_t qword = 0;
    if (replay_file) {
        qword = replay_get_dword();
        qword = (qword << 32) + replay_get_dword();
    }

    return qword;
}

void replay_get_array(uint8_t *buf, size_t *size)
{
    if (replay_file) {
        *size = replay_get_dword();
        fread(buf, 1, *size, replay_file);
    }
}

void replay_get_array_alloc(uint8_t **buf, size_t *size)
{
    if (replay_file) {
        *size = replay_get_dword();
        *buf = g_malloc(*size);
        fread(*buf, 1, *size, replay_file);
    }
}

void replay_check_error(void)
{
    if (replay_file) {
        if (feof(replay_file)) {
            error_report("replay file is over");
            qemu_system_vmstop_request_prepare();
            qemu_system_vmstop_request(RUN_STATE_PAUSED);
        } else if (ferror(replay_file)) {
            error_report("replay file is over or something goes wrong");
            qemu_system_vmstop_request_prepare();
            qemu_system_vmstop_request(RUN_STATE_INTERNAL_ERROR);
        }
    }
}

void replay_fetch_data_kind(void)
{
    if (replay_file) {
        if (!replay_has_unread_data) {
            replay_data_kind = replay_get_byte();
            replay_check_error();
            replay_has_unread_data = 1;
        }
    }
}

void replay_finish_event(void)
{
    replay_has_unread_data = 0;
    replay_fetch_data_kind();
}

void replay_mutex_init(void)
{
    qemu_mutex_init(&lock);
}

void replay_mutex_destroy(void)
{
    qemu_mutex_destroy(&lock);
}

void replay_mutex_lock(void)
{
    qemu_mutex_lock(&lock);
}

void replay_mutex_unlock(void)
{
    qemu_mutex_unlock(&lock);
}
