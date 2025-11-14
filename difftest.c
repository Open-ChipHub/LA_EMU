#define _GNU_SOURCE

#include "qemu/osdep.h"
#include "util.h"

#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/mman.h>

#include <elf.h>

#include "sizes.h"
#include "cpu.h"
#include "internals.h"

#define DUT_TO_REF 0
#define REF_TO_DUT 1

extern char* ram;

extern int64_t singlestep;
extern bool fastforward;
extern int check_level;
extern bool determined;
extern bool ptw_hw_setVD;
extern bool hw_ptw;
extern store_queue_t store_queue;

extern int exec_env(CPULoongArchState *env);
extern void cpu_reset(CPUState* cs);
extern uint64_t helper_read_csr(CPULoongArchState *env, int csr_index);

extern const char* const csrnames[];


CPULoongArchState *inst_env;


static inline uint8_t* guest_to_host(uint64_t guest_paddr)
{
    return (uint8_t*)(ram + guest_paddr);
}


static void difftest_init_ram(void *host_ram)
{
    lsassert(ram == NULL);
    ram = host_ram;
    lsassert(ram != NULL);
}


void loong64_difftest_init(void *host_ram)
{
    LoongArchCPU* cpu = aligned_alloc(64, sizeof(LoongArchCPU));
    memset(cpu, 0, sizeof(LoongArchCPU));
    CPUState *cs = CPU(cpu);
    CPULoongArchState* env = &cpu->env;
    inst_env = aligned_alloc(64, sizeof(CPULoongArchState));
    cs->env = env;
    cpu_reset(cs);
    loongarch_core_initfn(env);
    cpu_clear_tc(env);
    env->timer_counter = INT64_MAX;

    current_env = env;

    if (!host_ram)
        perror("Host RAM Pointer is NULL!");

    difftest_init_ram(host_ram);

    check_level |= CPU_CHECK_TLB_MHIT;
    determined = true;

    helper_invtlb_all(env);
    ptw_hw_setVD = false;
    hw_ptw = true;
}

void loong64_difftest_exec(uint64_t n, bool fast)
{
    singlestep = n;
    fastforward = fast;
    exec_env(current_env);
}

extern uint32_t fetch(CPULoongArchState *env, INSCache** ic);
uint32_t loong64_difftest_get_inst_by_env(void) {
    INSCache* ic;
    uint32_t insn;
    insn = fetch(current_env, &ic);
    return insn;
}
int debugs = 0;
uint32_t loong64_difftest_get_inst_by_pc(uint64_t pc) {
    uint32_t insn;
    hwaddr ha;
    int prot;
    // fetch will change current_env
    memcpy(inst_env, current_env, sizeof(CPULoongArchState));
    // CPULoongArchState *env =  current_env;

    if (probe_get_physical_address(inst_env, &ha, &prot, pc, MMU_INST_FETCH)== -1) {
        // printf("EMU: Fetch Instruction Address Error!\n");
        return 0;
    }
    insn = ram_lduw(ha);
    return insn;
}

void loong64_difftest_cosim_end()
{
    printf("Emulator Simulation Over!");
    exit(0);
}

void loong64_difftest_store_commit(uint64_t addr, uint64_t data) {

}

static const char* reg_name[] = {
        "r0",      "ra",     "tp",      "sp",      "a0",      "a1",     "a2",        "a3",        "a4",      "a5",
        "a6",      "a7",     "t0",      "t1",      "t2",      "t3",     "t4",        "t5",        "t6",      "t7",
        "t8",      " x",     "fp",      "s0",      "s1",      "s2",     "s3",        "s4",        "s5",      "s6",
        "s7",      "s8",
        "crmd",    "prmd",   "euen",    "ecfg",    "era",     "badv",   "eentry",    "tlbidx",    "tlbehi",  "tlbelo0",
        "tlbelo1", "asid",   "pgdl",    "pgdh",    "save0",   "save1",  "save2",     "save3",     "tid",     "tcfg",
        "tval",    "llbctl", "tlbrentry", "dmw0",  "dmw1",    "estat",   "cur_pc"
};


