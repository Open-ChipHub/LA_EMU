/*
 * QEMU 16550A UART emulation
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
 * Copyright (c) 2008 Citrix Systems, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "riscv_plic.h"
#include "qemu/atomic.h"

// #include "hw/char/serial.h"
// #include "chardev/char-serial.h"
// #include "qapi/error.h"
// #include "qemu/timer.h"
// #include "qemu/error-report.h"
// #include "trace.h"

//#define DEBUG_SERIAL
PlicState *riscv_plic_restore(int base, qemu_irq irq, int baudbase, const char* filename) {
    return NULL;
}


static uint32_t atomic_set_masked(uint32_t *a, uint32_t mask, uint32_t value)
{
    uint32_t old, new, cmp = qatomic_read(a);

    do {
        old = cmp;
        new = (old & ~mask) | (value & mask);
        cmp = qatomic_cmpxchg(a, old, new);
    } while (old != cmp);

    return old;
}

static void riscv_plic_set_pending(PlicState *plic, int irq, bool level)
{
    atomic_set_masked(&plic->pending[irq >> 5], 1 << (irq & 31), -!!level);
}

static void riscv_plic_set_claimed(PlicState *plic, int irq, bool level)
{
    atomic_set_masked(&plic->claimed[irq >> 5], 1 << (irq & 31), -!!level);
}

static void riscv_plic_update(PlicState *plic)
{
    // if (plic->enable[0][0]) {
    //     qemu_set_irq(plic->m_external_irqs, 1);
    // } else {
    //     qemu_set_irq(plic->m_external_irqs, 0);
    // }
    if (plic->enable[1][0] & plic->pending[0]) {
        qemu_set_irq(plic->s_external_irqs, 1);
        // fprintf(stderr, "lxy: %s:%d %s plic raise\n", __FILE__,__LINE__,__func__);
    } else {
        qemu_set_irq(plic->s_external_irqs, 0);
        // fprintf(stderr, "lxy: %s:%d %s plic down\n", __FILE__,__LINE__,__func__);
    }
}

static void riscv_plic_irq_request(void *opaque, int irq, int level)
{
    PlicState *s = (PlicState *)opaque;

    if (level > 0) {
        riscv_plic_set_pending(s, irq, true);
        riscv_plic_update(s);
    }
}


uint64_t riscv_plic_ioport_read(void *opaque, hwaddr addr, unsigned size) {
    assert(size == 4);
    assert(addr % 4 == 0);
    PlicState *s = (PlicState *)opaque;
    uint64_t r = 0;
    if (addr >= 0 && addr < (PLIC_SOURCE_NUM * 4)) {
        return s->source_priority[addr / 4];
    } else if (addr >= 0x001000 && addr < (0x001000 + PLIC_SOURCE_NUM / 8)) {
        return s->pending[(addr - 0x001000) / (8*4)];
    } else if (addr >= 0x002000 && addr < 0x1F1FFC) {
        uint32_t disp = (addr - 0x002000);
        uint32_t context = disp / 0x80;
        assert(context < PLIC_CONTEXT_NUM);
        return s->enable[context][(disp % 0x80) / (8*4)];
    } else if (addr >= 0x200000 && addr < 0x400000) {
        uint32_t disp = (addr - 0x200000);
        uint32_t context = disp / 0x1000;
        uint32_t context_addr = disp % 0x1000;
        assert(context < PLIC_CONTEXT_NUM);
        if (context_addr == 0) {
            r = s->threshold[context];
        } else if (context_addr == 4) {
            for (int i=0; i< PLIC_SOURCE_NUM; i++) {
                if ((1 << i) & s->enable[context][0] & s->pending[0]) {
                    r = i;
                    riscv_plic_set_pending(s, i, false);
                    riscv_plic_set_claimed(s, i, true);
                    break;
                }
            }
        } else {
            assert(0);
        }
    } else {
        fprintf(stderr, "[emu.plic] Unhandled IO device handles address 0x%lX, read\n", addr);
    }
    riscv_plic_update(s);
    // fprintf(stderr, "[emu.plic] IO device handles address 0x%lX, read\n", addr);
    return r;
}

void riscv_plic_ioport_write(void *opaque, hwaddr addr, uint64_t val, unsigned size) {
    assert(size == 4);
    assert(addr % 4 == 0);
    PlicState *s =(PlicState *)opaque;
    if (addr >= 0 && addr < (PLIC_SOURCE_NUM * 4)) {
        s->source_priority[addr / 4] = val;
    } else if (addr >= 0x002000 && addr < 0x1F1FFC) {
        uint32_t disp = (addr - 0x002000);
        uint32_t context = disp / 0x80;
        assert(context < PLIC_CONTEXT_NUM);
        s->enable[context][(disp % 0x80) / (8*4)] = val;
    } else if (addr >= 0x200000 && addr < 0x400000) {
        uint32_t disp = (addr - 0x200000);
        uint32_t context = disp / 0x1000;
        uint32_t context_addr = disp % 0x1000;
        assert(context < PLIC_CONTEXT_NUM);
        if (context_addr == 0) {
            s->threshold[context] = val;
        } else if (context_addr == 4) {
            assert (val < PLIC_SOURCE_NUM);
            riscv_plic_set_claimed(s, val, false);
        } else {
            assert(0);
        }
    } else {
        fprintf(stderr, "[emu.plic] Unhandled IO device handles address 0x%lX, write, %lx\n", addr, val);
    }
    riscv_plic_update(s);
    // fprintf(stderr, "[emu.plic] IO device handles address 0x%lX, write, %lx\n", addr, val);
}

void riscv_plic_check_io(PlicState *s) {
    riscv_plic_update(s);
}


PlicState *riscv_plic_init(int base, qemu_irq irq_m, qemu_irq irq_s, qemu_irq_handler *irq_request) {
    PlicState *s;

    s = (PlicState*)malloc(sizeof(PlicState));
    s->m_external_irqs = irq_m;
    s->s_external_irqs = irq_s;
    *irq_request = riscv_plic_irq_request;
    return s;
}

