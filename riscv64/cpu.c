#include "qemu/osdep.h"
#include "cpu.h"
#include <stdio.h>

const char * const riscv_int_regnames[] = {
    "x0/zero", "x1/ra",  "x2/sp",  "x3/gp",  "x4/tp",  "x5/t0",   "x6/t1",
    "x7/t2",   "x8/s0",  "x9/s1",  "x10/a0", "x11/a1", "x12/a2",  "x13/a3",
    "x14/a4",  "x15/a5", "x16/a6", "x17/a7", "x18/s2", "x19/s3",  "x20/s4",
    "x21/s5",  "x22/s6", "x23/s7", "x24/s8", "x25/s9", "x26/s10", "x27/s11",
    "x28/t3",  "x29/t4", "x30/t5", "x31/t6"
};

const char * const riscv_int_regnamesh[] = {
    "x0h/zeroh", "x1h/rah",  "x2h/sph",   "x3h/gph",   "x4h/tph",  "x5h/t0h",
    "x6h/t1h",   "x7h/t2h",  "x8h/s0h",   "x9h/s1h",   "x10h/a0h", "x11h/a1h",
    "x12h/a2h",  "x13h/a3h", "x14h/a4h",  "x15h/a5h",  "x16h/a6h", "x17h/a7h",
    "x18h/s2h",  "x19h/s3h", "x20h/s4h",  "x21h/s5h",  "x22h/s6h", "x23h/s7h",
    "x24h/s8h",  "x25h/s9h", "x26h/s10h", "x27h/s11h", "x28h/t3h", "x29h/t4h",
    "x30h/t5h",  "x31h/t6h"
};

const char * const riscv_fpr_regnames[] = {
    "f0/ft0",   "f1/ft1",  "f2/ft2",   "f3/ft3",   "f4/ft4",  "f5/ft5",
    "f6/ft6",   "f7/ft7",  "f8/fs0",   "f9/fs1",   "f10/fa0", "f11/fa1",
    "f12/fa2",  "f13/fa3", "f14/fa4",  "f15/fa5",  "f16/fa6", "f17/fa7",
    "f18/fs2",  "f19/fs3", "f20/fs4",  "f21/fs5",  "f22/fs6", "f23/fs7",
    "f24/fs8",  "f25/fs9", "f26/fs10", "f27/fs11", "f28/ft8", "f29/ft9",
    "f30/ft10", "f31/ft11"
};

// const char * const riscv_rvv_regnames[] = {
//   "v0",  "v1",  "v2",  "v3",  "v4",  "v5",  "v6",
//   "v7",  "v8",  "v9",  "v10", "v11", "v12", "v13",
//   "v14", "v15", "v16", "v17", "v18", "v19", "v20",
//   "v21", "v22", "v23", "v24", "v25", "v26", "v27",
//   "v28", "v29", "v30", "v31"
// };

static const char * const riscv_excp_names[] = {
    "misaligned_fetch",
    "fault_fetch",
    "illegal_instruction",
    "breakpoint",
    "misaligned_load",
    "fault_load",
    "misaligned_store",
    "fault_store",
    "user_ecall",
    "supervisor_ecall",
    "hypervisor_ecall",
    "machine_ecall",
    "exec_page_fault",
    "load_page_fault",
    "reserved",
    "store_page_fault",
    "reserved",
    "reserved",
    "reserved",
    "reserved",
    "guest_exec_page_fault",
    "guest_load_page_fault",
    "reserved",
    "guest_store_page_fault",
};

static const char * const riscv_intr_names[] = {
    "u_software",
    "s_software",
    "vs_software",
    "m_software",
    "u_timer",
    "s_timer",
    "vs_timer",
    "m_timer",
    "u_external",
    "s_external",
    "vs_external",
    "m_external",
    "reserved",
    "reserved",
    "reserved",
    "reserved"
};

const char *riscv_cpu_get_trap_name(target_ulong cause, bool async)
{
    if (async) {
        return (cause < ARRAY_SIZE(riscv_intr_names)) ?
               riscv_intr_names[cause] : "(unknown)";
    } else {
        return (cause < ARRAY_SIZE(riscv_excp_names)) ?
               riscv_excp_names[cause] : "(unknown)";
    }
}

