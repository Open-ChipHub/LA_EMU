/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU LoongArch CPU
 *
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 */

#ifndef RISCV_CPU_H
#define RISCV_CPU_H

#include "util.h"
#include "qemu/int128.h"
#include "qemu/compiler.h"
#include "hw/registerfields.h"
#include "fpu/softfloat-types.h"
// #include "cpu-csr.h"
#include "cpu_bits.h"
#include "cpu_cfg.h"

#if defined(CONFIG_PLUGIN)
#include "plugin.h"
#endif

#define NB_MMU_MODES 16

/**
 * vaddr:
 * Type wide enough to contain any #target_ulong virtual address.
 */
typedef uint64_t vaddr;
#define VADDR_PRId PRId64
#define VADDR_PRIu PRIu64
#define VADDR_PRIo PRIo64
#define VADDR_PRIx PRIx64
#define VADDR_PRIX PRIX64
#define VADDR_MAX UINT64_MAX

typedef struct CPUArchState CPURISCVState;

#define LOONGARCH_CSR_MAXADDR 0x1000

// used for determined emulation time scaling
#define TIME_SCALE 10
// increase this, vm time slower, real 1s, vm see 1 / MUL second
#define TIME_MUL 1
// increase this, vm time faster, real 1s, vm see 1 * DIV second
#define TIME_DIV 1

#define TIMER_PERIOD                (100 * TIME_MUL / TIME_DIV) /* 10 ns period for 100 MHz frequency */
#define CONSTANT_TIMER_TICK_MASK    0xfffffffffffcUL
#define CONSTANT_TIMER_ENABLE       0x1UL

typedef struct INSCache {
    bool (*trans_func)(void*, void*);
    int arg[4];
    int insn;
} INSCache;

#define IC_BITS 14
#define IC_NUM (1 << IC_BITS)
#define IC_MASK (((target_long)1 << IC_BITS) - 1)
#define IC_INDEX(va) ((va >> 2) & IC_MASK)

#define TC_BITS 8
#define TC_NUM (1 << 8)
#define TC_MASK (((target_long)1 << TC_BITS) - 1)
#define TC_INDEX(va) ((va >> TARGET_PAGE_BITS) & TC_MASK)

#define CPU_TLB_ENTRY_BITS 5

/* Minimalized TLB entry for use by TCG fast path. */
typedef union CPUTLBEntry {
    struct {
        uint64_t addr_read;
        uint64_t addr_write;
        uint64_t addr_code;
        /*
         * Addend to virtual address to get host address.  IO accesses
         * use the corresponding iotlb value.
         */
        uintptr_t addend;
    };
    /*
     * Padding to get a power of two size, as well as index
     * access to addr_{read,write,code}.
     */
    uint64_t addr_idx[(1 << CPU_TLB_ENTRY_BITS) / sizeof(uint64_t)];
} CPUTLBEntry;

QEMU_BUILD_BUG_ON(sizeof(CPUTLBEntry) != (1 << CPU_TLB_ENTRY_BITS));


typedef struct CPUNegativeOffsetState {
    char dummp[25];
    CPUTLBEntry tlb[NB_MMU_MODES][TC_NUM];
    // IcountDecr icount_decr;
    bool can_do_io;
} CPUNegativeOffsetState;


typedef enum MMUAccessType {
    MMU_DATA_LOAD  = 0,
    MMU_DATA_STORE = 1,
    MMU_INST_FETCH = 2
#define MMU_ACCESS_COUNT 3
} MMUAccessType;

/* same as PROT_xxx */
#define PAGE_READ      0x0001
#define PAGE_WRITE     0x0002
#define PAGE_EXEC      0x0004
#define PAGE_BITS      (PAGE_READ | PAGE_WRITE | PAGE_EXEC)
#define PAGE_VALID     0x0008

#define TARGET_LONG_BITS 64
#define TARGET_PHYS_ADDR_SPACE_BITS 48
#define TARGET_VIRT_ADDR_SPACE_BITS 48


#define TARGET_LONG_SIZE (TARGET_LONG_BITS / 8)

/* target_ulong is the type of a virtual address */
#if TARGET_LONG_SIZE == 4
typedef int32_t target_long;
typedef uint32_t target_ulong;
#define TARGET_FMT_lx "%08x"
#define TARGET_FMT_ld "%d"
#define TARGET_FMT_lu "%u"
#define MO_TL MO_32
#elif TARGET_LONG_SIZE == 8
typedef int64_t target_long;
typedef uint64_t target_ulong;
#define TARGET_FMT_lx "%016" PRIx64
#define TARGET_FMT_ld "%" PRId64
#define TARGET_FMT_lu "%" PRIu64
#define MO_TL MO_64
#else
#error TARGET_LONG_SIZE undefined
#endif

#if !defined(CONFIG_USER_ONLY)
#define TARGET_PAGE_BITS 12

#define TARGET_PAGE_SIZE   (1 << TARGET_PAGE_BITS)
#define TARGET_PAGE_MASK   ((target_ulong)-1 << TARGET_PAGE_BITS)
#else
extern target_ulong TARGET_PAGE_BITS;
extern target_ulong TARGET_PAGE_SIZE;
extern target_ulong TARGET_PAGE_MASK;
#endif

#define TARGET_PHYS_MASK MAKE_64BIT_MASK(0, TARGET_PHYS_ADDR_SPACE_BITS)
#define TARGET_VIRT_MASK MAKE_64BIT_MASK(0, TARGET_VIRT_ADDR_SPACE_BITS)