void loong64_isa_reg_display() {
    CPULoongArchState *env = current_env;
    int i;

    // Display general purpose registers
    for (i = 0; i < 32; i++) {
        printf("%s(r%2d): 0x%016lx ", reg_name[i], i, env->gpr[i]);
        if (i % 4 == 3) printf("\n");
    }

    // Display PC
    printf("pc: 0x%016lx\n", env->pc);
    printf("prev_pc : 0x%016lx\n", env->prev_pc);
    printf("CRMD: 0x%016lx,    PRMD: 0x%016lx,   EUEN: 0x%016lx\n", env->CSR_CRMD, env->CSR_PRMD, env->CSR_EUEN);
    printf("ECFG: 0x%016lx,   ESTAT: 0x%016lx,    ERA: 0x%016lx\n", env->CSR_ECFG, env->CSR_ESTAT, env->CSR_ERA);
    printf("Badv: 0x%016lx,  EEntry: 0x%016lx, LLBCTL: 0x%016lx\n", env->CSR_BADV, env->CSR_EENTRY, env->CSR_LLBCTL);
    printf("cpu.ll_bit: %lu\n", env->CSR_LLBCTL & 0x1);
    printf("INDEX: 0x%016lx, TLBEHI: 0x%016lx, TLBELO0: 0x%08lx, TLBELO1: 0x%08lx\n", env->CSR_TLBIDX, env->CSR_TLBEHI, env->CSR_TLBELO0, env->CSR_TLBELO1);
    printf("ASID: 0x%016lx, TLBRENTRY: 0x%016lx, DMW0: 0x%08lx, DMW1: 0x%08lx\n", env->CSR_ASID, env->CSR_TLBRENTRY, env->CSR_DMW[0], env->CSR_DMW[1]);
    printf("*******************************************************************************\n");
}


struct la64_timer {
    // for stable_counter
    uint64_t counter_id;
    uint64_t stable_timer;
    // for TVAL csr
    uint64_t time_val;
};

void loong64_difftest_timercpy(void* dut_buf, bool direction) {
    CPULoongArchState *env =  current_env;
    struct la64_timer *timer = dut_buf;

    if (direction == DUT_TO_REF) {
        env->timer = timer->stable_timer;
        env->CSR_TVAL = timer->time_val;
        env->CSR_TID = timer->counter_id;
    } else {
        timer->stable_timer = env->icount;
        timer->time_val = env->CSR_TVAL;
        timer->counter_id = env->CSR_TID;
    }
}

uint64_t loong64_difftest_get_cur_pc(void) {
    return current_env->pc;
}

uint64_t loong64_difftest_get_prev_pc(void) {
    return current_env->prev_pc;
}

void loong64_difftest_estat_sync(uint64_t index, uint64_t mask) {

}

void loong64_difftest_set_reset_pc(uint64_t reset_pc) {
    current_env->pc = reset_pc;
}

void loongarch_cpu_do_interrupt(CPUState *cs);
void loong64_difftest_raise_trap(int is_interrupt, uint64_t is, uint64_t ecode) {
    CPULoongArchState *env =  current_env;
    CPUState* cs = env_cpu(env);
    if (is_interrupt) {
        env->CSR_ESTAT = FIELD_DP64(env->CSR_ESTAT, CSR_ESTAT, IS, (is & 0x1FFF));
        env->CSR_ESTAT = FIELD_DP64(env->CSR_ESTAT, CSR_ESTAT, ECODE, ecode & 0x3f);
        env->CSR_ESTAT = FIELD_DP64(env->CSR_ESTAT, CSR_ESTAT, ESUBCODE, (ecode >> 6) & 0x1ff);
    }
    if (unlikely(loongarch_cpu_has_irq(env))) {
        cs->exception_index = EXCCODE_INT;
        loongarch_cpu_do_interrupt(cs);
        return;
    } else if ((is == 0) && (ecode != 0)) {
        cs->exception_index = ecode;
        loongarch_cpu_do_interrupt(cs);
        return;
    }
}

static inline void difftest_cpy_helper(void* ref_buf, void* dut_buf, size_t n, bool direction)
{
    if (direction == DUT_TO_REF) {
        memcpy(ref_buf, dut_buf, n);
    } else {
        memcpy(dut_buf, ref_buf, n);
    }
}

