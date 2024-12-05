#include "qemu/osdep.h"
#include "cpu.h"
#include <stdio.h>
#include <inttypes.h>
#include <linux/virtio_config.h>
#include <linux/virtio_mmio.h>
#include <linux/virtio_ring.h>
#include <linux/virtio_blk.h>
#include "simple_virtio_blk.h"

#include <errno.h>

#include "util.h"


#define BUFFER_SIZE 32
void format_size(unsigned long bytes, char *buffer, size_t buf_size) {
    if (bytes < 1024) {
        snprintf(buffer, buf_size, "%lu B", bytes);
    }
    else if (bytes < (1024 * 1024)) {
        double kb = bytes / 1024.0;
        snprintf(buffer, buf_size, "%.2f KB", kb);
    }
    else if (bytes < (1024 * 1024 * 1024)) {
        double mb = bytes / (1024.0 * 1024.0);
        snprintf(buffer, buf_size, "%.2f MB", mb);
    }
    else {
        double gb = bytes / (1024.0 * 1024.0 * 1024.0);
        snprintf(buffer, buf_size, "%.2f GB", gb);
    }
}

// Macro to set the lower 32 bits of a 64-bit variable
#define SET_LOW32(x, val)                                   \
    do {                                                    \
        (x) = ((x) & 0xFFFFFFFF00000000ULL) |             \
              ((uint64_t)(val) & 0xFFFFFFFFULL);           \
    } while(0)

// Macro to set the higher 32 bits of a 64-bit variable
#define SET_HIGH32(x, val)                                  \
    do {                                                    \
        (x) = ((x) & 0x00000000FFFFFFFFULL) |             \
              (((uint64_t)(val) & 0xFFFFFFFFULL) << 32);   \
    } while(0)

long get_file_size(const char *filename) {
    FILE *file = fopen(filename, "rb");  // 打开文件，以二进制读取模式
    if (file == NULL) {
        perror("Failed to open file");
        return -1;
    }

    // 移动文件指针到文件末尾
    if (fseek(file, 0, SEEK_END) != 0) {
        perror("Failed to seek to end of file");
        fclose(file);
        return -1;
    }

    // 获取当前文件指针的位置，即文件大小
    long size = ftell(file);
    if (size == -1) {
        perror("Failed to tell file position");
        fclose(file);
        return -1;
    }

    // 关闭文件
    fclose(file);
    return size;
}

#define VIRTIO_SECTOR_SIZE 512

FILE* blk_file_init(const char* filename) {
    FILE* blk_file = fopen(filename, "rb+");
    if (!blk_file) {
        qemu_log("virtio_blk: can not open %s (%s)\n", filename, strerror(errno));
        abort();
    }
    qemu_log_mask(CPU_LOG_VIRTIO_BLK, "virtio_blk: %s:%d %s %d\n", __FILE__,__LINE__,__func__, blk_file->_fileno);
    return blk_file;
}
static void blk_file_fini(FILE* blk_file) {
    fclose(blk_file);
}

void blk_file_read(FILE* blk_file, uint64_t sector, uint64_t size, void* data) {
    qemu_log_mask(CPU_LOG_VIRTIO_BLK, "virtio_blk: %s:%d %s %d\n", __FILE__,__LINE__,__func__);
    uint64_t offset = sector * VIRTIO_SECTOR_SIZE;
    fseek(blk_file, offset, SEEK_SET);
    size_t r = fread(data, size, 1, blk_file);
    if(r != 1) {
        perror("fread");
        exit(-1);
    }
}

void blk_file_write(FILE* blk_file, uint64_t sector, uint64_t size, void* data) {
    qemu_log_mask(CPU_LOG_VIRTIO_BLK, "virtio_blk: %s:%d %s %d\n", __FILE__,__LINE__,__func__);
    uint64_t offset = sector * VIRTIO_SECTOR_SIZE;
    fseek(blk_file, offset, SEEK_SET);
    size_t r = fwrite(data, size, 1, blk_file);
    if(r != 1) {
        perror("fwrite");
        exit(-1);
    }
    fflush(blk_file);
}



