#include <string>
#include <vector>
#include <unordered_map>
#include "common.h"

using namespace std;

extern "C" {
#include "../plugin.h"
}

#define FETCH_NUM 16
#define ALIGN_BIT 3
#define BOUND_BIT 0
#define ROUND_BIT 6

static FILE* log_file;
int cur_block_inst_num = 0;
uint64_t cur_block_pc_s;
uint64_t nt_inst_num;
uint64_t prev_pc;
uint32_t prev_insn;
bool has_exec_first_inst;

static uint64_t get_pc_nt(uint64_t pc_s) {
    uint64_t pc_l = (pc_s >> ALIGN_BIT) << ALIGN_BIT;
    uint64_t pc_r_round = pc_l + (1 << ROUND_BIT);
    uint64_t pc_r_width = pc_s + (FETCH_NUM << 2);
    uint64_t pc_r = min(pc_r_round, pc_r_width);
    if (BOUND_BIT > 0) {
        if ((((pc_s ^ pc_r) >> BOUND_BIT) << BOUND_BIT) != 0) {
            pc_r = (pc_r >> BOUND_BIT) << BOUND_BIT;
        }
    }
    return pc_r;
}

static uint64_t get_nt_inst_num(uint64_t pc_s) {
    uint64_t pc_nt = get_pc_nt(pc_s);
    return (pc_nt - pc_s) >> 2;
}

void my_emu_insn_before(void* env, uint64_t pc, uint32_t insn) {
    if (!has_exec_first_inst) {
        cur_block_pc_s = pc;
        nt_inst_num = get_nt_inst_num(pc);
    }
}

void my_emu_insn_after(void* env, uint64_t pc, uint32_t insn, uint64_t next_pc) {
    if (!has_exec_first_inst) {
        has_exec_first_inst = true;
        prev_pc = pc;
        prev_insn = insn;
        cur_block_inst_num = 1;
        return;
    }
    if (cur_block_inst_num == nt_inst_num || pc != prev_pc + 4) {
        lsassert(cur_block_pc_s <= prev_pc);
        fprintf(log_file, "pc_s=%#lx,pc_e=%#lx,pc_t=%#lx,pc_e_insn=%#x\n",
            cur_block_pc_s, prev_pc, pc, prev_insn);
        prev_pc = pc;
        prev_insn = insn;
        cur_block_inst_num = 1;
        cur_block_pc_s = pc;
        nt_inst_num = get_nt_inst_num(pc);
        return;
    }

    cur_block_inst_num++;
    prev_pc = pc;
    prev_insn = insn;
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