void loong64_difftest_memcpy(uint64_t guest_paddr, void* dut_buf, size_t n, bool direction)
{
    void* ref_buf = (void*)guest_to_host(guest_paddr);
    difftest_cpy_helper(ref_buf, dut_buf, n, direction);
}

static inline void difftest_helper_get_fpr(void* buf) {
    uint64_t *fpr_buf = (uint64_t *) buf;
    fpr_buf[0] = current_env->fpr[0].vreg.UD[0];
    fpr_buf[1] = current_env->fpr[1].vreg.UD[0];
    fpr_buf[2] = current_env->fpr[2].vreg.UD[0];
    fpr_buf[3] = current_env->fpr[3].vreg.UD[0];
    fpr_buf[4] = current_env->fpr[4].vreg.UD[0];
    fpr_buf[5] = current_env->fpr[5].vreg.UD[0];
    fpr_buf[6] = current_env->fpr[6].vreg.UD[0];
    fpr_buf[7] = current_env->fpr[7].vreg.UD[0];
    fpr_buf[8] = current_env->fpr[8].vreg.UD[0];
    fpr_buf[9] = current_env->fpr[9].vreg.UD[0];
    fpr_buf[10] = current_env->fpr[10].vreg.UD[0];
    fpr_buf[11] = current_env->fpr[11].vreg.UD[0];
    fpr_buf[12] = current_env->fpr[12].vreg.UD[0];
    fpr_buf[13] = current_env->fpr[13].vreg.UD[0];
    fpr_buf[14] = current_env->fpr[14].vreg.UD[0];
    fpr_buf[15] = current_env->fpr[15].vreg.UD[0];
    fpr_buf[16] = current_env->fpr[16].vreg.UD[0];
    fpr_buf[17] = current_env->fpr[17].vreg.UD[0];
    fpr_buf[18] = current_env->fpr[18].vreg.UD[0];
    fpr_buf[19] = current_env->fpr[19].vreg.UD[0];
    fpr_buf[20] = current_env->fpr[20].vreg.UD[0];
    fpr_buf[21] = current_env->fpr[21].vreg.UD[0];
    fpr_buf[22] = current_env->fpr[22].vreg.UD[0];
    fpr_buf[23] = current_env->fpr[23].vreg.UD[0];
    fpr_buf[24] = current_env->fpr[24].vreg.UD[0];
    fpr_buf[25] = current_env->fpr[25].vreg.UD[0];
    fpr_buf[26] = current_env->fpr[26].vreg.UD[0];
    fpr_buf[27] = current_env->fpr[27].vreg.UD[0];
    fpr_buf[28] = current_env->fpr[28].vreg.UD[0];
    fpr_buf[29] = current_env->fpr[29].vreg.UD[0];
    fpr_buf[30] = current_env->fpr[30].vreg.UD[0];
    fpr_buf[31] = current_env->fpr[31].vreg.UD[0];

    uint64_t fccr = 0;
    fccr |= current_env->cf[0] << 0;
    fccr |= current_env->cf[1] << 1;
    fccr |= current_env->cf[2] << 2;
    fccr |= current_env->cf[3] << 3;
    fccr |= current_env->cf[4] << 4;
    fccr |= current_env->cf[5] << 5;
    fccr |= current_env->cf[6] << 6;
    fccr |= current_env->cf[7] << 7;
    fpr_buf[32] = fccr;
    fpr_buf[33] = current_env->fcsr0;
}

