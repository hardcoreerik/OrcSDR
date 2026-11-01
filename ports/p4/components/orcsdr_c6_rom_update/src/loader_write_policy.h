#pragma once

#include <stdint.h>

typedef struct {
    uint32_t offset;
    uint32_t size;
    uint32_t next_sequence;
    uint8_t active;
    uint8_t begun;
    uint8_t verified;
} orc_c6_loader_write_session_t;

void orc_c6_loader_guard_arm(uint32_t offset, uint32_t size);
void orc_c6_loader_guard_disarm(void);
int orc_c6_loader_allow_begin(uint32_t offset, uint32_t size,
                              uint32_t packets, uint32_t block);
int orc_c6_loader_allow_data(uint32_t sequence, uint32_t size);
int orc_c6_loader_allow_md5(uint32_t address, uint32_t size);
int orc_c6_loader_allow_end(uint32_t stay);