#define hwaddr uint64_t
#define HWADDR_MAX UINT64_MAX
#define HWADDR_FMT_plx "%016" PRIx64
#define HWADDR_PRId PRId64
#define HWADDR_PRIi PRIi64
#define HWADDR_PRIo PRIo64
#define HWADDR_PRIu PRIu64
#define HWADDR_PRIx PRIx64
#define HWADDR_PRIX PRIX64

typedef struct MemMapEntry {
    hwaddr base;
    hwaddr size;
} MemMapEntry;
typedef uint64_t vaddr;

#ifdef TARGET_ABI32
#define TARGET_ABI_BITS 32
#else
#define TARGET_ABI_BITS TARGET_LONG_BITS
#endif


#ifndef ABI_SHORT_ALIGNMENT
#define ABI_SHORT_ALIGNMENT 2
#endif
#ifndef ABI_INT_ALIGNMENT
#define ABI_INT_ALIGNMENT 4
#endif
#ifndef ABI_LONG_ALIGNMENT
#define ABI_LONG_ALIGNMENT (TARGET_ABI_BITS / 8)
#endif
#ifndef ABI_LLONG_ALIGNMENT
#define ABI_LLONG_ALIGNMENT 8
#endif

#ifdef TARGET_ABI32
typedef uint32_t abi_ulong __attribute__((aligned(ABI_LONG_ALIGNMENT)));
typedef int32_t abi_long __attribute__((aligned(ABI_LONG_ALIGNMENT)));
#define TARGET_ABI_FMT_lx "%08x"
#define TARGET_ABI_FMT_ld "%d"
#define TARGET_ABI_FMT_lu "%u"

// static inline abi_ulong tswapal(abi_ulong v)
// {
//     return tswap32(v);
// }

#else
typedef target_ulong abi_ulong __attribute__((aligned(ABI_LONG_ALIGNMENT)));
typedef target_long abi_long __attribute__((aligned(ABI_LONG_ALIGNMENT)));
#define TARGET_ABI_FMT_lx TARGET_FMT_lx
#define TARGET_ABI_FMT_ld TARGET_FMT_ld
#define TARGET_ABI_FMT_lu TARGET_FMT_lu
/* for consistency, define ABI32 too */
#if TARGET_ABI_BITS == 32
#define TARGET_ABI32 1
#endif

// static inline abi_ulong tswapal(abi_ulong v)
// {
//     return tswapl(v);
// }

#endif

extern const char * const regnames[32];
extern const char * const fregnames[32];

/*
 * RISC-V-specific extra insn start words:
 * 1: Original instruction opcode
 * 2: more information about instruction
 */
#define TARGET_INSN_START_EXTRA_WORDS 2
/*
 * b0: Whether a instruction always raise a store AMO or not.
 */
#define RISCV_UW2_ALWAYS_STORE_AMO 1

#define RV(x) ((target_ulong)1 << (x - 'A'))

/*
 * Update misa_bits[], misa_ext_info_arr[] and misa_ext_cfgs[]
 * when adding new MISA bits here.
 */
#define RVI RV('I')
#define RVE RV('E') /* E and I are mutually exclusive */
#define RVM RV('M')
#define RVA RV('A')
#define RVF RV('F')
#define RVD RV('D')
#define RVV RV('V')
#define RVC RV('C')
#define RVS RV('S')
#define RVU RV('U')
#define RVH RV('H')
#define RVJ RV('J')
#define RVG RV('G')
#define RVB RV('B')

/* Privileged specification version */
#define PRIV_VER_1_10_0_STR "v1.10.0"
#define PRIV_VER_1_11_0_STR "v1.11.0"
#define PRIV_VER_1_12_0_STR "v1.12.0"
#define PRIV_VER_1_13_0_STR "v1.13.0"
enum {
    PRIV_VERSION_1_10_0 = 0,
    PRIV_VERSION_1_11_0,
    PRIV_VERSION_1_12_0,
    PRIV_VERSION_1_13_0,

    PRIV_VERSION_LATEST = PRIV_VERSION_1_13_0,
};

#define VEXT_VERSION_1_00_0 0x00010000
#define VEXT_VER_1_00_0_STR "v1.0"

enum {
    TRANSLATE_SUCCESS,
    TRANSLATE_FAIL,
    TRANSLATE_PMP_FAIL,
    TRANSLATE_G_STAGE_FAIL
};

/* Extension context status */
typedef enum {
    EXT_STATUS_DISABLED = 0,
    EXT_STATUS_INITIAL,
    EXT_STATUS_CLEAN,
    EXT_STATUS_DIRTY,
} RISCVExtStatus;

#define RISCV_IMPLIED_EXTS_RULE_END -1

#define MMU_USER_IDX 3

#define MAX_RISCV_PMPS (16)

#if !defined(CONFIG_USER_ONLY)
#include "pmp.h"
#include "debug.h"
#endif

#define RV_VLEN_MAX 1024
#define RV_MAX_MHPMEVENTS 32
#define RV_MAX_MHPMCOUNTERS 32

FIELD(VTYPE, VLMUL, 0, 3)
FIELD(VTYPE, VSEW, 3, 3)
FIELD(VTYPE, VTA, 6, 1)
FIELD(VTYPE, VMA, 7, 1)
FIELD(VTYPE, VEDIV, 8, 2)
FIELD(VTYPE, RESERVED, 10, sizeof(target_ulong) * 8 - 11)

