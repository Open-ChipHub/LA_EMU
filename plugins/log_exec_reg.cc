#include <iostream>

extern "C" {
#include "../plugin.h"
}


static size_t haldle_gpr;

static const char *const loongarch_r_alias[32] =
{
    "zer", "ra", "tp", "sp", "a0", "a1", "a2", "a3",
    "a4",   "a5", "a6", "a7", "t0", "t1", "t2", "t3",
    "t4",   "t5", "t6", "t7", "t8", "r21","fp", "s0",
    "s1",   "s2", "s3", "s4", "s5", "s6", "s7", "s8",
};

static void show_register(void *env) {
    for (int i = 0; i <32; i++) {
        printf("r%02d/%-3s:%016lx    ", i, loongarch_r_alias[i], la_emu_get_gpr(env, haldle_gpr, i));
        if ((i + 1) % 4 == 0) {
            printf("\n");
        }
    }
}

void my_emu_insn_before(void* env, uint64_t pc, uint32_t insn) {
    printf("pc:%016lx insn:%09x\n", pc, insn);
    show_register(env);
}

la_emu_plugin_ops my_op = {
    .emu_insn_before = my_emu_insn_before,
};

extern "C" la_emu_plugin_ops* la_emu_plugin_install(const char* arg) {
    haldle_gpr = la_emu_get_handle_gpr();
    printf("%lx\n", haldle_gpr);
    return &my_op;
}