VirtioBlkState *simple_virtio_blk_init(qemu_irq irq, const char* filename) {
    VirtioBlkState *s;

    s = (VirtioBlkState*)calloc(1, sizeof(VirtioBlkState));
    lsassert(s);

    s->device_features = (1ull << VIRTIO_F_VERSION_1);
    s->irq = irq;

    s->blk_file = blk_file_init(filename);

    s->capacity = get_file_size(filename);
    if (s->capacity & 0x100000) {
        fprintf(stderr, "size of virtio_blk file %s must be multiplr of 1M, get %lx", filename, s->capacity);
        exit(-1);
    }

    strncpy(s->blk_filename, filename, PATH_MAX);

    return s;
}

void simple_virtio_blk_fini(void *opaque) {
    VirtioBlkState *s = opaque;
    blk_file_fini(s->blk_file);

    char buffer_num_bytes_read[BUFFER_SIZE];
    char buffer_num_bytes_write[BUFFER_SIZE];
    format_size(s->num_bytes_read, buffer_num_bytes_read, BUFFER_SIZE);
    format_size(s->num_bytes_write, buffer_num_bytes_write, BUFFER_SIZE);
    qemu_log("virtio_blk: [%s] read:%s write:%s\n", s->blk_filename, buffer_num_bytes_read, buffer_num_bytes_write);

    free(s);
}

void dump_vring_desc(struct vring_desc* desc) {
    qemu_log_mask(CPU_LOG_VIRTIO_BLK, "addr:%llx len:%x flags:%x next:%x\n", desc->addr, desc->len, desc->flags, desc->next);
}

void dump_virtio_blk_outhdr(struct virtio_blk_outhdr* req) {
    qemu_log_mask(CPU_LOG_VIRTIO_BLK, "type:%x ioprio:%x sector:%llx\n", req->type, req->ioprio, req->sector);
}


void handle_request(void *opaque) {

#define QUEUE_NUM_MASKED(x) ((x) & (s->queue_num - 1))

    VirtioBlkState *s = opaque;
    struct vring_desc* vq_desc = (void*)(ram + s->queue_desc);
    struct vring_avail* vq_avail = (void*)(ram + s->queue_avail);
    struct vring_used* vq_used = (void*)(ram + s->queue_used);
    while (s->vq_avail_currect != vq_avail->idx) {
        uint16_t vq_avail_currect_index = QUEUE_NUM_MASKED(s->vq_avail_currect);
        uint32_t vq_desc_idx = vq_avail->ring[vq_avail_currect_index];
        qemu_log_mask(CPU_LOG_VIRTIO_BLK, "vq_avail_currect:%x vq_avail_currect_index:%x vq_desc_idx:%x vq_avail_next:%x\n", s->vq_avail_currect, vq_avail_currect_index, vq_desc_idx, vq_avail->idx);
        uint32_t begin_vq_desc_idx = vq_desc_idx;
        dump_vring_desc(vq_desc + vq_desc_idx);
        struct virtio_blk_outhdr *req = (void*)(ram + vq_desc[vq_desc_idx].addr);
        dump_virtio_blk_outhdr(req);

        if (vq_desc[vq_desc_idx].next != QUEUE_NUM_MASKED((vq_desc_idx + 1))) {
            qemu_log("vq_desc_idx:%x next:%x\n", vq_desc_idx, vq_desc[vq_desc_idx].next);
            exit(-1);
        }

        vq_desc_idx = QUEUE_NUM_MASKED(vq_desc_idx + 1);
        dump_vring_desc(vq_desc + vq_desc_idx);
        uint64_t data_len = vq_desc[vq_desc_idx].len;
        uint64_t data_addr = vq_desc[vq_desc_idx].addr;

        if (vq_desc[vq_desc_idx].next != QUEUE_NUM_MASKED(vq_desc_idx + 1)) {
            qemu_log("vq_desc_idx:%x next:%x\n", vq_desc_idx, vq_desc[vq_desc_idx].next);
            exit(-1);
        }
        vq_desc_idx = QUEUE_NUM_MASKED(vq_desc_idx + 1);
        dump_vring_desc(vq_desc + vq_desc_idx);
        uint64_t status_addr = vq_desc[vq_desc_idx].addr;

        vq_used->ring[vq_avail_currect_index].id = begin_vq_desc_idx;
        vq_used->ring[vq_avail_currect_index].len = data_len;
        ram_stb(status_addr, 0);
        vq_used->idx = s->vq_avail_currect + 1;

        s->vq_avail_currect = (s->vq_avail_currect + 1);

        qemu_log_mask(CPU_LOG_VIRTIO_BLK, "virtio_blk: %s:%d %s data_addr:%lx begin_vq_desc_idx:%x\n", __FILE__,__LINE__,__func__, data_addr, begin_vq_desc_idx);

        if (req->type == VIRTIO_BLK_T_IN) {
            s->num_bytes_read += data_len;
            blk_file_read(s->blk_file, req->sector, data_len, ram + data_addr);
        } else if (req->type == VIRTIO_BLK_T_OUT) {
            s->num_bytes_write += data_len;
            blk_file_write(s->blk_file, req->sector, data_len, ram + data_addr);
        } else {
            qemu_log("virtio_blk: %s:%d %s unknown type:%x\n", __FILE__,__LINE__,__func__, req->type);
        }
    }
}