typedef struct PMUCTRState {
    /* Current value of a counter */
    target_ulong mhpmcounter_val;
    /* Current value of a counter in RV32 */
    target_ulong mhpmcounterh_val;
    /* Snapshot values of counter */
    target_ulong mhpmcounter_prev;
    /* Snapshort value of a counter in RV32 */
    target_ulong mhpmcounterh_prev;
    /* Value beyond UINT32_MAX/UINT64_MAX before overflow interrupt trigger */
    target_ulong irq_overflow_left;
} PMUCTRState;

typedef struct PMUFixedCtrState {
        /* Track cycle and icount for each privilege mode */
        uint64_t counter[4];
        uint64_t counter_prev[4];
        /* Track cycle and icount for each privilege mode when V = 1*/
        uint64_t counter_virt[2];
        uint64_t counter_virt_prev[2];
} PMUFixedCtrState;

typedef struct CPUArchState {


    target_ulong gpr[32];
    target_ulong gprh[32]; /* 64 top bits of the 128-bit registers */

    /* vector coprocessor state. */
    uint64_t vreg[32 * RV_VLEN_MAX / 64] QEMU_ALIGNED(16);
    target_ulong vxrm;
    target_ulong vxsat;
    target_ulong vl;
    target_ulong vstart;
    target_ulong vtype;
    bool vill;

    target_ulong pc;
    target_ulong load_res;
    target_ulong load_val;

    /* Floating-Point state */
    uint64_t fpr[32]; /* assume both F and D extensions */
    target_ulong frm;
    float_status fp_status;

    target_ulong badaddr;
    target_ulong bins;

    target_ulong guest_phys_fault_addr;

    target_ulong priv_ver;
    target_ulong vext_ver;

    /* RISCVMXL, but uint32_t for vmstate migration */
    uint32_t misa_mxl;      /* current mxl */
    uint32_t misa_ext;      /* current extensions */
    uint32_t misa_ext_mask; /* max ext for this cpu */
    uint32_t xl;            /* current xlen */

    /* 128-bit helpers upper part return value */
    target_ulong retxh;

    target_ulong jvt;

    /* elp state for zicfilp extension */
    bool      elp;
    /* shadow stack register for zicfiss extension */
    target_ulong ssp;
    /* env place holder for extra word 2 during unwind */
    target_ulong excp_uw2;
    /* sw check code for sw check exception */
    target_ulong sw_check_code;
#ifdef CONFIG_USER_ONLY
    uint32_t elf_flags;
#endif

    target_ulong priv;
    /* CSRs for execution environment configuration */
    uint64_t menvcfg;
    target_ulong senvcfg;

#ifndef CONFIG_USER_ONLY
    /* This contains QEMU specific information about the virt state. */
    bool virt_enabled;
    target_ulong geilen;
    uint64_t resetvec;

    target_ulong mhartid;
    /*
     * For RV32 this is 32-bit mstatus and 32-bit mstatush.
     * For RV64 this is a 64-bit mstatus.
     */
    uint64_t mstatus;

    uint64_t mip;
    /*
     * MIP contains the software writable version of SEIP ORed with the
     * external interrupt value. The MIP register is always up-to-date.
     * To keep track of the current source, we also save booleans of the values
     * here.
     */
    bool external_seip;
    bool software_seip;

    uint64_t miclaim;

    uint64_t mie;
    uint64_t mideleg;

    /*
     * When mideleg[i]=0 and mvien[i]=1, sie[i] is no more
     * alias of mie[i] and needs to be maintained separately.
     */
    uint64_t sie;

    /*
     * When hideleg[i]=0 and hvien[i]=1, vsie[i] is no more
     * alias of sie[i] (mie[i]) and needs to be maintained separately.
     */
    uint64_t vsie;

    target_ulong satp;   /* since: priv-1.10.0 */
    target_ulong stval;
    target_ulong medeleg;

    target_ulong stvec;
    target_ulong sepc;
    target_ulong scause;

    target_ulong mtvec;
    target_ulong mepc;
    target_ulong mcause;
    target_ulong mtval;  /* since: priv-1.10.0 */

    /* Machine and Supervisor interrupt priorities */
    uint8_t miprio[64];
    uint8_t siprio[64];

    /* AIA CSRs */
    target_ulong miselect;
    target_ulong siselect;
    uint64_t mvien;
    uint64_t mvip;

    /* Hypervisor CSRs */
    target_ulong hstatus;
    target_ulong hedeleg;
    uint64_t hideleg;
    uint32_t hcounteren;
    target_ulong htval;
    target_ulong htinst;
    target_ulong hgatp;
    target_ulong hgeie;
    target_ulong hgeip;
    uint64_t htimedelta;
    uint64_t hvien;

    /*
     * Bits VSSIP, VSTIP and VSEIP in hvip are maintained in mip. Other bits
     * from 0:12 are reserved. Bits 13:63 are not aliased and must be separately
     * maintain in hvip.
     */
    uint64_t hvip;

    /* Hypervisor controlled virtual interrupt priorities */
    target_ulong hvictl;
    uint8_t hviprio[64];

    /* Upper 64-bits of 128-bit CSRs */
    uint64_t mscratchh;
    uint64_t sscratchh;

    /* Virtual CSRs */
    /*
     * For RV32 this is 32-bit vsstatus and 32-bit vsstatush.
     * For RV64 this is a 64-bit vsstatus.
     */
    uint64_t vsstatus;
    target_ulong vstvec;
    target_ulong vsscratch;
    target_ulong vsepc;
    target_ulong vscause;
    target_ulong vstval;
    target_ulong vsatp;

    /* AIA VS-mode CSRs */
    target_ulong vsiselect;

    target_ulong mtval2;
    target_ulong mtinst;

    /* HS Backup CSRs */
    target_ulong stvec_hs;
    target_ulong sscratch_hs;
    target_ulong sepc_hs;
    target_ulong scause_hs;
    target_ulong stval_hs;
    target_ulong satp_hs;
    uint64_t mstatus_hs;

    /*
     * Signals whether the current exception occurred with two-stage address
     * translation active.
     */
    bool two_stage_lookup;
    /*
     * Signals whether the current exception occurred while doing two-stage
     * address translation for the VS-stage page table walk.
     */
    bool two_stage_indirect_lookup;

    uint32_t scounteren;
    uint32_t mcounteren;

    uint32_t mcountinhibit;

    /* PMU cycle & instret privilege mode filtering */
    target_ulong mcyclecfg;
    target_ulong mcyclecfgh;
    target_ulong minstretcfg;
    target_ulong minstretcfgh;

    /* PMU counter state */
    PMUCTRState pmu_ctrs[RV_MAX_MHPMCOUNTERS];

    /* PMU event selector configured values. First three are unused */
    target_ulong mhpmevent_val[RV_MAX_MHPMEVENTS];

    /* PMU event selector configured values for RV32 */
    target_ulong mhpmeventh_val[RV_MAX_MHPMEVENTS];

    PMUFixedCtrState pmu_fixed_ctrs[2];

    target_ulong sscratch;
    target_ulong mscratch;

    /* Sstc CSRs */
    uint64_t stimecmp;

    uint64_t vstimecmp;

    /* physical memory protection */
    pmp_table_t pmp_state;
    target_ulong mseccfg;

    /* trigger module */
    target_ulong trigger_cur;
    target_ulong tdata1[RV_MAX_TRIGGERS];
    target_ulong tdata2[RV_MAX_TRIGGERS];
    target_ulong tdata3[RV_MAX_TRIGGERS];
    target_ulong mcontext;
    struct CPUBreakpoint *cpu_breakpoint[RV_MAX_TRIGGERS];
    struct CPUWatchpoint *cpu_watchpoint[RV_MAX_TRIGGERS];
    void *itrigger_timer[RV_MAX_TRIGGERS];
    int64_t last_icount;
    bool itrigger_enabled;

    /* machine specific rdtime callback */
    uint64_t (*rdtime_fn)(void *);
    void *rdtime_fn_arg;

    /* machine specific AIA ireg read-modify-write callback */
#define AIA_MAKE_IREG(__isel, __priv, __virt, __vgein, __xlen) \
    ((((__xlen) & 0xff) << 24) | \
     (((__vgein) & 0x3f) << 20) | \
     (((__virt) & 0x1) << 18) | \
     (((__priv) & 0x3) << 16) | \
     (__isel & 0xffff))
#define AIA_IREG_ISEL(__ireg)                  ((__ireg) & 0xffff)
#define AIA_IREG_PRIV(__ireg)                  (((__ireg) >> 16) & 0x3)
#define AIA_IREG_VIRT(__ireg)                  (((__ireg) >> 18) & 0x1)
#define AIA_IREG_VGEIN(__ireg)                 (((__ireg) >> 20) & 0x3f)
#define AIA_IREG_XLEN(__ireg)                  (((__ireg) >> 24) & 0xff)
    int (*aia_ireg_rmw_fn[4])(void *arg, target_ulong reg,
        target_ulong *val, target_ulong new_val, target_ulong write_mask);
    void *aia_ireg_rmw_fn_arg[4];

    /* True if in debugger mode.  */
    bool debugger;

    /*
     * CSRs for PointerMasking extension
     */
    target_ulong mmte;
    target_ulong mpmmask;
    target_ulong mpmbase;
    target_ulong spmmask;
    target_ulong spmbase;
    target_ulong upmmask;
    target_ulong upmbase;

    uint64_t mstateen[SMSTATEEN_MAX_COUNT];
    uint64_t hstateen[SMSTATEEN_MAX_COUNT];
    uint64_t sstateen[SMSTATEEN_MAX_COUNT];
    uint64_t henvcfg;
#endif
    target_ulong cur_pmmask;
    target_ulong cur_pmbase;

    /* Fields from here on are preserved across CPU reset. */
    void *stimer; /* Internal timer for S-mode interrupt */
    void *vstimer; /* Internal timer for VS-mode interrupt */
    bool vstime_irq;

    hwaddr kernel_addr;
    hwaddr fdt_addr;

#ifdef CONFIG_KVM
    /* kvm timer */
    bool kvm_timer_dirty;
    uint64_t kvm_timer_time;
    uint64_t kvm_timer_compare;
    uint64_t kvm_timer_state;
    uint64_t kvm_timer_frequency;
#endif /* CONFIG_KVM */

    target_ulong ol;
    target_ulong address_xl;
    target_ulong zero;


    uint64_t prev_pc;
    uint32_t insn;
    uint32_t cur_insn_len;
    #ifdef RECORD_BRNACH
    /* for branch */
    bool     taken;
    uint64_t target;
    #endif

    uint64_t clint_mtimecmp;


#ifndef CONFIG_USER_ONLY
    bool load_elf;
    uint64_t elf_address;
#endif

    INSCache inscache[IC_NUM];
    uint64_t icount;
    uint64_t ecount;
    uint64_t syscall_count;
    uint64_t ic_hit_count;
    uint64_t ecounter[0x100];
    uint64_t tlbr_count;
    uint64_t irq_count;
#if defined(CONFIG_PERF)
#define COUNTER_INST_FP                    0
#define COUNTER_INST_VEC                   1
#define COUNTER_INST_BRANCH                3
#define COUNTER_INST_BRANCH_DIRECT_JUMP    4
#define COUNTER_INST_BRANCH_INDIRECT       5
#define COUNTER_INST_BRANCH_CONDITIONAL    6
#define COUNTER_INST_BRANCH_DIRECT_CALL    7
#define COUNTER_INST_BRANCH_INDIRECT_CALL  8
#define COUNTER_INST_BRANCH_RETURN         9
#define COUNTER_INST_LOAD                  10
#define COUNTER_INST_STORE                 11
#define COUNTER_INST_CROSS_PAGE_LOAD       12
#define COUNTER_INST_CROSS_PAGE_STORE      13
#define COUNTER_INST                       14

#define COUNTER_MAX 0x100

    uint64_t perf_counter[4][COUNTER_MAX];

#define PERF_INC(event) do {++env->perf_counter[env->priv][event];} while (0);

#else
#define PERF_INC(event) ;
#endif
    int64_t timer_counter;
    timer_t timerid;
    volatile sig_atomic_t timer_int;
} CPURISCVState;

