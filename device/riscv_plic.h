#ifndef __RISCV_PLIC_H
#define __RISCV_PLIC_H

#include <stdio.h>
#include <stdint.h>
#include <assert.h>

#include "irq.h"

#define hwaddr uint64_t

#define UART_FIFO_LENGTH    16      /* 16550A Fifo Length */


#define PLIC_SOURCE_NUM 32
#define PLIC_CONTEXT_NUM 32

struct PlicState {
    int infd;
    int outfd;
    // addr 0
    uint32_t source_priority[PLIC_SOURCE_NUM];
    uint32_t pending[PLIC_SOURCE_NUM/8];
    uint32_t enable[PLIC_CONTEXT_NUM][PLIC_SOURCE_NUM/8];
    uint32_t claimed[PLIC_SOURCE_NUM/8];
    uint32_t threshold[PLIC_CONTEXT_NUM];

    qemu_irq m_external_irqs;
    qemu_irq s_external_irqs;

    // QEMUTimer *modem_status_poll;
    // MemoryRegion io;
};
typedef struct PlicState PlicState;

PlicState *riscv_plic_init(int base, qemu_irq irq_m, qemu_irq irq_s, qemu_irq_handler *irq_request);
PlicState *riscv_plic_restore(int base, qemu_irq irq, int baudbase, const char* filename);
uint64_t riscv_plic_ioport_read(void *opaque, hwaddr addr, unsigned size);
void riscv_plic_ioport_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
void riscv_plic_check_io(PlicState *s);

#endif