uint64_t virtio_blk_ioport_read(void *opaque, uint64_t addr, unsigned size) {
    VirtioBlkState *s = opaque;
    uint64_t ret = 0;
    switch (addr) {
        case VIRTIO_MMIO_MAGIC_VALUE:
            ret = ('v' | 'i' << 8 | 'r' << 16 | 't' << 24);
            break;
        case VIRTIO_MMIO_VERSION:
            ret = 2;
            break;
        case VIRTIO_MMIO_DEVICE_ID:
            ret = 2; /* blk */
            break;
        case VIRTIO_MMIO_VENDOR_ID:
            ret = 0x554D4551; /* 'QEMU' */
            break;
        case VIRTIO_MMIO_DEVICE_FEATURES:
            ret = s->device_features >> (s->virtio_mmio_device_features_sel * 32);
            break;
        case VIRTIO_MMIO_INTERRUPT_STATUS:
            ret = 1;
            break;
        case VIRTIO_MMIO_STATUS:
            ret = s->status;
            break;
        case VIRTIO_MMIO_QUEUE_READY:
            ret = s->queue_ready;
            break;
        case VIRTIO_MMIO_QUEUE_NUM_MAX:
            ret = 1024;
            break;
        case VIRTIO_MMIO_CONFIG_GENERATION:
            ret = 0;
            break;
        case 0x100:
            ret = s->capacity >> 9;
            break;
        case 0x104:
            ret = 0;
            break;
        default:
            fprintf(stderr, "%s, unknown addr:%lx, data:%x, size:%d\n", __func__, addr, 0, size);
    }
    qemu_log_mask(CPU_LOG_VIRTIO_BLK, "%s, addr:%lx, data:%lx, size:%d\n", __func__, addr, ret, size);
    return ret;
}