typedef CPURISCVState CPUArchState;


typedef struct CPUState {
    int dummy;
    int cpu_index;
    void* as;
    int exception_index;
    CPURISCVState *env;
    sigjmp_buf jmp_env;
    int halted;
    void* watchpoint_hit;
    char neg_align[-sizeof(CPUNegativeOffsetState) % 16] QEMU_ALIGNED(16);
    CPUNegativeOffsetState neg;
}CPUState;

typedef struct RISCVCPU {
    CPUState parent_obj;
    CPURISCVState env;
    RISCVCPUConfig cfg;
    uint32_t pmu_avail_ctrs;
}RISCVCPU;

typedef RISCVCPU ArchCPU;

#define CPU(obj) ((CPUState *)(obj))

#define RISCV_CPU(obj) ((RISCVCPU *)(obj))

int cpu_exec(CPUState *cpu);

/* Validate correct placement of CPUArchState. */
QEMU_BUILD_BUG_ON(offsetof(ArchCPU, parent_obj) != 0);
QEMU_BUILD_BUG_ON(offsetof(ArchCPU, env) != sizeof(CPUState));

/**
 * env_archcpu(env)
 * @env: The architecture environment
 *
 * Return the ArchCPU associated with the environment.
 */
static inline ArchCPU *env_archcpu(CPUArchState *env)
{
    return (void *)env - sizeof(CPUState);
}

