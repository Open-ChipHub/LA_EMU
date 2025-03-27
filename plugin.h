#ifndef PLUGIN_H
#define PLUGIN_H

#include <inttypes.h>

typedef struct la_emu_plugin_ops {
    void (*emu_start)(void);
    void (*emu_stop)(void);
    void (*emu_insn_before)(void* env, uint64_t pc, uint32_t insn);
    void (*emu_insn_after)(void* env, uint64_t pc, uint32_t insn, uint64_t next_pc);
    void (*emu_execption)(void* env, int ecode);
    // size_shift: size of access in ^2 (0=byte, 1=16bit, 2=32bit etc...)
    void (*emu_load)(uint64_t vaddr, uint32_t size_shift, void* data);
    void (*emu_store)(uint64_t vaddr, uint32_t size_shift, void* data);
} la_emu_plugin_ops;



typedef la_emu_plugin_ops* (*la_emu_plugin_install_func_t)(const char *);

void la_emu_save_checkpoint(void *env, char* name);

size_t la_emu_get_handle_gpr();
size_t la_emu_get_handle_fpr();

static inline uint64_t la_emu_get_gpr(void* env, size_t handle_gpr, int index) {
    return ((uint64_t*)((char*)env + handle_gpr))[index];
}
static inline uint32_t la_emu_get_fprw(void* env, size_t handle_fpr, int index, int element_index) {
    return ((uint32_t*)((char*)env + handle_fpr))[index * 8 + element_index];
}
static inline uint64_t la_emu_get_fprd(void* env, size_t handle_fpr, int index, int element_index) {
    return ((uint64_t*)((char*)env + handle_fpr))[index * 4 + element_index];
}

typedef enum Plugin_MMUAccessType {
    PLUGIN_MMU_DATA_LOAD  = 0,
    PLUGIN_MMU_DATA_STORE = 1,
    PLUGIN_MMU_INST_FETCH = 2
#define Plugin_MMU_ACCESS_COUNT 3
} Plugin_MMUAccessType;

// give address and access_type
// set physical and prot
// return 0 ok, -1 tlb miss
int la_emu_probe_get_physical_address(uint64_t *physical, int *prot, uint64_t address, Plugin_MMUAccessType access_type);

// return 0 means success
int la_emu_ram_read(uint64_t addr, uint64_t size, void* data);

#endif /* PLUGIN_H */
