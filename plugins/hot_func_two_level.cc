#include "common.h"
#include <assert.h>
#include <elf.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#include <functional>

using namespace std;

extern "C" {
#include "../plugin.h"
}

typedef struct {
    uint64_t sym_start;
    uint64_t sym_end;
    size_t strtab_index;
} sym_item_info;

int compare_sym_item_info(const void *a, const void *b) {
    const sym_item_info *i1 = (const sym_item_info *)a;
    const sym_item_info *i2 = (const sym_item_info *)b;

    // 首先比较起始点
    if (i1->sym_start < i2->sym_start) {
        return -1;
    } else if (i1->sym_start > i2->sym_start) {
        return 1;
    } else {
        // 如果起始点相同，则比较终点
        if (i1->sym_end < i2->sym_end) {
            return -1;
        } else if (i1->sym_end > i2->sym_end) {
            return 1;
        } else {
            return 0;
        }
    }
}

typedef struct {
    uint64_t sym_item_size;
    sym_item_info* sym_items;
    char* strtab;
} sym_info;



char *read_string_table(int fd, Elf64_Shdr *shdr) {
    char *strings = (char *)malloc(shdr->sh_size);
    if (strings == NULL) {
        fprintf(stderr, "Failed to allocate memory for string table\n");
        return NULL;
    }
    if (lseek(fd, shdr->sh_offset, SEEK_SET) < 0) {
        perror("Failed to seek to string table");
        free(strings);
        return NULL;
    }
    if (read(fd, strings, shdr->sh_size) != shdr->sh_size) {
        perror("Failed to read string table");
        free(strings);
        return NULL;
    }
    return strings;
}

int load_symbol_addresses(sym_info* sym_info, const char *filename) {
    int fd;
    Elf64_Ehdr ehdr;
    Elf64_Shdr shdr;
    Elf64_Sym sym;
    char *strtab = NULL;
    int strtab_found = 0;

    fd = open(filename, O_RDONLY);
    if (fd < 0) {
        perror("Failed to open file");
        return -1;
    }

    if (read(fd, &ehdr, sizeof(ehdr)) != sizeof(ehdr)) {
        perror("Failed to read ELF header");
        close(fd);
        return -1;
    }

    if (memcmp(ehdr.e_ident, ELFMAG, SELFMAG) != 0) {
        fprintf(stderr, "Not a valid ELF file\n");
        close(fd);
        return -1;
    }

    if (lseek(fd, ehdr.e_shoff, SEEK_SET) < 0) {
        perror("Failed to seek to section header table");
        close(fd);
        return -1;
    }

    for (int i = 0; i < ehdr.e_shnum; i++) {
        if (read(fd, &shdr, sizeof(shdr)) != sizeof(shdr)) {
            perror("Failed to read section header");
            close(fd);
            return -1;
        }

        if (shdr.sh_type == SHT_STRTAB && i != ehdr.e_shstrndx) {
            // 读取字符串表
            strtab = read_string_table(fd, &shdr);
            if (strtab != NULL) {
                strtab_found = 1;
            }
        }
        sym_info->strtab = strtab;
    }

    if (!strtab_found || strtab == NULL) {
        fprintf(stderr, "String table not found or failed to load\n");
        close(fd);
        return -1;
    }

    if (lseek(fd, ehdr.e_shoff, SEEK_SET) < 0) {
        perror("Failed to seek to section header table");
        close(fd);
        return -1;
    }

    sym_info->sym_item_size = 0;
    int sym_item_cnt = 0;
    for (int i = 0; i < ehdr.e_shnum; i++) {
        if (read(fd, &shdr, sizeof(shdr)) != sizeof(shdr)) {
            perror("Failed to read section header");
            close(fd);
            return -1;
        }

        if (shdr.sh_type == SHT_SYMTAB || shdr.sh_type == SHT_DYNSYM) {
            int symcount = shdr.sh_size / sizeof(Elf64_Sym);
            if (lseek(fd, shdr.sh_offset, SEEK_SET) < 0) {
                perror("Failed to seek to symbol table");
                close(fd);
                return -1;
            }
            if (!sym_info->sym_item_size) {
                sym_info->sym_items = (sym_item_info*)malloc(sizeof(sym_item_info) * symcount);
            } else {
                sym_info->sym_items = (sym_item_info*)realloc(sym_info->sym_items, sizeof(sym_item_info) * symcount);
            }
            sym_info->sym_item_size += symcount;
            for (int j = 0; j < symcount; j++) {
                if (read(fd, &sym, sizeof(sym)) != sizeof(sym)) {
                    perror("Failed to read symbol");
                    close(fd);
                    return -1;
                }
                sym_info->sym_items[sym_item_cnt].sym_start = sym.st_value;
                sym_info->sym_items[sym_item_cnt].sym_end = sym.st_value + sym.st_size;
                sym_info->sym_items[sym_item_cnt].strtab_index = sym.st_name;
                sym_item_cnt ++;
                // printf("Symbol %d: %s, Address = 0x%lx, Size = 0x%lx\n", j, strtab + sym.st_name, sym.st_value, sym.st_size);
                // if (sym.st_name != 0) {
                // } else {
                    // printf("Symbol %d: [No Name], Address = 0x%lx, Size = 0x%lx\n", j, sym.st_value, sym.st_size);
                // }
            }
        }
    }

    qsort(sym_info->sym_items, sym_info->sym_item_size, sizeof(sym_item_info), compare_sym_item_info);

    // free(strtab);
    close(fd);
    return 0;
}



