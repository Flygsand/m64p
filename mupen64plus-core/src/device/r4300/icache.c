/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - icache.c                                                *
 *   Mupen64Plus homepage: https://mupen64plus.org/                        *
 *   Copyright (C) 2022 loganmc10                                          *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.          *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include <string.h>

#include "device/r4300/r4300_core.h"
#include "device/r4300/cached_interp.h"
#include "device/memory/memory.h"
#include "device/rcp/rsp/rsp_core.h"

void poweron_icache(struct instcache *lines)
{
    memset(lines, 0, 512 * sizeof(lines[0]));
    for (uint32_t i = 0; i < 512; ++i)
        lines[i].index = i << 5 & UINT32_C(0xFE0);
}

void icache_writeback(struct r4300_core* r4300, struct instcache *line)
{
    cp0_dcb_interlock(r4300, 48);
    uint32_t cache_address = line->tag | line->index;
    invalidate_r4300_cached_code(r4300, cache_address, 32);
    invalidate_r4300_cached_code(r4300, cache_address ^ UINT32_C(0x20000000), 32);
    cache_address &= UINT32_C(0x1ffffffc);
    const struct mem_handler* handler = mem_get_handler(r4300->mem, cache_address);
    mem_write32(handler, cache_address | UINT32_C(0x0), line->words[0], ~UINT32_C(0));
    mem_write32(handler, cache_address | UINT32_C(0x4), line->words[1], ~UINT32_C(0));
    mem_write32(handler, cache_address | UINT32_C(0x8), line->words[2], ~UINT32_C(0));
    mem_write32(handler, cache_address | UINT32_C(0xC), line->words[3], ~UINT32_C(0));
    mem_write32(handler, cache_address | UINT32_C(0x10), line->words[4], ~UINT32_C(0));
    mem_write32(handler, cache_address | UINT32_C(0x14), line->words[5], ~UINT32_C(0));
    mem_write32(handler, cache_address | UINT32_C(0x18), line->words[6], ~UINT32_C(0));
    mem_write32(handler, cache_address | UINT32_C(0x1C), line->words[7], ~UINT32_C(0));
}

void icache_fill(struct instcache *line, struct r4300_core* r4300, uint32_t address)
{
    cp0_icb_interlock(r4300, 15);
    line->valid = 1;
    line->tag = address & ~UINT32_C(0xFFF);
    uint32_t cache_address = line->tag | line->index;
    cache_address &= UINT32_C(0x1ffffffc);
    const struct mem_handler* handler = mem_get_handler(r4300->mem, cache_address);
    mem_read32(handler, cache_address | UINT32_C(0x0), &line->words[0]);
    mem_read32(handler, cache_address | UINT32_C(0x4), &line->words[1]);
    mem_read32(handler, cache_address | UINT32_C(0x8), &line->words[2]);
    mem_read32(handler, cache_address | UINT32_C(0xC), &line->words[3]);
    mem_read32(handler, cache_address | UINT32_C(0x10), &line->words[4]);
    mem_read32(handler, cache_address | UINT32_C(0x14), &line->words[5]);
    mem_read32(handler, cache_address | UINT32_C(0x18), &line->words[6]);
    mem_read32(handler, cache_address | UINT32_C(0x1C), &line->words[7]);
}

// This code is performance critical, so for the cached interpreter, we used a version that doesn't need to return anything in order to save some CPU
void icache_step(struct r4300_core* r4300)
{
    if ((*r4300_pc_struct(r4300))->ops == cached_interp_FIN_BLOCK || \
        (*r4300_pc_struct(r4300))->ops == cached_interp_NOTCOMPILED || \
        (*r4300_pc_struct(r4300))->ops == cached_interp_NOTCOMPILED2)
        return;
    do_SP_Task(r4300->sp);
    cp0_base_cycle(r4300);
    uint32_t address = (*r4300_pc_struct(r4300))->phys_addr;
    if ((*r4300_pc_struct(r4300))->cached)
    {
        struct instcache *line = &r4300->icache[(address >> 5) & UINT32_C(0x1FF)];
        if(!icache_hit(line, address))
            icache_fill(line, r4300, address);
    }
    else
    {
        address &= UINT32_C(0x1ffffffc);
        mem_read32(mem_get_handler(r4300->mem, address), address, &(uint32_t){0}); // Done in order to get correct cycle count
    }
}

uint32_t* icache_fetch(struct r4300_core* r4300, uint32_t address)
{
    do_SP_Task(r4300->sp);
    cp0_base_cycle(r4300);
    if (r4300_translate_address(r4300, &address, &r4300->cached, 2))
        return NULL;
    if (r4300->cached)
    {
        struct instcache *line = &r4300->icache[(address >> 5) & UINT32_C(0x1FF)];
        if(!icache_hit(line, address))
            icache_fill(line, r4300, address);
        return &line->words[address >> 2 & 7];
    }
    else
    {
        address &= UINT32_C(0x1ffffffc);
        mem_read32(mem_get_handler(r4300->mem, address), address, &(uint32_t){0}); // Done in order to get correct cycle count
        return mem_base_u32(r4300->mem->base, address);
    }
}