/**
 * env_cpu(env)
 * @env: The architecture environment
 *
 * Return the CPUState associated with the environment.
 */
static inline CPUState *env_cpu(CPUArchState *env)
{
    return (void *)env - sizeof(CPUState);
}

static inline CPUArchState *cpu_env(CPUState *cpu)
{
    /* We validate that CPUArchState follows CPUState in cpu-all.h. */
    return (CPUArchState *)(cpu + 1);
}

#include "internals.h"

int check_get_physical_address(CPURISCVState *env, hwaddr *physical,
                                int *prot, target_ulong address,
                                MMUAccessType access_type, int mmu_idx);

int probe_get_physical_address(CPURISCVState *env, hwaddr *physical,
                                int *prot, target_ulong address,
                                MMUAccessType access_type);
bool interpreter(CPURISCVState *env, uint32_t insn, INSCache* ic);


#ifdef CONFIG_USER_ONLY
#define ram (char*)0
#else
extern char* ram;
#endif

static inline uint64_t ram_ldb(hwaddr addr) {return (int64_t)*(int8_t*)(ram + addr);}
static inline uint64_t ram_ldh(hwaddr addr) {return (int64_t)*(int16_t*)(ram + addr);}
static inline uint64_t ram_ldw(hwaddr addr) {return (int64_t)*(int32_t*)(ram + addr);}
static inline uint64_t ram_ldd(hwaddr addr) {return (int64_t)*(int64_t*)(ram + addr);}
static inline uint64_t ram_ldub(hwaddr addr) {return *(uint8_t*)(ram + addr);}
static inline uint64_t ram_lduh(hwaddr addr) {return *(uint16_t*)(ram + addr);}
static inline uint64_t ram_lduw(hwaddr addr) {return *(uint32_t*)(ram + addr);}
static inline uint64_t ram_ldud(hwaddr addr) {return *(uint64_t*)(ram + addr);}
// static inline Int128  ram_ld128(hwaddr addr) {return *(Int128*)(ram + addr);}
// static inline VReg    ram_ld256(hwaddr addr) {return *(VReg*)(ram + addr);}
static inline void ram_stb(hwaddr addr, uint64_t data) {*(uint8_t*)(ram + addr) = data;}
static inline void ram_sth(hwaddr addr, uint64_t data) {*(uint16_t*)(ram + addr) = data;}
static inline void ram_stw(hwaddr addr, uint64_t data) {*(uint32_t*)(ram + addr) = data;}
static inline void ram_std(hwaddr addr, uint64_t data) {*(uint64_t*)(ram + addr) = data;}
// static inline void ram_st128(hwaddr addr, Int128 data) {*(Int128*)(ram + addr) = data;}
// static inline void ram_st256(hwaddr addr, VReg data) {*(VReg*)(ram + addr) = data;}
#ifndef CONFIG_USER_ONLY
bool addr_in_ram(hwaddr pa);
static inline bool ram_ldub_check(hwaddr addr, uint8_t *data) {if (!addr_in_ram(addr)){*data = 0xff; return false;} *data = *(uint8_t*)(ram + addr); return true;}
#endif

G_NORETURN void cpu_loop_exit(CPUState *cpu);

