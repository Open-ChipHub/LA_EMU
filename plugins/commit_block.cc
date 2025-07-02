#include <string>
#include <vector>
#include <unordered_map>
#include "common.h"

using namespace std;

extern "C" {
#include "../plugin.h"
}

#define FETCH_NUM 16
static FILE* log_file;
int cur_block_inst_num = 0;
uint64_t cur_block_pc_s;
bool has_exec_first_inst;

void my_emu_insn_before(void* env, uint64_t pc, uint32_t insn) {
    if (!has_exec_first_inst) {
        cur_block_pc_s = pc;
        has_exec_first_inst = true;
    }
}

void my_emu_insn_after(void* env, uint64_t pc, uint32_t insn, uint64_t next_pc) {
    cur_block_inst_num++;
    if (cur_block_inst_num == FETCH_NUM || next_pc != pc + 4) {
        fprintf(log_file, "pc_s=%#lx,pc_e=%#lx,pc_t=%#lx,pc_e_insn=%#x\n",
            cur_block_pc_s, pc, next_pc, insn);
        cur_block_inst_num = 0;
        cur_block_pc_s = next_pc;
    }
}

la_emu_plugin_ops my_op = {
    .emu_insn_before = my_emu_insn_before,
    .emu_insn_after = my_emu_insn_after,
};

extern "C" la_emu_plugin_ops* la_emu_plugin_install(const char* arg) {
    string log_name("ref_commit_block.txt");
    if (arg[0]) {
        auto options = split(arg, ",");
        for (auto &option : options) {
            auto sp = split(option, "=");
            if (sp[0] == "log") {
                log_name = stol(sp[1]);
            } else {
                printf("unknoen option:%s\n", option.c_str());
            }
        }
    }
    log_file = fopen_nofail(log_name.c_str(), "w");
    return &my_op;
}