static inline void difftest_helper_set_fpr(void* buf) {
    uint64_t *fpr_buf = (uint64_t *) buf;
    current_env->fpr[0].vreg.UD[0] = fpr_buf[0];
    current_env->fpr[1].vreg.UD[0] = fpr_buf[1];
    current_env->fpr[2].vreg.UD[0] = fpr_buf[2];
    current_env->fpr[3].vreg.UD[0] = fpr_buf[3];
    current_env->fpr[4].vreg.UD[0] = fpr_buf[4];
    current_env->fpr[5].vreg.UD[0] = fpr_buf[5];
    current_env->fpr[6].vreg.UD[0] = fpr_buf[6];
    current_env->fpr[7].vreg.UD[0] = fpr_buf[7];
    current_env->fpr[8].vreg.UD[0] = fpr_buf[8];
    current_env->fpr[9].vreg.UD[0] = fpr_buf[9];
    current_env->fpr[10].vreg.UD[0] = fpr_buf[10];
    current_env->fpr[11].vreg.UD[0] = fpr_buf[11];
    current_env->fpr[12].vreg.UD[0] = fpr_buf[12];
    current_env->fpr[13].vreg.UD[0] = fpr_buf[13];
    current_env->fpr[14].vreg.UD[0] = fpr_buf[14];
    current_env->fpr[15].vreg.UD[0] = fpr_buf[15];
    current_env->fpr[16].vreg.UD[0] = fpr_buf[16];
    current_env->fpr[17].vreg.UD[0] = fpr_buf[17];
    current_env->fpr[18].vreg.UD[0] = fpr_buf[18];
    current_env->fpr[19].vreg.UD[0] = fpr_buf[19];
    current_env->fpr[20].vreg.UD[0] = fpr_buf[20];
    current_env->fpr[21].vreg.UD[0] = fpr_buf[21];
    current_env->fpr[22].vreg.UD[0] = fpr_buf[22];
    current_env->fpr[23].vreg.UD[0] = fpr_buf[23];
    current_env->fpr[24].vreg.UD[0] = fpr_buf[24];
    current_env->fpr[25].vreg.UD[0] = fpr_buf[25];
    current_env->fpr[26].vreg.UD[0] = fpr_buf[26];
    current_env->fpr[27].vreg.UD[0] = fpr_buf[27];
    current_env->fpr[28].vreg.UD[0] = fpr_buf[28];
    current_env->fpr[29].vreg.UD[0] = fpr_buf[29];
    current_env->fpr[30].vreg.UD[0] = fpr_buf[30];
    current_env->fpr[31].vreg.UD[0] = fpr_buf[31];

    uint64_t fccr = fpr_buf[32];    
    current_env->cf[0] = (fccr >> 0) & 0x1;
    current_env->cf[1] = (fccr >> 1) & 0x1;
    current_env->cf[2] = (fccr >> 2) & 0x1;
    current_env->cf[3] = (fccr >> 3) & 0x1;
    current_env->cf[4] = (fccr >> 4) & 0x1;
    current_env->cf[5] = (fccr >> 5) & 0x1;
    current_env->cf[6] = (fccr >> 6) & 0x1;
    current_env->cf[7] = (fccr >> 7) & 0x1;

    current_env->fcsr0 = fpr_buf[33];
}


void loong64_difftest_gprcpy(void* dut_buf, bool direction, int reg_type)
{
    if (reg_type == 0) {
        void *gpr_base_addr = (void *) (&current_env->gpr);
        size_t gpr_size = sizeof(current_env->gpr[0]) * 32;
        difftest_cpy_helper(gpr_base_addr, dut_buf, gpr_size, direction);
    } else if (reg_type == 1) {
        if (direction == REF_TO_DUT)
        {
            difftest_helper_get_fpr(dut_buf);
        } else {
            difftest_helper_set_fpr(dut_buf);
        }
    } else { // all 
        void *gpr_base_addr = (void *) (&current_env->gpr);
        size_t gpr_size = sizeof(current_env->gpr[0]) * 32;
        difftest_cpy_helper(gpr_base_addr, dut_buf, gpr_size, direction);
        dut_buf = dut_buf + gpr_size;
        if (direction == REF_TO_DUT)
        {
            difftest_helper_get_fpr(dut_buf);
        } else {
            difftest_helper_set_fpr(dut_buf);
        }
    }
}

/*
    crmd;
    prmd;
    euen;
    ecfg;
    era, 
    badv, 
    eentry;
    tlbidx, 
    tlbehi, 
    tlbelo0, 
    tlbelo1;
    asid, 
    pgdl, 
    pgdh;
    save0, 
    save1, 
    save2, 
    save3;
    tid, 
    tcfg, 
    tval; // ticlr;
    llbctl, 
    tlbrentry, 
    dmw0, 
    dmw1;
    estat;
    cur_pc;

*/

