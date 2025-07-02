#ifndef DIFFTEST_H
#define DIFFTEST_H

#include <stdint.h>

typedef enum DiffProgType {
    DIFF_PROG_TYPE_ELF = 0,
    DIFF_PROG_TYPE_CKPT = 1,
    DIFF_PROG_TYPE_QCKPT = 2,
} DiffProgType;

typedef struct DiffConfig {
    uint64_t ram_size;
    DiffProgType prog_type;
    char* elf_path;
    char* ckpt_dir;
    char* qckpt_mem_path;
    char* qckpt_cpu_path;
    char* cpu_option;
    uint8_t has_debugcon;
    uint64_t debugcon_base_addr;
} DiffConfig;

typedef struct DiffInitInfo {
    uint64_t start_pc;
} DiffInitInfo;

#endif