static inline target_ulong ldq_phys(void* as, hwaddr addr) {
    return ram_ldd(addr);
}

void helper_ertn(CPURISCVState *env);

void G_NORETURN do_raise_exception(CPURISCVState *env, uint32_t exception, uintptr_t pc);

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
static inline void cpu_put_ic(CPURISCVState *env, bool (*trans_func)(void*, void*), void* arg, int insn) {
    INSCache* ic = &env->inscache[IC_INDEX(env->pc)];
    ic->trans_func = trans_func;
    int* args = (int*)arg;
    ic->arg[0] = args[0];
    ic->arg[1] = args[1];
    ic->arg[2] = args[2];
    ic->arg[3] = args[3];
    ic->insn = insn;
    // fprintf(stderr, "put %p %lx %08x %d %d %d %d\n", ic->trans_func, env->pc, ic->insn, ic->arg[0], ic->arg[1], ic->arg[2], ic->arg[3]);
}
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

static inline INSCache* cpu_get_ic(CPURISCVState *env, int insn) {
    INSCache* ic = &env->inscache[IC_INDEX(env->pc)];
    if (likely(ic->insn == insn)) {
    // fprintf(stderr, "get %p %lx %08x %d %d %d %d\n", ic->trans_func, env->pc, ic->insn, ic->arg[0], ic->arg[1], ic->arg[2], ic->arg[3]);
        ++ env->ic_hit_count;
        return ic;
    } else {
        return NULL;
    }
}

#include "helper.h"

extern __thread CPURISCVState *current_env;
int exec_env(CPURISCVState *env);
extern bool determined;
extern bool serial_plus;

void loongarch_cpu_set_irq(void *opaque, int irq, int level);

#if defined(CONFIG_PERF)
void perf_report(CPURISCVState *env, FILE*);
#endif
void dump_exec_info(CPURISCVState *env, FILE*);

static inline void cpu_settimer(CPURISCVState* env, uint64_t ns) {
    struct itimerspec its;
    its.it_value.tv_sec = ns / 1000000000;
    its.it_value.tv_nsec = ns % 1000000000;
    its.it_interval.tv_sec = 0;
    its.it_interval.tv_nsec = 0;
    lsassert(timer_settime(env->timerid, 0, &its, NULL) == 0);
}

static inline void cpu_disable_timer(CPURISCVState* env) {
    struct itimerspec its;
    its.it_value.tv_sec = 0;
    its.it_value.tv_nsec = 0;
    its.it_interval.tv_sec = 0;
    its.it_interval.tv_nsec = 0;
    lsassert(timer_settime(env->timerid, 0, &its, NULL) == 0);
}

void get_dir_base_width(CPURISCVState *env, uint64_t *dir_base,
                               uint64_t *dir_width, target_ulong level);
#if !defined(CONFIG_USER_ONLY)
void do_io_st(hwaddr ha, uint64_t data, int size);
uint64_t do_io_ld(hwaddr ha, int size);

bool loongarch_cpu_has_irq(CPUArchState *env);
void loongarch_cpu_check_irq(CPURISCVState *env);
#endif

void cpu_set_feature(CPURISCVState* env, char* feature, int value);

#if defined(CONFIG_PLUGIN)
extern la_emu_plugin_ops plugin_ops;
    #define PLUGIN_CALL(func, ...)           \
        do {                                 \
            if (plugin_ops.func) {            \
                plugin_ops.func(__VA_ARGS__); \
            }                                \
        } while(0)
#else
    #define PLUGIN_CALL(func, ...) do {} while(0)
#endif


static inline void laemu_exit(int64_t status) {
    PLUGIN_CALL(emu_stop);
    exit(status);
}

void cpu_reset(CPUState* cs);
void show_register(CPUArchState *env);
void show_register_fpr(CPUArchState *env);
void cpu_set_pc(CPUArchState *cs, vaddr value);

static inline void loongarch_la464_initfn(void* env) {}

static inline void cpu_set_sp(CPUArchState *env, target_ulong sp) {
    env->gpr[2] = sp;
}

G_NORETURN void riscv_raise_exception(CPURISCVState *env,
                                      uint32_t exception, uintptr_t pc);

void loongarch_cpu_check_irq(CPUArchState *env);

static inline int riscv_has_ext(CPURISCVState *env, target_ulong ext)
{
    return (env->misa_ext & ext) != 0;
}

#ifdef TARGET_RISCV32
#define riscv_cpu_mxl(env)  ((void)(env), MXL_RV32)
#else
static inline RISCVMXL riscv_cpu_mxl(CPURISCVState *env)
{
    return env->misa_mxl;
}
#endif
#define riscv_cpu_mxl_bits(env) (1UL << (4 + riscv_cpu_mxl(env)))

static inline const RISCVCPUConfig *riscv_cpu_cfg(CPURISCVState *env)
{
    return &env_archcpu(env)->cfg;
}

#if !defined(CONFIG_USER_ONLY)
static inline int cpu_address_mode(CPURISCVState *env)
{
    int mode = env->priv;

    if (mode == PRV_M && get_field(env->mstatus, MSTATUS_MPRV)) {
        mode = get_field(env->mstatus, MSTATUS_MPP);
    }
    return mode;
}