void loong64_difftest_get_csr(void* dut_buf) {
    uint64_t *csr_buf = (uint64_t *) dut_buf;
    csr_buf[0] = current_env->CSR_CRMD;
    csr_buf[1] = current_env->CSR_PRMD;
    csr_buf[2] = current_env->CSR_EUEN;
    csr_buf[3] = current_env->CSR_ECFG;
    csr_buf[4] = current_env->CSR_ERA;
    csr_buf[5] = current_env->CSR_BADV;
    csr_buf[6] = current_env->CSR_EENTRY;
    // tlbidx, 
    // tlbehi, 
    // tlbelo0, 
    // tlbelo1;
    csr_buf[11] = current_env->CSR_ASID;
    csr_buf[12] = current_env->CSR_PGDL;
    csr_buf[13] = current_env->CSR_PGDH;
    csr_buf[14] = current_env->CSR_SAVE[0];
    csr_buf[15] = current_env->CSR_SAVE[1];
    csr_buf[16] = current_env->CSR_SAVE[2];
    csr_buf[17] = current_env->CSR_SAVE[3];
    csr_buf[18] = current_env->CSR_TID;
    csr_buf[19] = current_env->CSR_TCFG;
    csr_buf[20] = current_env->CSR_TVAL;
    // llbctl, 
    // tlbrentry, 
    csr_buf[23] = current_env->CSR_DMW[0];
    csr_buf[24] = current_env->CSR_DMW[1];
    csr_buf[25] = current_env->CSR_ESTAT;
    csr_buf[26] = current_env->prev_pc;
}


void loong64_difftest_set_csr(void* dut_buf) {
    uint64_t *csr_buf = (uint64_t *) dut_buf;
    current_env->CSR_CRMD = csr_buf[0];
    current_env->CSR_PRMD = csr_buf[1];
    current_env->CSR_EUEN = csr_buf[2];
    current_env->CSR_ECFG = csr_buf[3];
    current_env->CSR_ERA = csr_buf[4];
    current_env->CSR_BADV = csr_buf[5];
    current_env->CSR_EENTRY = csr_buf[6];
    // tlbidx, 
    // tlbehi, 
    // tlbelo0, 
    // tlbelo1;
    current_env->CSR_ASID = csr_buf[11];
    current_env->CSR_PGDL = csr_buf[12];
    current_env->CSR_PGDH = csr_buf[13];
    current_env->CSR_SAVE[0] = csr_buf[14];
    current_env->CSR_SAVE[1] = csr_buf[15];
    current_env->CSR_SAVE[2] = csr_buf[16];
    current_env->CSR_SAVE[3] = csr_buf[17];
    current_env->CSR_TID = csr_buf[18];
    current_env->CSR_TCFG = csr_buf[19];
    current_env->CSR_TVAL = csr_buf[20];
    // llbctl, 
    // tlbrentry,
    current_env->CSR_DMW[0] = csr_buf[23];
    current_env->CSR_DMW[1] = csr_buf[24];
    current_env->CSR_ESTAT = csr_buf[25];
    current_env->pc = csr_buf[26];
}

void loong64_difftest_csrcpy(void* dut_buf, bool direction) {
    if (direction == REF_TO_DUT) {
        loong64_difftest_get_csr(dut_buf);
    } else {
        loong64_difftest_set_csr(dut_buf);
    }
}

void loong64_difftest_get_gpr(void* dut_buf)
{
    void* gpr_base_addr = (void*)(&current_env->gpr);
    size_t gpr_size = sizeof(current_env->gpr[0]) * 32;

    difftest_cpy_helper(gpr_base_addr, dut_buf, gpr_size, 1);
}

void loong64_difftest_set_gpr(void* dut_buf)
{
    void* gpr_base_addr = (void*)(&current_env->gpr);
    size_t gpr_size = sizeof(current_env->gpr[0]) * 32;

    difftest_cpy_helper(gpr_base_addr, dut_buf, gpr_size, 1);
}


void loong64_difftest_get_gpr_idx(int gpr_idx, uint64_t* dut_buf)
{
    assert(gpr_idx >= 0 && gpr_idx <= 31);
    *dut_buf = current_env->gpr[gpr_idx];
}

void loong64_difftest_set_gpr_idx(int gpr_idx, uint64_t* dut_buf)
{
    assert(gpr_idx >= 0 && gpr_idx <= 31);
    current_env->gpr[gpr_idx] = *dut_buf;
}