void virtio_blk_ioport_write(void *opaque, uint64_t addr, uint64_t val, unsigned size) {
    VirtioBlkState *s = opaque;
    switch (addr) {
        case VIRTIO_MMIO_STATUS:
            {
                uint32_t changed_status = val ^ s->status;
                if (changed_status & VIRTIO_CONFIG_S_ACKNOWLEDGE)     qemu_log_mask(CPU_LOG_VIRTIO_BLK, "[LA_EMU]: %s, driver set status VIRTIO_CONFIG_S_ACKNOWLEDGE\n", __func__);
                if (changed_status & VIRTIO_CONFIG_S_DRIVER)          qemu_log_mask(CPU_LOG_VIRTIO_BLK, "[LA_EMU]: %s, driver set status VIRTIO_CONFIG_S_DRIVER\n", __func__);
                if (changed_status & VIRTIO_CONFIG_S_DRIVER_OK)       qemu_log_mask(CPU_LOG_VIRTIO_BLK, "[LA_EMU]: %s, driver set status VIRTIO_CONFIG_S_DRIVER_OK\n", __func__);
                if (changed_status & VIRTIO_CONFIG_S_FEATURES_OK)     qemu_log_mask(CPU_LOG_VIRTIO_BLK, "[LA_EMU]: %s, driver set status VIRTIO_CONFIG_S_FEATURES_OK\n", __func__);
                if (changed_status & VIRTIO_CONFIG_S_NEEDS_RESET)     qemu_log_mask(CPU_LOG_VIRTIO_BLK, "[LA_EMU]: %s, driver set status VIRTIO_CONFIG_S_NEEDS_RESET\n", __func__);
                if (changed_status & VIRTIO_CONFIG_S_FAILED)          qemu_log("[LA_EMU]: %s, driver set status VIRTIO_CONFIG_S_FAILED\n", __func__);
                s->status = val;
            }
            break;
        case VIRTIO_MMIO_DEVICE_FEATURES_SEL:
            s->virtio_mmio_device_features_sel = val;
            break;
        case VIRTIO_MMIO_DRIVER_FEATURES:
            s->driver_features = val;
            break;
        case VIRTIO_MMIO_DRIVER_FEATURES_SEL:
            s->virtio_mmio_driver_features_sel = val;
            break;
        case VIRTIO_MMIO_QUEUE_SEL:
            lsassert(val == 0);
            s->virtio_mmio_queue_sel = val;
            break;
        case VIRTIO_MMIO_QUEUE_NUM:
            s->queue_num = val;
            break;
        case VIRTIO_MMIO_QUEUE_READY:
            s->queue_ready = val;
            if (val == 1) {
                qemu_log_mask(CPU_LOG_VIRTIO_BLK, "%s, queue_desc:%lx\n", __func__, s->queue_desc);
                qemu_log_mask(CPU_LOG_VIRTIO_BLK, "%s, queue_avail:%lx\n", __func__, s->queue_avail);
                qemu_log_mask(CPU_LOG_VIRTIO_BLK, "%s, queue_used:%lx\n", __func__, s->queue_used);
            }
            break;
        case VIRTIO_MMIO_QUEUE_NOTIFY:
            lsassert(val == 0);
            handle_request(s);
            qemu_irq_raise(s->irq);
            break;
        case VIRTIO_MMIO_INTERRUPT_ACK:
            qemu_irq_lower(s->irq);
            break;
        case VIRTIO_MMIO_QUEUE_DESC_LOW:
            SET_LOW32(s->queue_desc, val);
            qemu_log_mask(CPU_LOG_VIRTIO_BLK, "%s, queue_desc:%lx\n", __func__, s->queue_desc);
            break;
        case VIRTIO_MMIO_QUEUE_DESC_HIGH:
            SET_HIGH32(s->queue_desc, val);
            qemu_log_mask(CPU_LOG_VIRTIO_BLK, "%s, queue_desc:%lx\n", __func__, s->queue_desc);
            break;
        case VIRTIO_MMIO_QUEUE_AVAIL_LOW:
            SET_LOW32(s->queue_avail, val);
            qemu_log_mask(CPU_LOG_VIRTIO_BLK, "%s, queue_avail:%lx\n", __func__, s->queue_avail);
            break;
        case VIRTIO_MMIO_QUEUE_AVAIL_HIGH:
            SET_HIGH32(s->queue_avail, val);
            qemu_log_mask(CPU_LOG_VIRTIO_BLK, "%s, queue_avail:%lx\n", __func__, s->queue_avail);
            break;
        case VIRTIO_MMIO_QUEUE_USED_LOW:
            SET_LOW32(s->queue_used, val);
            qemu_log_mask(CPU_LOG_VIRTIO_BLK, "%s, queue_used:%lx\n", __func__, s->queue_used);
            break;
        case VIRTIO_MMIO_QUEUE_USED_HIGH:
            SET_HIGH32(s->queue_used, val);
            qemu_log_mask(CPU_LOG_VIRTIO_BLK, "%s, queue_used:%lx\n", __func__, s->queue_used);
            break;
        default:
            fprintf(stderr, "%s, unknown addr:%lx, data:%lx, size:%d\n", __func__, addr, val, size);
    }
    qemu_log_mask(CPU_LOG_VIRTIO_BLK, "%s, addr:%lx, data:%lx, size:%d\n", __func__, addr, val, size);
}