static inline RISCVMXL cpu_get_xl(CPURISCVState *env, target_ulong mode)
{
    RISCVMXL xl = env->misa_mxl;
    /*
     * When emulating a 32-bit-only cpu, use RV32.
     * When emulating a 64-bit cpu, and MXL has been reduced to RV32,
     * MSTATUSH doesn't have UXL/SXL, therefore XLEN cannot be widened
     * back to RV64 for lower privs.
     */
    if (xl != MXL_RV32) {
        switch (mode) {
        case PRV_M:
            break;
        case PRV_U:
            xl = get_field(env->mstatus, MSTATUS64_UXL);
            break;
        default: /* PRV_S */
            xl = get_field(env->mstatus, MSTATUS64_SXL);
            break;
        }
    }
    return xl;
}
#endif

#if defined(TARGET_RISCV32)
#define cpu_recompute_xl(env)  ((void)(env), MXL_RV32)
#else
static inline RISCVMXL cpu_recompute_xl(CPURISCVState *env)
{
#if !defined(CONFIG_USER_ONLY)
    return cpu_get_xl(env, env->priv);
#else
    return env->misa_mxl;
#endif
}
#endif

#if defined(TARGET_RISCV32)
#define cpu_address_xl(env)  ((void)(env), MXL_RV32)
#else
static inline RISCVMXL cpu_address_xl(CPURISCVState *env)
{
#ifdef CONFIG_USER_ONLY
    return env->xl;
#else
    int mode = cpu_address_mode(env);

    return cpu_get_xl(env, mode);
#endif
}
#endif

static inline int riscv_cpu_xlen(CPURISCVState *env)
{
    return 16 << env->xl;
}

#ifdef TARGET_RISCV32
#define riscv_cpu_sxl(env)  ((void)(env), MXL_RV32)
#else
static inline RISCVMXL riscv_cpu_sxl(CPURISCVState *env)
{
#ifdef CONFIG_USER_ONLY
    return env->misa_mxl;
#else
    if (env->misa_mxl != MXL_RV32) {
        return get_field(env->mstatus, MSTATUS64_SXL);
    }
#endif
    return MXL_RV32;
}
#endif

static inline bool icount_enabled() {return false;}

const char *riscv_cpu_get_trap_name(target_ulong cause, bool async);
#define cpu_do_interrupt loongarch_cpu_do_interrupt

void loongarch_cpu_do_interrupt(CPUState *cpu);

static inline int riscv_env_mmu_index(CPURISCVState *env, bool ifetch)
{
#ifdef CONFIG_USER_ONLY
    return 0;
#else
    bool virt = env->virt_enabled;
    int mode = env->priv;

    /* All priv -> mmu_idx mapping are here */
    if (!ifetch) {
        uint64_t status = env->mstatus;

        if (mode == PRV_M && get_field(status, MSTATUS_MPRV)) {
            mode = get_field(env->mstatus, MSTATUS_MPP);
            virt = get_field(env->mstatus, MSTATUS_MPV) &&
                   (mode != PRV_M);
            if (virt) {
                status = env->vsstatus;
            }
        }
        if (mode == PRV_S && get_field(status, MSTATUS_SUM)) {
            mode = MMUIdx_S_SUM;
        }
    }

    return mode | (virt ? MMU_2STAGE_BIT : 0);
#endif
}

int get_physical_address(CPURISCVState *env, hwaddr *physical,
                                int *ret_prot, vaddr addr,
                                target_ulong *fault_pte_addr,
                                int access_type, int mmu_idx,
                                bool first_stage, bool two_stage,
                                bool is_debug, bool is_probe);

RISCVException riscv_csrr(CPURISCVState *env, int csrno,
                          target_ulong *ret_value);
RISCVException riscv_csrrw(CPURISCVState *env, int csrno,
                           target_ulong *ret_value,
                           target_ulong new_value, target_ulong write_mask);
RISCVException riscv_csrrw_debug(CPURISCVState *env, int csrno,
                                 target_ulong *ret_value,
                                 target_ulong new_value,
                                 target_ulong write_mask);

static inline void riscv_csr_write(CPURISCVState *env, int csrno,
                                   target_ulong val)
{
    riscv_csrrw(env, csrno, NULL, val, MAKE_64BIT_MASK(0, TARGET_LONG_BITS));
}

static inline target_ulong riscv_csr_read(CPURISCVState *env, int csrno)
{
    target_ulong val = 0;
    riscv_csrrw(env, csrno, &val, 0, 0);
    return val;
}

typedef RISCVException (*riscv_csr_predicate_fn)(CPURISCVState *env,
                                                 int csrno);
typedef RISCVException (*riscv_csr_read_fn)(CPURISCVState *env, int csrno,
                                            target_ulong *ret_value);
typedef RISCVException (*riscv_csr_write_fn)(CPURISCVState *env, int csrno,
                                             target_ulong new_value);
typedef RISCVException (*riscv_csr_op_fn)(CPURISCVState *env, int csrno,
                                          target_ulong *ret_value,
                                          target_ulong new_value,
                                          target_ulong write_mask);

RISCVException riscv_csrr_i128(CPURISCVState *env, int csrno,
                               Int128 *ret_value);
RISCVException riscv_csrrw_i128(CPURISCVState *env, int csrno,
                                Int128 *ret_value,
                                Int128 new_value, Int128 write_mask);

typedef RISCVException (*riscv_csr_read128_fn)(CPURISCVState *env, int csrno,
                                               Int128 *ret_value);
typedef RISCVException (*riscv_csr_write128_fn)(CPURISCVState *env, int csrno,
                                             Int128 new_value);

typedef struct {
    const char *name;
    riscv_csr_predicate_fn predicate;
    riscv_csr_read_fn read;
    riscv_csr_write_fn write;
    riscv_csr_op_fn op;
    riscv_csr_read128_fn read128;
    riscv_csr_write128_fn write128;
    /* The default priv spec version should be PRIV_VERSION_1_10_0 (i.e 0) */
    uint32_t min_priv_ver;
} riscv_csr_operations;