void loong64_difftest_get_pc(uint64_t* dut_buf)
{
    *dut_buf = current_env->prev_pc;
}

void loong64_difftest_set_pc(uint64_t* dut_buf)
{
    current_env->pc = *dut_buf;
}

void loong64_difftest_get_current_inst(uint32_t* dut_buf)
{
    *dut_buf = current_env->insn;
}

void loong64_difftest_set_current_inst(uint32_t* dut_buf)
{
    current_env->insn = *dut_buf;
}

void loong64_difftest_get_fpr(void* dut_buf)
{
    void* fpr_base_addr = (void*)(&current_env->fpr);
    size_t fpr_size = sizeof(current_env->fpr[0]) * 32;

    difftest_cpy_helper(fpr_base_addr, dut_buf, fpr_size, 1);
}

void loong64_difftest_set_fpr(void* dut_buf)
{
    void* fpr_base_addr = (void*)(&current_env->fpr);
    size_t fpr_size = sizeof(current_env->fpr[0]) * 32;

    difftest_cpy_helper(fpr_base_addr, dut_buf, fpr_size, 0);
}


void loong64_difftest_get_fpr_idx(int fpr_idx, void* dut_buf)
{
    assert(fpr_idx >= 0 && fpr_idx <= 31);
    void* fpr_base_addr = (void*)(&current_env->fpr[fpr_idx]);
    size_t fpr_size = sizeof(current_env->fpr[0]);

    difftest_cpy_helper(fpr_base_addr, dut_buf, fpr_size, 1);
}

void loong64_difftest_set_fpr_idx(int fpr_idx, void* dut_buf)
{
    assert(fpr_idx >= 0 && fpr_idx <= 31);
    void* fpr_base_addr = (void*)(&current_env->fpr[fpr_idx]);
    size_t fpr_size = sizeof(current_env->fpr[0]);

    difftest_cpy_helper(fpr_base_addr, dut_buf, fpr_size, 0);
}

void loong64_difftest_get_fccr(void* dut_buf)
{
    void* cf_base_addr = &current_env->cf;
    size_t cf_size = sizeof(current_env->cf[0]) * 8;

    difftest_cpy_helper(cf_base_addr, dut_buf, cf_size, 1);
}

void loong64_difftest_set_fccr(void* dut_buf)
{
    void* cf_base_addr = &current_env->cf;
    size_t cf_size = sizeof(current_env->cf[0]) * 8;

    difftest_cpy_helper(cf_base_addr, dut_buf, cf_size, 0);
}


void loong64_difftest_get_fccr_idx(int cf_idx, uint8_t* dut_buf)
{
    assert(cf_idx >= 0 && cf_idx <= 7);
    *dut_buf = current_env->cf[cf_idx];
}

void loong64_difftest_set_fccr_idx(int cf_idx, uint8_t* dut_buf)
{
    assert(cf_idx >= 0 && cf_idx <= 7);
    current_env->cf[cf_idx] = *dut_buf;
}

void loong64_difftest_get_fcsr0(uint32_t* dut_buf)
{

    *dut_buf = current_env->fcsr0;
}

void loong64_difftest_set_fcsr0(uint32_t* dut_buf)
{
    current_env->fcsr0 = *dut_buf;
}

bool loong64_difftest_get_store(store_data_t* store_data) {
    if (store_queue.head == store_queue.tail) {
        return false;
    }
    store_data->paddr = store_queue.data[store_queue.head].paddr;
    store_data->data = store_queue.data[store_queue.head].data;
    store_data->mask = store_queue.data[store_queue.head].mask;

    store_queue.head = (store_queue.head + 1) & 0x3ff;
    return true;
}

#define CSR_CPY_HELPER(CSR)             \
    case LOONGARCH_CSR_ ## CSR : csr_base_addr = &(current_env->CSR_ ## CSR); break;