#define GETBIT(__a, __index) ((__a >> __index) & 1)
#define GETBITS(__a, __index, __len) ((__a >> __index) & ((1 << __len) - 1))

__attribute__((noinline)) void show_register(CPURISCVState *env) {
    int i;
    fprintf(stderr, " %s " TARGET_FMT_lx "\n", "pc      ", env->pc);
    for (i = 0; i < 32; i++) {
        fprintf(stderr, " %-8s " TARGET_FMT_lx,
                     riscv_int_regnames[i], env->gpr[i]);
        if ((i & 3) == 3) {
            fprintf(stderr, "\n");
        }
    }
}

// static void dump_vzoui(int fcsr) {
//     fprintf(stderr, "%c%c%c%c%c", GETBIT(fcsr, 4) ? 'V' : '-',  GETBIT(fcsr, 3) ? 'Z' : '-',  GETBIT(fcsr, 2) ? 'O' : '-',  GETBIT(fcsr, 1) ? 'U' : '-',  GETBIT(fcsr, 0) ? 'I' : '-');
// }

// static void dump_fcsr(int fcsr) {
//     int rm = (fcsr >> 8) & 0x3;
//     static const char* rm_mode[4] = {
//         "RNE",
//         "RZ",
//         "RP",
//         "RM",
//     };
//     // printf("    Enables:V:%d, Z:%d, O:%d, U:%d, I:%d\n", GETBIT(fcsr, 4), GETBIT(fcsr, 3), GETBIT(fcsr, 2), GETBIT(fcsr, 1), GETBIT(fcsr, 0));
//     // printf("    RM     :%d(%s)\n", rm, rm_mode[rm]);
//     // printf("    Flags  :V:%d, Z:%d, O:%d, U:%d, I:%d\n", GETBIT(fcsr, 20), GETBIT(fcsr, 19), GETBIT(fcsr, 18), GETBIT(fcsr, 17), GETBIT(fcsr, 16));
//     // printf("    Cause  :V:%d, Z:%d, O:%d, U:%d, I:%d\n", GETBIT(fcsr, 28), GETBIT(fcsr, 27), GETBIT(fcsr, 26), GETBIT(fcsr, 25), GETBIT(fcsr, 24));

//     fprintf(stderr, "RM:%d(%s)", rm, rm_mode[rm]);
//     fprintf(stderr, ",Enables:");dump_vzoui(GETBITS(fcsr, 0, 5));
//     fprintf(stderr, ",Flags:");dump_vzoui(GETBITS(fcsr, 16, 5));
//     fprintf(stderr, ",Cause:");dump_vzoui(GETBITS(fcsr, 24, 5));
//     fprintf(stderr, "\n");
// }

__attribute__((noinline)) void show_register_fpr(CPURISCVState *env) {
    for (int i = 0; i <32; i++) {
        fprintf(stderr, "f%02d   {f = 0x%08x, d = 0x%016lx} {f = %.6f\t, d = %.12f\t}\n",
            i, (uint32_t)env->fpr[i], env->fpr[i], 0.0,0.0);
            // i, (uint32_t)env->fpr[i], env->fpr[i], *(float*)&env->fpr[i], *(double*)&env->fpr[i]);
    }
    fprintf(stderr, "\n");
    // fprintf(stderr, "fcsr:%08x\n", env->fcsr0);
    // dump_fcsr(env->fcsr0);
}

void cpu_set_pc(CPUArchState *env, vaddr value)
{
    if (env->xl == MXL_RV32) {
        env->pc = (int32_t)value;
    } else {
        env->pc = value;
    }
}
#ifndef CONFIG_USER_ONLY
static uint64_t cpu_riscv_read_rtc(void *opaque)
{
    CPURISCVState *env = opaque;
    uint64_t r = la_get_tval(env);
    qemu_log_mask(CPU_LOG_TIMER, "read_rtc:%lu\n", r);
    return r;
}
#endif

