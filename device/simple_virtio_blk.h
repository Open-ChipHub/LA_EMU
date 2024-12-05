#ifndef __SIMPLE_VIRTIO_BLK_H__
#define __SIMPLE_VIRTIO_BLK_H__

#include <stdio.h>
#include <stdint.h>
#include <limits.h>
#include "irq.h"


struct VirtioBlkState {
    uint64_t device_features;
    uint64_t driver_features;
    uint32_t virtio_mmio_device_features_sel;
    uint32_t virtio_mmio_driver_features_sel;
    uint32_t status;
    uint32_t virtio_mmio_queue_sel;
    uint32_t queue_ready;
    uint32_t queue_num;

    uint64_t queue_desc;
    uint64_t queue_avail;
    uint64_t queue_used;
    uint16_t vq_avail_currect;
    uint16_t vq_used_currect;
    qemu_irq irq;
    uint64_t capacity;
    char blk_filename[PATH_MAX];
    FILE* blk_file;
    uint64_t num_bytes_read;
    uint64_t num_bytes_write;
};
typedef struct VirtioBlkState VirtioBlkState;

uint64_t virtio_blk_ioport_read(void *opaque, uint64_t addr, unsigned size);
void virtio_blk_ioport_write(void *opaque, uint64_t addr, uint64_t val, unsigned size);

VirtioBlkState *simple_virtio_blk_init(qemu_irq irq, const char* filename);
void simple_virtio_blk_fini(void *opaque);


#endif