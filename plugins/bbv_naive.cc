#include <string>
#include <vector>
#include <unordered_map>
#include <zlib.h>
#include "common.h"

extern "C" {
#include "../plugin.h"
}

using namespace std;

bool begin_after_ibar0x40;
static uint64_t interval_size = 1000000;
static FILE* bbv_file;
static gzFile gz_bbv_file;
bool is_gziped;
// simpoint id can not be zero
uint64_t unique_trans_id = 1;

uint64_t last_id;
uint64_t last_pc = -1;
uint32_t last_insn;
uint64_t cur_block_count;

uint64_t cur_interval_count;

unordered_map<uint64_t, uint64_t> pc2id;
unordered_map<uint64_t, uint64_t> interval_id_cnt;

static inline bool is_br_inst(uint32_t insn) {
    return ((insn >> 26) >= 0x10 && (insn >> 26) <= 0x1b); // beqz - bgeu
}

static void bbv_printf(const char* format, ...) {
    va_list args;
    va_start(args, format);

    if (is_gziped) {
        gzvprintf(gz_bbv_file, format, args);
    } else {
        vfprintf(bbv_file, format, args);
    }

    va_end(args);
}

void my_emu_insn_before(void* env, uint64_t pc, uint32_t insn) {
    if (begin_after_ibar0x40 && last_pc == -1) {
        if (insn == 0x38728040) { // 0x38728040 is ibar 0x40
            last_pc = pc;
            last_insn = insn;
            ++ cur_block_count;
            ++ cur_interval_count;
        }
        return;
    }
    
    if (pc != last_pc + 4 || is_br_inst(last_insn)) {
        interval_id_cnt[last_id] += cur_block_count;
        cur_block_count = 0;
        if(!pc2id.count(pc)) {
            pc2id[pc] = unique_trans_id;
            last_id = unique_trans_id;
            ++ unique_trans_id;
        } else {
            last_id = pc2id[pc];
        }
    }

    last_pc = pc;
    last_insn = insn;
    ++ cur_block_count;
    ++ cur_interval_count;

    if (cur_interval_count == interval_size) {
        bbv_printf("T");
        for (auto i = interval_id_cnt.begin();i != interval_id_cnt.end();i ++) {
            auto& k = i->first;
            auto& v = i->second;
            if (k != 0) {
                bbv_printf(":%ld:%ld ", k, v);
            }
        }
        bbv_printf("\n");

        interval_id_cnt.clear();
        cur_interval_count = 0;
        cur_block_count = 0;
    }
}

void my_emu_stop() {
    if (is_gziped) {
        gzclose(gz_bbv_file);
    } else {
        fclose(bbv_file);
    }
}

la_emu_plugin_ops my_op = {
    .emu_stop = my_emu_stop,
    .emu_insn_before = my_emu_insn_before,
};

extern "C" la_emu_plugin_ops* la_emu_plugin_install(const char* arg) {
    string bbv_file_name("bbv.txt");
    string gz_bbv_file_name("bbv.gz");
    if (arg[0]) {
        auto options = split(arg, ",");
        for (auto &option : options) {
            auto sp = split(option, "=");
            if (sp[0] == "interval") {
                interval_size = stol(sp[1]);
            } else if (sp[0] == "bbv") {
                bbv_file_name = sp[1];
                gz_bbv_file_name = sp[1];
            } else if (sp[0] == "ibar0x40") {
                begin_after_ibar0x40 = stol(sp[1]);
            } else if (sp[0] == "gz") {
                is_gziped = stol(sp[1]);
            } else {
                printf("unknoen option:%s\n", option.c_str());
            }
        }
    }
    if (is_gziped) {
        gz_bbv_file = gzopen(gz_bbv_file_name.c_str(), "wb");
    } else {
        bbv_file = fopen_nofail(bbv_file_name.c_str(), "w");
    }
    return &my_op;
}