void cpu_reset(CPUState* cs) {
#ifndef CONFIG_USER_ONLY
    uint8_t iprio;
    int i, irq, rdzero;
#endif
    RISCVCPU *cpu = RISCV_CPU(cs);
    cpu->cfg.ext_zicntr = true;
    cpu->cfg.ext_zifencei = true;
    cpu->cfg.ext_zicsr = true;
    CPURISCVState *env = cpu_env(cs);
    env->xl = MXL_RV64;
    env->ol = MXL_RV64;
    env->address_xl = MXL_RV64;
    env->priv = PRV_M;
#ifndef CONFIG_USER_ONLY
    env->clint_mtimecmp = -1;
    riscv_cpu_set_rdtime_fn(env, cpu_riscv_read_rtc, env);
    // riscv_cpu_satp_mode_finalize(cpu, NULL);
    cpu->cfg.satp_mode.map = -1;
    cpu->cfg.satp_mode.init = -1;
    cpu->cfg.satp_mode.supported = -1;
    cpu->cfg.mmu = true;
    cpu->cfg.pmp = true;
    env->misa_mxl = MXL_RV64;
    env->misa_ext = RVI | RVM | RVA | RVF | RVD | RVC | RVU | RVS;
    env->mstatus &= ~(MSTATUS_MIE | MSTATUS_MPRV);
    if (env->misa_mxl > MXL_RV32) {
        /*
         * The reset status of SXL/UXL is undefined, but mstatus is WARL
         * and we must ensure that the value after init is valid for read.
         */
        env->mstatus = set_field(env->mstatus, MSTATUS64_SXL, env->misa_mxl);
        env->mstatus = set_field(env->mstatus, MSTATUS64_UXL, env->misa_mxl);
        if (riscv_has_ext(env, RVH)) {
            env->vsstatus = set_field(env->vsstatus,
                                      MSTATUS64_SXL, env->misa_mxl);
            env->vsstatus = set_field(env->vsstatus,
                                      MSTATUS64_UXL, env->misa_mxl);
            env->mstatus_hs = set_field(env->mstatus_hs,
                                        MSTATUS64_SXL, env->misa_mxl);
            env->mstatus_hs = set_field(env->mstatus_hs,
                                        MSTATUS64_UXL, env->misa_mxl);
        }
    }
    env->mcause = 0;
    env->miclaim = MIP_SGEIP;
    env->pc = env->resetvec;
    env->bins = 0;
    env->two_stage_lookup = false;

    env->menvcfg = (cpu->cfg.ext_svpbmt ? MENVCFG_PBMTE : 0) |
                   (!cpu->cfg.ext_svade && cpu->cfg.ext_svadu ?
                    MENVCFG_ADUE : 0);
    env->henvcfg = 0;

    /* Initialized default priorities of local interrupts. */
    for (i = 0; i < ARRAY_SIZE(env->miprio); i++) {
        iprio = riscv_cpu_default_priority(i);
        env->miprio[i] = (i == IRQ_M_EXT) ? 0 : iprio;
        env->siprio[i] = (i == IRQ_S_EXT) ? 0 : iprio;
        env->hviprio[i] = 0;
    }
    i = 0;
    while (!riscv_cpu_hviprio_index2irq(i, &irq, &rdzero)) {
        if (!rdzero) {
            env->hviprio[irq] = env->miprio[irq];
        }
        i++;
    }
    /* mmte is supposed to have pm.current hardwired to 1 */
    env->mmte |= (EXT_STATUS_INITIAL | MMTE_M_PM_CURRENT);

    /*
     * Bits 10, 6, 2 and 12 of mideleg are read only 1 when the Hypervisor
     * extension is enabled.
     */
    if (riscv_has_ext(env, RVH)) {
        env->mideleg |= HS_MODE_INTERRUPTS;
    }

    /*
     * Clear mseccfg and unlock all the PMP entries upon reset.
     * This is allowed as per the priv and smepmp specifications
     * and is needed to clear stale entries across reboots.
     */
    if (riscv_cpu_cfg(env)->ext_smepmp) {
        env->mseccfg = 0;
    }

    pmp_unlock_entries(env);
#else
    env->priv = PRV_U;
    env->senvcfg = 0;
    env->menvcfg = 0;
#endif
}

void cpu_set_feature(CPURISCVState* env, char* feature, int value) {

}

