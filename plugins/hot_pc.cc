#include "common.h"
#include <functional>

using namespace std;

extern "C" {
#include "../plugin.h"
}

unordered_map<uint64_t, uint64_t> pc_cnt;
string log_file_name;

void my_emu_insn_before(void* env, uint64_t pc, uint32_t insn) {
    ++ pc_cnt[((uint64_t*)env)[1]];
}

void my_emu_stop() {
    uint64_t total_cnt = 0;
    // 使用 multimap 以值作为键，以原键作为值
    multimap<uint64_t, int64_t, greater<uint64_t>> sortedByValue;

    // 插入反转的键值对
    for (const auto& kv : pc_cnt) {
        total_cnt += kv.second;
        sortedByValue.insert(std::make_pair(kv.second, kv.first));
    }

    FILE* logfile = fopen_nofail(log_file_name.c_str(), "w");
    uint64_t acc_cnt = 0;
    // 输出排序后的结果
    for (const auto& vk : sortedByValue) {
        acc_cnt += vk.first;
        fprintf(logfile, "%016lx:%ld, %f%%, %f%%\n", vk.second, vk.first, double(vk.first) / total_cnt * 100, double(acc_cnt) / total_cnt * 100);
    }

    fclose(logfile);
}

la_emu_plugin_ops my_op = {
    .emu_stop = my_emu_stop,
    .emu_insn_before = my_emu_insn_before,
};

extern "C" la_emu_plugin_ops* la_emu_plugin_install(const char* arg) {
    auto options = split(arg, ",");
    for (auto &option : options) {
        auto sp = split(option, "=");
        if (sp[0] == "log_file") {
            log_file_name = sp[1];
        } else {
            printf("unknown option:%s\n", option.c_str());
        }
    }
    return &my_op;
}