sym_info* sym_info_init(const char *filename) {
    sym_info* r = (sym_info*)malloc(sizeof(sym_info));
    assert(r);
    if (load_symbol_addresses(r, filename) != 0) {
        fprintf(stderr, "load_symbol_addresses fail\n");
        exit(EXIT_FAILURE);
    }
    return r;
}

void sym_info_print_one(sym_info* sym_info, int64_t index) {
    if (index < 0) {
        printf("ill sym\n");
        return ;
    }
    if (sym_info->sym_items[index].strtab_index != 0) {
        printf("Symbol %ld: %s, Address = 0x%lx, Size = 0x%lx\n", index, sym_info->strtab + sym_info->sym_items[index].strtab_index, sym_info->sym_items[index].sym_start, sym_info->sym_items[index].sym_end - sym_info->sym_items[index].sym_start);
    } else {
        printf("Symbol %ld: [No Name], Address = 0x%lx, Size = 0x%lx\n", index, sym_info->sym_items[index].sym_start, sym_info->sym_items[index].sym_end - sym_info->sym_items[index].sym_start);
    }
}

void sym_info_print(sym_info* sym_info) {
    for (uint64_t i=0; i < sym_info->sym_item_size; i++) {
        sym_info_print_one(sym_info, i);
    }
}


int64_t find_interval(sym_item_info intervals[], uint64_t n, uint64_t value) {
    int64_t low = 0;
    int64_t high = n - 1;

    while (low <= high) {
        int64_t mid = low + (high - low) / 2;

        if (value >= intervals[mid].sym_start && value < intervals[mid].sym_end) {
            // 如果值位于当前区间内，返回该区间的索引
            return mid;
        } else if (value < intervals[mid].sym_start) {
            // 如果值小于当前区间的起始值，搜索左侧区间
            high = mid - 1;
        } else {
            // 如果值大于等与当前区间的结束值，搜索右侧区间
            low = mid + 1;
        }
    }

    return -1; // 如果没有找到任何包含该值的区间
}

int64_t sym_info_find_interval(sym_info* sym_info, uint64_t addr) {
    return  find_interval(sym_info->sym_items, sym_info->sym_item_size, addr);
}

void sym_info_fini(sym_info* sym_info) {
    free (sym_info->sym_items);
    free (sym_info->strtab);
    free (sym_info);
}


sym_info* elf_sym_info;
string log_file_name;
unordered_map<uint64_t, unordered_map<uint64_t, uint64_t>> pc_cnt;

void my_emu_insn_before(void* env, uint64_t pc, uint32_t insn) {
    ++ pc_cnt[((uint64_t*)env)[1]][pc];
}

void my_emu_insn_before_retpc(void* env, uint64_t pc, uint32_t insn) {
}

void my_emu_stop() {
    unordered_map<int64_t, unordered_map<int64_t, uint64_t>> func_cnt;
    unordered_map<int64_t, uint64_t> func_cnt_total;
    for (auto& kv : pc_cnt) {
        int64_t stem_func_index = sym_info_find_interval(elf_sym_info, kv.first);
        auto &c = func_cnt[stem_func_index];
        auto &c_total = func_cnt_total[stem_func_index];
        for (auto& leaf_pc : kv.second) {
            int64_t leaf_func_index = sym_info_find_interval(elf_sym_info, leaf_pc.first);
            c[leaf_func_index] += leaf_pc.second;
            c_total += leaf_pc.second;
        }
        // fprintf(stderr, "%-40s:%ld, %f%%, %f%%\n", vk.second <0 ? "[unknown]" : elf_sym_info->strtab + elf_sym_info->sym_items[vk.second].strtab_index, vk.first, double(vk.first) / total_cnt * 100, double(acc_cnt) / total_cnt * 100);
    }

    // 使用 multimap 以值作为键，以原键作为值
    multimap<uint64_t, int64_t, greater<uint64_t>> sortedByValue;

    // 插入反转的键值对
    for (const auto& kv : func_cnt_total) {
        sortedByValue.insert(std::make_pair(kv.second, kv.first));
    }

    FILE* logfile = fopen_nofail(log_file_name.c_str(), "w");

    for (const auto& vk : sortedByValue) {
        multimap<uint64_t, int64_t, greater<uint64_t>> sortedByValue2;
        for (const auto& kv : func_cnt[vk.second]) {
            sortedByValue2.insert(std::make_pair(kv.second, kv.first));
        }
        for (const auto& leaf : sortedByValue2) {
            fprintf(logfile, "%s;%s %ld\n",  vk.second < 0 ? "[unknown]" : elf_sym_info->strtab + elf_sym_info->sym_items[vk.second].strtab_index,
                                leaf.second < 0 ? "[unknown]" : elf_sym_info->strtab + elf_sym_info->sym_items[leaf.second].strtab_index,
                                leaf.first);
        }
    }

    fclose(logfile);
    sym_info_fini(elf_sym_info);
}

la_emu_plugin_ops my_op = {
    .emu_stop = my_emu_stop,
    .emu_insn_before = my_emu_insn_before,
};

extern "C" la_emu_plugin_ops* la_emu_plugin_install(const char* arg) {
    if (!arg[0]) {
        fprintf(stderr, "elf file name needed\n");
        exit(EXIT_FAILURE);
    }
    string elf_file_name;
    auto options = split(arg, ",");
    for (auto &option : options) {
        auto sp = split(option, "=");
        if (sp[0] == "elf_file") {
            elf_file_name = sp[1];
        } else if (sp[0] == "log_file") {
            log_file_name = sp[1];
        } else {
            printf("unknown option:%s\n", option.c_str());
        }
    }
    elf_sym_info = sym_info_init(elf_file_name.c_str());
    return &my_op;
}