void dump_exec_info(CPURISCVState *env, FILE* f) {
    fprintf(f, "icount:%ld ic_hit_count:%ld syscall_count:%ld ecount:%ld tlbr:%ld irq:%ld\n", env->icount, env->ic_hit_count, env->syscall_count, env->ecount, env->tlbr_count, env->irq_count);
}

G_NORETURN void riscv_raise_exception(CPURISCVState *env,
                                      uint32_t exception, uintptr_t pc) {
    CPUState *cs = env_cpu(env);
    cs->exception_index = exception;
    cpu_loop_exit(cs);
}

#ifndef CONFIG_USER_ONLY
void loongarch_cpu_set_irq(void *opaque, int irq, int level)
{
    RISCVCPU *cpu = RISCV_CPU(opaque);
    CPURISCVState *env = &cpu->env;
    env->irq_count ++;

    if (irq < IRQ_LOCAL_MAX) {
        switch (irq) {
        case IRQ_U_SOFT:
        case IRQ_S_SOFT:
        case IRQ_VS_SOFT:
        case IRQ_M_SOFT:
        case IRQ_U_TIMER:
        case IRQ_S_TIMER:
        case IRQ_VS_TIMER:
        case IRQ_M_TIMER:
        case IRQ_U_EXT:
        case IRQ_VS_EXT:
        case IRQ_M_EXT:
            // if (kvm_enabled()) {
            //     kvm_riscv_set_irq(cpu, irq, level);
            // } else {
                riscv_cpu_update_mip(env, 1 << irq, BOOL_TO_MASK(level));
            // }
             break;
        case IRQ_S_EXT:
            // if (kvm_enabled()) {
            //     kvm_riscv_set_irq(cpu, irq, level);
            // } else {
                env->external_seip = level;
                riscv_cpu_update_mip(env, 1 << irq,
                                     BOOL_TO_MASK(level | env->software_seip));
            // }
            break;
        default:
            g_assert_not_reached();
        }
    } else if (irq < (IRQ_LOCAL_MAX + IRQ_LOCAL_GUEST_MAX)) {
        /* Require H-extension for handling guest local interrupts */
        if (!riscv_has_ext(env, RVH)) {
            g_assert_not_reached();
        }

        /* Compute bit position in HGEIP CSR */
        irq = irq - IRQ_LOCAL_MAX + 1;
        if (env->geilen < irq) {
            g_assert_not_reached();
        }

        /* Update HGEIP CSR */
        env->hgeip &= ~((target_ulong)1 << irq);
        if (level) {
            env->hgeip |= (target_ulong)1 << irq;
        }

        /* Update mip.SGEIP bit */
        riscv_cpu_update_mip(env, MIP_SGEIP,
                             BOOL_TO_MASK(!!(env->hgeie & env->hgeip)));
    } else {
        g_assert_not_reached();
    }
}
#endif

bool riscv_cpu_is_32bit(RISCVCPU *cpu)
{
    return riscv_cpu_mxl(&cpu->env) == MXL_RV32;
}


/*
 * Called when timecmp is written to update the QEMU timer or immediately
 * trigger timer interrupt if mtimecmp <= current timer value.
 */
void riscv_timer_write_timecmp(CPURISCVState *env, void *timer,
                               uint64_t timecmp, uint64_t delta,
                               uint32_t timer_irq) {
                                lsassert(0);
}
#ifndef CONFIG_USER_ONLY
uint64_t clint_ioport_read(void *opaque, uint64_t addr, unsigned size) {
    lsassert(0);
}
void clint_ioport_write(void *opaque, uint64_t addr, uint64_t val, unsigned size) {
    CPURISCVState *env = opaque;
    if (addr == 0) {
        if (val) {
            printf("[clint] write addr:%lx val:%lx\n", addr, val);
            lsassert(0);
        } else {
            printf("[clint] write addr:%lx val:%lx\n", addr, val);
        }
    } else if (addr == 0X4000) {
        env->clint_mtimecmp = val;
        uint64_t mtime = la_get_tval(env);
        qemu_log_mask(CPU_LOG_TIMER, "[clint] pc:%lx ra:%lx mtimecmp_clk:%lx, mtime:%lx dt:%lx\n", env->pc, env->gpr[1], env->clint_mtimecmp, mtime, env->clint_mtimecmp - mtime);
        if (determined) {
            if (mtime < env->clint_mtimecmp) {
                loongarch_cpu_set_irq(env_cpu(env), IRQ_M_TIMER, 0);
            }
        } else {
            uint64_t mtimecmp_clk = env->clint_mtimecmp;
            if (mtime < mtimecmp_clk) {
                loongarch_cpu_set_irq(env_cpu(env), IRQ_M_TIMER, 0);
                uint64_t dt = (mtimecmp_clk - mtime) * TIMER_PERIOD;
                cpu_settimer(env, dt);
            } else {
                loongarch_cpu_set_irq(env_cpu(env), IRQ_M_TIMER, 1);
            }
        }
    } else {
        printf("write addr:%lx val:%lx\n", addr, val);
        lsassert(0);
    }
}
#endif