void loong64_difftest_csrcpy_idx(int csr_idx, uint64_t* dut_buf, uint64_t mask, bool direction)
{
    uint64_t csr_value;

    if (direction == REF_TO_DUT) {
        csr_value = helper_read_csr(current_env, csr_idx);
        *dut_buf = (*dut_buf & ~mask) | (csr_value & mask);
        return;
    }

    uint64_t* csr_base_addr = &csr_value;

    switch (csr_idx)
    {
    CSR_CPY_HELPER(CRMD)
    CSR_CPY_HELPER(PRMD)
    CSR_CPY_HELPER(EUEN)
    CSR_CPY_HELPER(MISC)
    CSR_CPY_HELPER(ECFG)
    CSR_CPY_HELPER(ESTAT)
    CSR_CPY_HELPER(ERA)
    CSR_CPY_HELPER(BADV)
    CSR_CPY_HELPER(BADI)
    CSR_CPY_HELPER(EENTRY)
    CSR_CPY_HELPER(TLBIDX)
    CSR_CPY_HELPER(TLBEHI)
    CSR_CPY_HELPER(TLBELO0)
    CSR_CPY_HELPER(TLBELO1)
    CSR_CPY_HELPER(ASID)
    CSR_CPY_HELPER(PGDL)
    CSR_CPY_HELPER(PGDH)
    CSR_CPY_HELPER(PGD)
    CSR_CPY_HELPER(PWCL)
    CSR_CPY_HELPER(PWCH)
    CSR_CPY_HELPER(STLBPS)
    CSR_CPY_HELPER(RVACFG)
    CSR_CPY_HELPER(CPUID)
    CSR_CPY_HELPER(PRCFG1)
    CSR_CPY_HELPER(PRCFG2)
    CSR_CPY_HELPER(PRCFG3)
    case LOONGARCH_CSR_SAVE(0) ... LOONGARCH_CSR_SAVE(15): csr_base_addr = &(current_env->CSR_SAVE[csr_idx - LOONGARCH_CSR_SAVE(0)]); break;
    CSR_CPY_HELPER(TID)
    CSR_CPY_HELPER(TCFG)
    CSR_CPY_HELPER(TVAL)
    CSR_CPY_HELPER(CNTC)
    CSR_CPY_HELPER(TICLR)
    CSR_CPY_HELPER(LLBCTL)
    CSR_CPY_HELPER(IMPCTL1)
    CSR_CPY_HELPER(IMPCTL2)
    CSR_CPY_HELPER(TLBRENTRY)
    CSR_CPY_HELPER(TLBRBADV)
    CSR_CPY_HELPER(TLBRERA)
    CSR_CPY_HELPER(TLBRSAVE)
    CSR_CPY_HELPER(TLBRELO0)
    CSR_CPY_HELPER(TLBRELO1)
    CSR_CPY_HELPER(TLBREHI)
    CSR_CPY_HELPER(TLBRPRMD)
    CSR_CPY_HELPER(MERRCTL)
    CSR_CPY_HELPER(MERRINFO1)
    CSR_CPY_HELPER(MERRINFO2)
    CSR_CPY_HELPER(MERRENTRY)
    CSR_CPY_HELPER(MERRERA)
    CSR_CPY_HELPER(MERRSAVE)
    CSR_CPY_HELPER(CTAG)
    case LOONGARCH_CSR_DMW(0) ... LOONGARCH_CSR_DMW(3): csr_base_addr = &(current_env->CSR_DMW[csr_idx - LOONGARCH_CSR_DMW(0)]); break;
    CSR_CPY_HELPER(DBG)
    CSR_CPY_HELPER(DERA)
    CSR_CPY_HELPER(DSAVE)
    default:
        fprintf(stderr, "NOT IMPLEMENTED %s %x\n", __func__, csr_idx);
        break;
    }

    *csr_base_addr = (*csr_base_addr & ~mask) | (*dut_buf & mask);

}

void loong64_difftest_get_csr_idx(int csr_idx, uint64_t* dut_buf, uint64_t mask) {
    loong64_difftest_csrcpy_idx(csr_idx, dut_buf, mask, 1);
}

void loong64_difftest_set_csr_idx(int csr_idx, uint64_t* dut_buf, uint64_t mask) {
    loong64_difftest_csrcpy_idx(csr_idx, dut_buf, mask, 0);
}

void loong64_trigger_syscall(void) {
    do_raise_exception(current_env, EXCCODE_SYS, 0);
}

void loong64_syscall_return_value_copy(uint64_t* dut_buf) {
    // a0 = r4
    current_env->gpr[4] = *dut_buf;
}

void loong64_difftest_tlbcpy()
{
    // TODO
}