/* CSR function table constants */
enum {
    CSR_TABLE_SIZE = 0x1000
};

void riscv_cpu_set_mode(CPURISCVState *env, target_ulong newpriv, bool virt_en);

bool cpu_get_fcfien(CPURISCVState *env);

/* CSR function table */
extern riscv_csr_operations csr_ops[CSR_TABLE_SIZE];
extern const bool valid_vm_1_10_32[], valid_vm_1_10_64[];

bool cpu_get_fcfien(CPURISCVState *env);
bool cpu_get_bcfien(CPURISCVState *env);
int riscv_cpu_mirq_pending(CPURISCVState *env);
int riscv_cpu_sirq_pending(CPURISCVState *env);
int riscv_cpu_vsirq_pending(CPURISCVState *env);
bool riscv_cpu_fp_enabled(CPURISCVState *env);
bool riscv_cpu_vector_enabled(CPURISCVState *env);

target_ulong riscv_cpu_get_fflags(CPURISCVState *env);
void riscv_cpu_set_fflags(CPURISCVState *env, target_ulong);
uint64_t riscv_cpu_update_mip(CPURISCVState *env, uint64_t mask,
                              uint64_t value);
void riscv_cpu_update_mask(CPURISCVState *env);

static inline int64_t get_clock(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static inline int64_t cpu_get_host_ticks(void)
{
    return get_clock();
}

void riscv_cpu_interrupt(CPURISCVState *env);

uint8_t riscv_cpu_default_priority(int irq);
uint64_t riscv_cpu_all_pending(CPURISCVState *env);

#define BOOL_TO_MASK(x) (-!!(x)) /* helper for riscv_cpu_update_mip value */

int riscv_cpu_hviprio_index2irq(int index, int *out_irq, int *out_rdzero);

void helper_raise_exception(CPURISCVState *env, uint32_t exception);


bool riscv_cpu_is_32bit(RISCVCPU *cpu);

void tlb_flush(CPUState *cpu);
static inline void cpu_clear_tc(CPURISCVState *env) {tlb_flush(env_cpu(env));}

void riscv_cpu_set_rdtime_fn(CPURISCVState *env, uint64_t (*fn)(void *),
                             void *arg);

void riscv_timer_write_timecmp(CPURISCVState *env, void *timer,
                               uint64_t timecmp, uint64_t delta,
                               uint32_t timer_irq);




void tlb_set_page(CPUState *cpu, vaddr addr,
                  hwaddr paddr, int prot,
                  int mmu_idx, uint64_t size);


uint64_t clint_ioport_read(void *opaque, uint64_t addr, unsigned size);
void clint_ioport_write(void *opaque, uint64_t addr, uint64_t val, unsigned size);


bool riscv_cpu_exec_interrupt(CPUState *cs, int interrupt_request);

bool riscv_cpu_tlb_fill(CPUState *cs, vaddr address, int size,
                        MMUAccessType access_type, int mmu_idx,
                        bool probe, uintptr_t retaddr);

static inline void cpu_loop_exit_restore(CPUState *cpu, uintptr_t pc)
{
    // if (pc) {
    //     cpu_restore_state(cpu, pc);
    // }
    cpu_loop_exit(cpu);
}

static inline bool tlb_get_page(CPUState *cpu, vaddr addr, hwaddr *paddr, MMUAccessType access_type, int mmu_idx)
{
    int index = TC_INDEX(addr);
    CPUTLBEntry *entry = &cpu->neg.tlb[mmu_idx][index];
    if (entry->addr_idx[access_type] == (addr & TARGET_PAGE_MASK)) {
        *paddr = entry->addend;
        return true;
    } else {
        *paddr = 0xbadbadbadbad;
        return false;
    }
}

static inline bool tlb_get_addr(CPUState *cpu, vaddr addr, hwaddr *paddr, MMUAccessType access_type, int mmu_idx)
{
    if (tlb_get_page(cpu, addr, paddr, access_type, mmu_idx)) {
        *paddr |= (addr & (TARGET_PAGE_SIZE - 1));
        return true;
    } else {
        return false;
    }
}

static inline hwaddr trans_pa(CPURISCVState *env, uint64_t addr, MMUAccessType access_type) {
    hwaddr ha;
    CPUState *cs = env_cpu(env);
    int mmu_idx = riscv_env_mmu_index(env, access_type == MMU_INST_FETCH);
    if (!tlb_get_addr(cs, addr, &ha, access_type, mmu_idx)) {
        int r = riscv_cpu_tlb_fill(cs, addr, 0, access_type, mmu_idx, false, 0);
        lsassert(r);
        lsassert(tlb_get_addr(cs, addr, &ha, access_type, mmu_idx));
    }
    // else {
    //     hwaddr tlb_ha_check;
    //     int r = riscv_cpu_tlb_fill(cs, addr, 0, access_type, mmu_idx, false, 0);
    //     lsassert(r);
    //     lsassert(tlb_get_addr(cs, addr, &tlb_ha_check, access_type, mmu_idx));
    //     lsassertm(ha == tlb_ha_check, "va:%lx, ha:%lx, ha:%lx\n", addr, ha, tlb_ha_check);
    // }
    return ha;
}

static inline long long la_get_tval(CPURISCVState *env){
    if (determined) {
        return current_env->icount / TIME_SCALE;
    } else {
        return nano_second() / TIMER_PERIOD;
    }
}

#endif /* RISCV_CPU_H */