void tlb_set_page(CPUState *cpu, vaddr addr,
                  hwaddr paddr, int prot,
                  int mmu_idx, uint64_t size)
{
    lsassertm (size == 0 || size == TARGET_PAGE_SIZE, "size:%lx\n", size);
    lsassertm ((paddr & ~TARGET_PAGE_MASK) == 0, "paddr:%lx\n", paddr);
    lsassertm ((addr & ~TARGET_PAGE_MASK) == 0, "addr:%lx\n", addr);
    int index = TC_INDEX(addr);
    CPUTLBEntry *entry = &cpu->neg.tlb[mmu_idx][index];
    entry->addr_code = (prot & PAGE_EXEC) ? (addr & TARGET_PAGE_MASK) : -1;
    entry->addr_read = (prot & PAGE_READ) ? (addr & TARGET_PAGE_MASK) : -1;
    entry->addr_write = (prot & PAGE_WRITE) ? (addr & TARGET_PAGE_MASK) : -1;
    entry->addend = paddr;
    cpu_env(cpu)->tlbr_count ++;
}

void tlb_flush(CPUState *cpu)
{
    memset(cpu->neg.tlb, -1, sizeof(cpu->neg.tlb));
    // CPUArchState *env = cpu->env;
    // printf("tlb_flush:%lx\n", env->pc);
}

#ifdef CONFIG_USER_ONLY
void loongarch_cpu_do_interrupt(CPUState *cs) {
    lsassert(0);
}

#endif

#if defined(CONFIG_PERF)
void perf_report_plv(CPURISCVState *env, FILE* f, int plv, char* plv_name) {
    fprintf(f, "%s, COUNTER_INST:                  %20lu\n", plv_name, env->perf_counter[plv][COUNTER_INST]);
    fprintf(f, "%s, COUNTER_INST_BRANCH:           %20lu\n", plv_name, env->perf_counter[plv][COUNTER_INST_BRANCH]);
    fprintf(f, "%s, COUNTER_INST_LOAD:             %20lu\n", plv_name, env->perf_counter[plv][COUNTER_INST_LOAD]);
    fprintf(f, "%s, COUNTER_INST_STORE:            %20lu\n", plv_name, env->perf_counter[plv][COUNTER_INST_STORE]);
    fprintf(f, "%s, COUNTER_INST_FP:               %20lu\n", plv_name, env->perf_counter[plv][COUNTER_INST_FP]);
    fprintf(f, "%s, COUNTER_INST_VEC:              %20lu\n", plv_name, env->perf_counter[plv][COUNTER_INST_VEC]);
    fprintf(f, "%s, COUNTER_INST_CROSS_PAGE_LOAD:  %20lu\n", plv_name, env->perf_counter[plv][COUNTER_INST_CROSS_PAGE_LOAD]);
    fprintf(f, "%s, COUNTER_INST_CROSS_PAGE_STORE: %20lu\n", plv_name, env->perf_counter[plv][COUNTER_INST_CROSS_PAGE_STORE]);

}
void perf_report(CPURISCVState *env, FILE* f) {
    perf_report_plv(env, f, PRV_U, "user      ");
#if !defined(CONFIG_USER_ONLY)
    perf_report_plv(env, f, PRV_S, "supervisor");
    perf_report_plv(env, f, PRV_M, "machine   ");
#endif
}
#endif
