#include "qemu/osdep.h"
#include "cpu.h"
#include "internals.h"
#include "cpu-csr.h"
#include <stdio.h>

const char * const regnames[32] = {
    "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7",
    "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",
    "r16", "r17", "r18", "r19", "r20", "r21", "r22", "r23",
    "r24", "r25", "r26", "r27", "r28", "r29", "r30", "r31",
};

const char * const fregnames[32] = {
    "f0", "f1", "f2", "f3", "f4", "f5", "f6", "f7",
    "f8", "f9", "f10", "f11", "f12", "f13", "f14", "f15",
    "f16", "f17", "f18", "f19", "f20", "f21", "f22", "f23",
    "f24", "f25", "f26", "f27", "f28", "f29", "f30", "f31",
};

const char *const loongarch_r_alias[32] =
{
    "zer", "ra", "tp", "sp", "a0", "a1", "a2", "a3",
    "a4",   "a5", "a6", "a7", "t0", "t1", "t2", "t3",
    "t4",   "t5", "t6", "t7", "t8", "r21","fp", "s0",
    "s1",   "s2", "s3", "s4", "s5", "s6", "s7", "s8",
};

const char *const loongarch_f_alias[32] =
{
    "fa0", "fa1", "fa2",  "fa3",  "fa4",  "fa5",  "fa6",  "fa7",
    "ft0", "ft1", "ft2",  "ft3",  "ft4",  "ft5",  "ft6",  "ft7",
    "ft8", "ft9", "ft10", "ft11", "ft12", "ft13", "ft14", "ft15",
    "fs0", "fs1", "fs2",  "fs3",  "fs4",  "fs5",  "fs6",  "fs7",
};


#define CSRNAME_DECL(name)                  \
        [LOONGARCH_CSR_ ## name] = #name


const char* const csrnames[LOONGARCH_CSR_MAXADDR + 1] = {
    CSRNAME_DECL(CRMD),
    CSRNAME_DECL(PRMD),
    CSRNAME_DECL(EUEN),
    CSRNAME_DECL(MISC),
    CSRNAME_DECL(ECFG),
    CSRNAME_DECL(ESTAT),
    CSRNAME_DECL(ERA),
    CSRNAME_DECL(BADV),
    CSRNAME_DECL(BADI),
    CSRNAME_DECL(EENTRY),
    CSRNAME_DECL(TLBIDX),
    CSRNAME_DECL(TLBEHI),
    CSRNAME_DECL(TLBELO0),
    CSRNAME_DECL(TLBELO1),
    CSRNAME_DECL(ASID),
    CSRNAME_DECL(PGDL),
    CSRNAME_DECL(PGDH),
    CSRNAME_DECL(PGD),
    CSRNAME_DECL(PWCL),
    CSRNAME_DECL(PWCH),
    CSRNAME_DECL(STLBPS),
    CSRNAME_DECL(RVACFG),
    CSRNAME_DECL(CPUID),
    CSRNAME_DECL(PRCFG1),
    CSRNAME_DECL(PRCFG2),
    CSRNAME_DECL(PRCFG3),
    CSRNAME_DECL(SAVE(0)),
    CSRNAME_DECL(SAVE(1)),
    CSRNAME_DECL(SAVE(2)),
    CSRNAME_DECL(SAVE(3)),
    CSRNAME_DECL(SAVE(4)),
    CSRNAME_DECL(SAVE(5)),
    CSRNAME_DECL(SAVE(6)),
    CSRNAME_DECL(SAVE(7)),
    CSRNAME_DECL(SAVE(8)),
    CSRNAME_DECL(SAVE(9)),
    CSRNAME_DECL(SAVE(10)),
    CSRNAME_DECL(SAVE(11)),
    CSRNAME_DECL(SAVE(12)),
    CSRNAME_DECL(SAVE(13)),
    CSRNAME_DECL(SAVE(14)),
    CSRNAME_DECL(SAVE(15)),
    CSRNAME_DECL(TID),
    CSRNAME_DECL(TCFG),
    CSRNAME_DECL(TVAL),
    CSRNAME_DECL(CNTC),
    CSRNAME_DECL(TICLR),
    CSRNAME_DECL(LLBCTL),
    CSRNAME_DECL(IMPCTL1),
    CSRNAME_DECL(IMPCTL2),
    CSRNAME_DECL(TLBRENTRY),
    CSRNAME_DECL(TLBRBADV),
    CSRNAME_DECL(TLBRERA),
    CSRNAME_DECL(TLBRSAVE),
    CSRNAME_DECL(TLBRELO0),
    CSRNAME_DECL(TLBRELO1),
    CSRNAME_DECL(TLBREHI),
    CSRNAME_DECL(TLBRPRMD),
    CSRNAME_DECL(MERRCTL),
    CSRNAME_DECL(MERRINFO1),
    CSRNAME_DECL(MERRINFO2),
    CSRNAME_DECL(MERRENTRY),
    CSRNAME_DECL(MERRERA),
    CSRNAME_DECL(MERRSAVE),
    CSRNAME_DECL(CTAG),
    CSRNAME_DECL(DMW(0)),
    CSRNAME_DECL(DMW(1)),
    CSRNAME_DECL(DMW(2)),
    CSRNAME_DECL(DMW(3)),
    CSRNAME_DECL(DBG),
    CSRNAME_DECL(DERA),
    CSRNAME_DECL(DSAVE),
};

#if defined(CONFIG_PERF)
void perf_report_plv(CPULoongArchState *env, FILE* f, int plv, char* plv_name) {
    fprintf(f, "%s, COUNTER_INST:                  %20lu\n", plv_name, env->perf_counter[plv][COUNTER_INST]);
    fprintf(f, "%s, COUNTER_INST_BRANCH:           %20lu\n", plv_name, env->perf_counter[plv][COUNTER_INST_BRANCH]);
    fprintf(f, "%s, COUNTER_INST_LOAD:             %20lu\n", plv_name, env->perf_counter[plv][COUNTER_INST_LOAD]);
    fprintf(f, "%s, COUNTER_INST_STORE:            %20lu\n", plv_name, env->perf_counter[plv][COUNTER_INST_STORE]);
    fprintf(f, "%s, COUNTER_INST_FP:               %20lu\n", plv_name, env->perf_counter[plv][COUNTER_INST_FP]);
    fprintf(f, "%s, COUNTER_INST_LSX:              %20lu\n", plv_name, env->perf_counter[plv][COUNTER_INST_LSX]);
    fprintf(f, "%s, COUNTER_INST_LASX:             %20lu\n", plv_name, env->perf_counter[plv][COUNTER_INST_LASX]);
    fprintf(f, "%s, COUNTER_INST_LBT:              %20lu\n", plv_name, env->perf_counter[plv][COUNTER_INST_LBT]);
    fprintf(f, "%s, COUNTER_INST_CROSS_PAGE_LOAD:  %20lu\n", plv_name, env->perf_counter[plv][COUNTER_INST_CROSS_PAGE_LOAD]);
    fprintf(f, "%s, COUNTER_INST_CROSS_PAGE_STORE: %20lu\n", plv_name, env->perf_counter[plv][COUNTER_INST_CROSS_PAGE_STORE]);

}
void perf_report(CPULoongArchState *env, FILE* f) {
#if defined(CONFIG_USER_ONLY)
    perf_report_plv(env, f, 0, "user");
#else
    perf_report_plv(env, f, 0, "kern");
    perf_report_plv(env, f, 3, "user");
#endif
}
#endif

void dump_exec_info(CPULoongArchState *env, FILE* f) {
    fprintf(f, "icount:%ld ic_hit_count:%ld syscall_count:%ld ecount:%ld tlbr:%ld irq:%ld\n", env->icount, env->ic_hit_count, env->syscall_count, env->ecount, env->tlbr_count, env->irq_count);
#if !defined(CONFIG_USER_ONLY)
    fprintf(f, "PIL:  %10lu ", env->ecounter[EXCCODE_PIL]);
    fprintf(f, "PIS:  %10lu ", env->ecounter[EXCCODE_PIS]);
    fprintf(f, "PIF:  %10lu\n", env->ecounter[EXCCODE_PIF]);
    fprintf(f, "PME:  %10lu ", env->ecounter[EXCCODE_PME]);
    fprintf(f, "PNR:  %10lu ", env->ecounter[EXCCODE_PNR]);
    fprintf(f, "PNX:  %10lu ", env->ecounter[EXCCODE_PNX]);
    fprintf(f, "PPI:  %10lu\n", env->ecounter[EXCCODE_PPI]);
    fprintf(f, "ADEF: %10lu ", env->ecounter[EXCCODE_ADEF]);
    fprintf(f, "ADEM: %10lu ", env->ecounter[EXCCODE_ADEM]);
    fprintf(f, "ALE:  %10lu ", env->ecounter[EXCCODE_ALE]);
    fprintf(f, "BCE:  %10lu\n", env->ecounter[EXCCODE_BCE]);
    fprintf(f, "SYS:  %10lu ", env->ecounter[EXCCODE_SYS]);
    fprintf(f, "BRK:  %10lu ", env->ecounter[EXCCODE_BRK]);
    fprintf(f, "INE:  %10lu ", env->ecounter[EXCCODE_INE]);
    fprintf(f, "IPE:  %10lu\n", env->ecounter[EXCCODE_IPE]);
    fprintf(f, "FPD:  %10lu ", env->ecounter[EXCCODE_FPD]);
    fprintf(f, "SXD:  %10lu ", env->ecounter[EXCCODE_SXD]);
    fprintf(f, "ASXD: %10lu\n", env->ecounter[EXCCODE_ASXD]);
    fprintf(f, "FPE:  %10lu ", env->ecounter[EXCCODE_FPE]);
    fprintf(f, "VFPE: %10lu ", env->ecounter[EXCCODE_VFPE]);
    fprintf(f, "WPEF: %10lu ", env->ecounter[EXCCODE_WPEF]);
    fprintf(f, "WPEM: %10lu\n", env->ecounter[EXCCODE_WPEM]);
    fprintf(f, "BTD:  %10lu ", env->ecounter[EXCCODE_BTD]);
    fprintf(f, "BTE:  %10lu ", env->ecounter[EXCCODE_BTE]);
    fprintf(f, "DBP:  %10lu\n", env->ecounter[EXCCODE_DBP]);
#endif
}


void loongarch_la464_initfn(CPULoongArchState* env) {
    int i;

    for (i = 0; i < 21; i++) {
        env->cpucfg[i] = 0x0;
    }

    env->cpucfg[0] = 0x14c010;  /* PRID */

    uint32_t data = 0;
    data = FIELD_DP32(data, CPUCFG1, ARCH, 2);
    data = FIELD_DP32(data, CPUCFG1, PGMMU, 1);
    data = FIELD_DP32(data, CPUCFG1, IOCSR, 1);
    data = FIELD_DP32(data, CPUCFG1, PALEN, 0x2f);
    data = FIELD_DP32(data, CPUCFG1, VALEN, 0x2f);
    data = FIELD_DP32(data, CPUCFG1, UAL, 1);
    data = FIELD_DP32(data, CPUCFG1, RI, 1);
    data = FIELD_DP32(data, CPUCFG1, EP, 1);
    data = FIELD_DP32(data, CPUCFG1, RPLV, 1);
    data = FIELD_DP32(data, CPUCFG1, HP, 1);
    data = FIELD_DP32(data, CPUCFG1, IOCSR_BRD, 1);
    env->cpucfg[1] = data;

    data = 0;
    data = FIELD_DP32(data, CPUCFG2, FP, 1);
    data = FIELD_DP32(data, CPUCFG2, FP_SP, 1);
    data = FIELD_DP32(data, CPUCFG2, FP_DP, 1);
    data = FIELD_DP32(data, CPUCFG2, FP_VER, 1);
    data = FIELD_DP32(data, CPUCFG2, LSX, 1),
    data = FIELD_DP32(data, CPUCFG2, LASX, 1),
    data = FIELD_DP32(data, CPUCFG2, LLFTP, 1);
    data = FIELD_DP32(data, CPUCFG2, LLFTP_VER, 1);
    data = FIELD_DP32(data, CPUCFG2, LSPW, 1);
    data = FIELD_DP32(data, CPUCFG2, LAM, 1);
    env->cpucfg[2] = data;

    env->cpucfg[4] = 100 * 1000 * 1000; /* Crystal frequency */

    data = 0;
    data = FIELD_DP32(data, CPUCFG5, CC_MUL, 1);
    data = FIELD_DP32(data, CPUCFG5, CC_DIV, 1);
    env->cpucfg[5] = data;

    data = 0;
    data = FIELD_DP32(data, CPUCFG16, L1_IUPRE, 1);
    data = FIELD_DP32(data, CPUCFG16, L1_DPRE, 1);
    data = FIELD_DP32(data, CPUCFG16, L2_IUPRE, 1);
    data = FIELD_DP32(data, CPUCFG16, L2_IUUNIFY, 1);
    data = FIELD_DP32(data, CPUCFG16, L2_IUPRIV, 1);
    data = FIELD_DP32(data, CPUCFG16, L3_IUPRE, 1);
    data = FIELD_DP32(data, CPUCFG16, L3_IUUNIFY, 1);
    data = FIELD_DP32(data, CPUCFG16, L3_IUINCL, 1);
    env->cpucfg[16] = data;

    data = 0;
    data = FIELD_DP32(data, CPUCFG17, L1IU_WAYS, 3);
    data = FIELD_DP32(data, CPUCFG17, L1IU_SETS, 8);
    data = FIELD_DP32(data, CPUCFG17, L1IU_SIZE, 6);
    env->cpucfg[17] = data;

    data = 0;
    data = FIELD_DP32(data, CPUCFG18, L1D_WAYS, 3);
    data = FIELD_DP32(data, CPUCFG18, L1D_SETS, 8);
    data = FIELD_DP32(data, CPUCFG18, L1D_SIZE, 6);
    env->cpucfg[18] = data;

    data = 0;
    data = FIELD_DP32(data, CPUCFG19, L2IU_WAYS, 15);
    data = FIELD_DP32(data, CPUCFG19, L2IU_SETS, 8);
    data = FIELD_DP32(data, CPUCFG19, L2IU_SIZE, 6);
    env->cpucfg[19] = data;

    data = 0;
    data = FIELD_DP32(data, CPUCFG20, L3IU_WAYS, 15);
    data = FIELD_DP32(data, CPUCFG20, L3IU_SETS, 14);
    data = FIELD_DP32(data, CPUCFG20, L3IU_SIZE, 6);
    env->cpucfg[20] = data;

    env->CSR_ASID = FIELD_DP64(0, CSR_ASID, ASIDBITS, 0xa);

    env->CSR_PRCFG1 = FIELD_DP64(env->CSR_PRCFG1, CSR_PRCFG1, SAVE_NUM, 8);
    env->CSR_PRCFG1 = FIELD_DP64(env->CSR_PRCFG1, CSR_PRCFG1, TIMER_BITS, 0x2f);
    env->CSR_PRCFG1 = FIELD_DP64(env->CSR_PRCFG1, CSR_PRCFG1, VSMAX, 7);

    env->CSR_PRCFG2 = 0x3ffff000;

    env->CSR_PRCFG3 = FIELD_DP64(env->CSR_PRCFG3, CSR_PRCFG3, TLB_TYPE, 2);
    env->CSR_PRCFG3 = FIELD_DP64(env->CSR_PRCFG3, CSR_PRCFG3, MTLB_ENTRY, 63);
    env->CSR_PRCFG3 = FIELD_DP64(env->CSR_PRCFG3, CSR_PRCFG3, STLB_WAYS, 7);
    env->CSR_PRCFG3 = FIELD_DP64(env->CSR_PRCFG3, CSR_PRCFG3, STLB_SETS, 8);
}

void loongarch_centaur320_initfn(CPULoongArchState* env) {

    int i;

    for (i = 0; i < 21; i++) {
        env->cpucfg[i] = 0x0;
    }

    env->cpucfg[0] = 0x14c010;  /* PRID */

    uint32_t data = 0;
    data = FIELD_DP32(data, CPUCFG1, ARCH, 2);
    data = FIELD_DP32(data, CPUCFG1, PGMMU, 1);
    data = FIELD_DP32(data, CPUCFG1, IOCSR, 1);
    data = FIELD_DP32(data, CPUCFG1, PALEN, 0x2f);
    data = FIELD_DP32(data, CPUCFG1, VALEN, 0x2f);
    data = FIELD_DP32(data, CPUCFG1, UAL, 1);
    data = FIELD_DP32(data, CPUCFG1, RI, 1);
    data = FIELD_DP32(data, CPUCFG1, EP, 1);
    data = FIELD_DP32(data, CPUCFG1, RPLV, 1);
    data = FIELD_DP32(data, CPUCFG1, HP, 1);
    data = FIELD_DP32(data, CPUCFG1, IOCSR_BRD, 1);
    data = FIELD_DP32(data, CPUCFG1, MSG_INT, 1);
    env->cpucfg[1] = data;

    data = 0;
    data = FIELD_DP32(data, CPUCFG2, FP, 1);
    data = FIELD_DP32(data, CPUCFG2, FP_SP, 1);
    data = FIELD_DP32(data, CPUCFG2, FP_DP, 1);
    data = FIELD_DP32(data, CPUCFG2, FP_VER, 1);
    data = FIELD_DP32(data, CPUCFG2, LSX, 1);
    data = FIELD_DP32(data, CPUCFG2, LASX, 0);
    data = FIELD_DP32(data, CPUCFG2, LVZ_VER, 1);
    data = FIELD_DP32(data, CPUCFG2, LLFTP, 1);
    data = FIELD_DP32(data, CPUCFG2, LLFTP_VER, 1);
    data = FIELD_DP32(data, CPUCFG2, LSPW, 1);
    data = FIELD_DP32(data, CPUCFG2, LAM, 1);
    data = FIELD_DP32(data, CPUCFG2, HPTW, 1);
    env->cpucfg[2] = data;

    data = 0;
    data = FIELD_DP32(data, CPUCFG3, SFB, 1);
    data = FIELD_DP32(data, CPUCFG3, UCACC, 1);
    data = FIELD_DP32(data, CPUCFG3, LLEXC, 1);
    data = FIELD_DP32(data, CPUCFG3, SCDLY, 1);
    data = FIELD_DP32(data, CPUCFG3, LLDBAR, 1);
    data = FIELD_DP32(data, CPUCFG3, ITLBHMC, 1);
    data = FIELD_DP32(data, CPUCFG3, ICHMC, 1);
    data = FIELD_DP32(data, CPUCFG3, SPW_HP_HF, 1);
    env->cpucfg[3] = data;

    env->cpucfg[4] = 2400000000;

    data = 0;
    data = FIELD_DP32(data, CPUCFG5, CC_MUL, 1);
    data = FIELD_DP32(data, CPUCFG5, CC_DIV, 1);
    env->cpucfg[5] = data;

    data = 0;
    data = FIELD_DP32(data, CPUCFG16, L1_IUPRE, 1);
    data = FIELD_DP32(data, CPUCFG16, L1_DPRE, 1);
    data = FIELD_DP32(data, CPUCFG16, L2_IUUNIFY, 1);
    data = FIELD_DP32(data, CPUCFG16, L2_IUPRIV, 1);
    data = FIELD_DP32(data, CPUCFG16, L2_IUINCL, 1);
    // data = FIELD_DP32(data, CPUCFG16, L3_IUPRE, 1);
    data = FIELD_DP32(data, CPUCFG16, L3_IUUNIFY, 1);
    // data = FIELD_DP32(data, CPUCFG16, L3_IUINCL, 1);
    env->cpucfg[16] = data;

    data = 0;
    data = FIELD_DP32(data, CPUCFG17, L1IU_WAYS, 7);
    data = FIELD_DP32(data, CPUCFG17, L1IU_SETS, 8);
    data = FIELD_DP32(data, CPUCFG17, L1IU_SIZE, 6);
    env->cpucfg[17] = data;

    data = 0;
    data = FIELD_DP32(data, CPUCFG18, L1D_WAYS, 7);
    data = FIELD_DP32(data, CPUCFG18, L1D_SETS, 8);
    data = FIELD_DP32(data, CPUCFG18, L1D_SIZE, 6);
    env->cpucfg[18] = data;

    env->CSR_ASID = FIELD_DP64(0, CSR_ASID, ASIDBITS, 0xa);

    env->CSR_PRCFG1 = FIELD_DP64(env->CSR_PRCFG1, CSR_PRCFG1, SAVE_NUM, 8);
    env->CSR_PRCFG1 = FIELD_DP64(env->CSR_PRCFG1, CSR_PRCFG1, TIMER_BITS, 0x2f);
    env->CSR_PRCFG1 = FIELD_DP64(env->CSR_PRCFG1, CSR_PRCFG1, VSMAX, 7);

    env->CSR_PRCFG2 = 0x1004000; // support 16KB and 32MB page size

    env->CSR_PRCFG3 = FIELD_DP64(env->CSR_PRCFG3, CSR_PRCFG3, TLB_TYPE, 1);
    env->CSR_PRCFG3 = FIELD_DP64(env->CSR_PRCFG3, CSR_PRCFG3, MTLB_ENTRY, 0x3f); // 64 entries

    env->CSR_STLBPS = 0xe; // 16KB page size

    ptw_hw_setVD = 0;
    fprintf(stderr, "warn:auto set ptw_hw_setVD=0\n");
}
void cpu_set_feature(CPULoongArchState* env, char* feature, int value) {
            if (strncmp(feature, "lsx",         3) == 0){   env->cpucfg[2] = FIELD_DP32(env->cpucfg[2], CPUCFG2, LSX, value);        qemu_log("set lsx %d\n", value);
    } else  if (strncmp(feature, "lasx",        4) == 0){   env->cpucfg[2] = FIELD_DP32(env->cpucfg[2], CPUCFG2, LASX, value);       qemu_log("set lasx %d\n", value);
    } else  if (strncmp(feature, "lbt_x86",     7) == 0){   env->cpucfg[2] = FIELD_DP32(env->cpucfg[2], CPUCFG2, LBT_X86, value);    qemu_log("set lbt_x86 %d\n", value);
    } else  if (strncmp(feature, "lbt_arm",     7) == 0){   env->cpucfg[2] = FIELD_DP32(env->cpucfg[2], CPUCFG2, LBT_ARM, value);    qemu_log("set lbt_arm %d\n", value);
    } else  if (strncmp(feature, "lbt_mips",    8) == 0){   env->cpucfg[2] = FIELD_DP32(env->cpucfg[2], CPUCFG2, LBT_MIPS, value);   qemu_log("set lbt_mips %d\n", value);
    } else  if (strncmp(feature, "hptw",        4) == 0){   env->cpucfg[2] = FIELD_DP32(env->cpucfg[2], CPUCFG2, HPTW, value);       qemu_log("set hptw %d\n", value);
    } else  if (strncmp(feature, "frecipe",     7) == 0){   env->cpucfg[2] = FIELD_DP32(env->cpucfg[2], CPUCFG2, FRECIPE, value);    qemu_log("set frecipe %d\n", value);
    } else  if (strncmp(feature, "div32",       5) == 0){   env->cpucfg[2] = FIELD_DP32(env->cpucfg[2], CPUCFG2, DIV32, value);      qemu_log("set div32 %d\n", value);
    } else  if (strncmp(feature, "lam_bh",      6) == 0){   env->cpucfg[2] = FIELD_DP32(env->cpucfg[2], CPUCFG2, LAM_BH, value);     qemu_log("set lam_bh %d\n", value);
    } else  if (strncmp(feature, "lamcas",      6) == 0){   env->cpucfg[2] = FIELD_DP32(env->cpucfg[2], CPUCFG2, LAMCAS, value);     qemu_log("set lamcas %d\n", value);
    } else  if (strncmp(feature, "llacq_screl",11) == 0){   env->cpucfg[2] = FIELD_DP32(env->cpucfg[2], CPUCFG2, LLACQ_SCREL, value);qemu_log("set llacq_screl %d\n", value);
    } else  if (strncmp(feature, "scq",         3) == 0){   env->cpucfg[2] = FIELD_DP32(env->cpucfg[2], CPUCFG2, SCQ, value);        qemu_log("set scq %d\n", value);
    }
}

size_t la_emu_get_handle_gpr() {
    return offsetof(CPUArchState, gpr);
}
size_t la_emu_get_handle_fpr() {
    return offsetof(CPUArchState, fpr);
}

void cpu_reset(CPUState* cs) {
    CPULoongArchState *env = cpu_env(cs);
    env->fcsr0_mask = FCSR0_M1 | FCSR0_M2 | FCSR0_M3;
    env->fcsr0 = 0x0;

    int n;
    /* Set csr registers value after reset */
    env->CSR_CRMD = FIELD_DP64(env->CSR_CRMD, CSR_CRMD, PLV, 0);
    env->CSR_CRMD = FIELD_DP64(env->CSR_CRMD, CSR_CRMD, IE, 0);
    env->CSR_CRMD = FIELD_DP64(env->CSR_CRMD, CSR_CRMD, DA, 1);
    env->CSR_CRMD = FIELD_DP64(env->CSR_CRMD, CSR_CRMD, PG, 0);
    env->CSR_CRMD = FIELD_DP64(env->CSR_CRMD, CSR_CRMD, DATF, 0);
    env->CSR_CRMD = FIELD_DP64(env->CSR_CRMD, CSR_CRMD, DATM, 0);

    env->CSR_EUEN = FIELD_DP64(env->CSR_EUEN, CSR_EUEN, FPE, 0);
    env->CSR_EUEN = FIELD_DP64(env->CSR_EUEN, CSR_EUEN, SXE, 0);
    env->CSR_EUEN = FIELD_DP64(env->CSR_EUEN, CSR_EUEN, ASXE, 0);
    env->CSR_EUEN = FIELD_DP64(env->CSR_EUEN, CSR_EUEN, BTE, 0);

    env->CSR_MISC = 0;

    env->CSR_ECFG = FIELD_DP64(env->CSR_ECFG, CSR_ECFG, VS, 0);
    env->CSR_ECFG = FIELD_DP64(env->CSR_ECFG, CSR_ECFG, LIE, 0);

    env->CSR_ESTAT = env->CSR_ESTAT & (~MAKE_64BIT_MASK(0, 2));
    env->CSR_RVACFG = FIELD_DP64(env->CSR_RVACFG, CSR_RVACFG, RBITS, 0);
    env->CSR_CPUID = cs->cpu_index;
    env->CSR_TCFG = FIELD_DP64(env->CSR_TCFG, CSR_TCFG, EN, 0);
    env->CSR_LLBCTL = FIELD_DP64(env->CSR_LLBCTL, CSR_LLBCTL, KLO, 0);
    env->CSR_TLBRERA = FIELD_DP64(env->CSR_TLBRERA, CSR_TLBRERA, ISTLBR, 0);
    env->CSR_MERRCTL = FIELD_DP64(env->CSR_MERRCTL, CSR_MERRCTL, ISMERR, 0);
    env->CSR_TID = cs->cpu_index;

    for (n = 0; n < 4; n++) {
        env->CSR_DMW[n] = FIELD_DP64(env->CSR_DMW[n], CSR_DMW, PLV0, 0);
        env->CSR_DMW[n] = FIELD_DP64(env->CSR_DMW[n], CSR_DMW, PLV1, 0);
        env->CSR_DMW[n] = FIELD_DP64(env->CSR_DMW[n], CSR_DMW, PLV2, 0);
        env->CSR_DMW[n] = FIELD_DP64(env->CSR_DMW[n], CSR_DMW, PLV3, 0);
    }

#ifndef CONFIG_USER_ONLY
    env->pc = 0x1c000000;
    memset(env->tlb, 0, sizeof(env->tlb));
    // if (kvm_enabled()) {
    //     kvm_arch_reset_vcpu(env);
    // }
#endif

#ifdef CONFIG_TCG
    restore_fp_status(env);
#endif
    cs->exception_index = -1;
}

void loongarch_cpu_do_interrupt(CPUState *cs)
{
    LoongArchCPU *cpu = LOONGARCH_CPU(cs);
    CPULoongArchState *env = &cpu->env;
    bool update_badinstr = 1;
    int cause = -1;
    bool tlbfill = FIELD_EX64(env->CSR_TLBRERA, CSR_TLBRERA, ISTLBR);
    uint32_t vec_size = FIELD_EX64(env->CSR_ECFG, CSR_ECFG, VS);

    if (cs->exception_index != EXCCODE_INT) {
        qemu_log_mask(CPU_LOG_INT,
                     "%s enter: pc " TARGET_FMT_lx " ERA " TARGET_FMT_lx
                     " TLBRERA " TARGET_FMT_lx " exception: %d (%s)\n",
                     __func__, env->pc, env->CSR_ERA, env->CSR_TLBRERA,
                     cs->exception_index,
                     loongarch_exception_name(cs->exception_index));
    }

    switch (cs->exception_index) {
    case EXCCODE_DBP:
        env->CSR_DBG = FIELD_DP64(env->CSR_DBG, CSR_DBG, DCL, 1);
        env->CSR_DBG = FIELD_DP64(env->CSR_DBG, CSR_DBG, ECODE, 0xC);
        goto set_DERA;
    set_DERA:
        env->CSR_DERA = env->pc;
        env->CSR_DBG = FIELD_DP64(env->CSR_DBG, CSR_DBG, DST, 1);
        set_pc(env, env->CSR_EENTRY + 0x480);
        break;
    case EXCCODE_INT:
        if (FIELD_EX64(env->CSR_DBG, CSR_DBG, DST)) {
            env->CSR_DBG = FIELD_DP64(env->CSR_DBG, CSR_DBG, DEI, 1);
            goto set_DERA;
        }
        QEMU_FALLTHROUGH;
    case EXCCODE_PIF:
    case EXCCODE_ADEF:
        cause = cs->exception_index;
        update_badinstr = 0;
        break;
    case EXCCODE_SYS:
    case EXCCODE_BRK:
    case EXCCODE_INE:
    case EXCCODE_IPE:
    case EXCCODE_FPD:
    case EXCCODE_FPE:
    case EXCCODE_SXD:
    case EXCCODE_ASXD:
    case EXCCODE_BTD:
    case EXCCODE_BCE:
    case EXCCODE_ADEM:
    case EXCCODE_PIL:
    case EXCCODE_PIS:
    case EXCCODE_PME:
    case EXCCODE_PNR:
    case EXCCODE_PNX:
    case EXCCODE_PPI:
        cause = cs->exception_index;
        break;
    default:
        qemu_log("Error: exception(%d) has not been supported\n",
                 cs->exception_index);
        abort();
    }

    if (update_badinstr) {
        env->CSR_BADI = cpu_ldl_code(env, env->pc);
    }

    /* Save PLV and IE */
    if (tlbfill) {
        env->CSR_TLBRPRMD = FIELD_DP64(env->CSR_TLBRPRMD, CSR_TLBRPRMD, PPLV,
                                       FIELD_EX64(env->CSR_CRMD,
                                       CSR_CRMD, PLV));
        env->CSR_TLBRPRMD = FIELD_DP64(env->CSR_TLBRPRMD, CSR_TLBRPRMD, PIE,
                                       FIELD_EX64(env->CSR_CRMD, CSR_CRMD, IE));
        /* set the DA mode */
        env->CSR_CRMD = FIELD_DP64(env->CSR_CRMD, CSR_CRMD, DA, 1);
        env->CSR_CRMD = FIELD_DP64(env->CSR_CRMD, CSR_CRMD, PG, 0);
        env->CSR_TLBRERA = FIELD_DP64(env->CSR_TLBRERA, CSR_TLBRERA,
                                      PC, (env->pc >> 2));
    } else {
        if (cause != EXCCODE_INT || (cause == EXCCODE_INT && FIELD_EX64(env->CSR_ECFG, CSR_ECFG, VS) == 0)) {
            env->CSR_ESTAT = FIELD_DP64(env->CSR_ESTAT, CSR_ESTAT, ECODE,
                                    EXCODE_MCODE(cause));
            env->CSR_ESTAT = FIELD_DP64(env->CSR_ESTAT, CSR_ESTAT, ESUBCODE,
                                    EXCODE_SUBCODE(cause));
        }
        env->CSR_PRMD = FIELD_DP64(env->CSR_PRMD, CSR_PRMD, PPLV,
                                   FIELD_EX64(env->CSR_CRMD, CSR_CRMD, PLV));
        env->CSR_PRMD = FIELD_DP64(env->CSR_PRMD, CSR_PRMD, PIE,
                                   FIELD_EX64(env->CSR_CRMD, CSR_CRMD, IE));
        env->CSR_ERA = env->pc;
    }

    env->CSR_CRMD = FIELD_DP64(env->CSR_CRMD, CSR_CRMD, PLV, 0);
    env->CSR_CRMD = FIELD_DP64(env->CSR_CRMD, CSR_CRMD, IE, 0);

    if (vec_size) {
        vec_size = (1 << vec_size) * 4;
    }

    if  (cs->exception_index == EXCCODE_INT) {
        env->irq_count ++;
        /* Interrupt */
        uint32_t vector = 0;
        uint32_t pending = FIELD_EX64(env->CSR_ESTAT, CSR_ESTAT, IS);
        pending &= FIELD_EX64(env->CSR_ECFG, CSR_ECFG, LIE);

        /* Find the highest-priority interrupt. */
        vector = 31 - clz32(pending);
        set_pc(env, env->CSR_EENTRY + \
               (EXCCODE_EXTERNAL_INT + vector) * vec_size);
        qemu_log_mask(CPU_LOG_INT,
                      "%s: PC " TARGET_FMT_lx " ERA " TARGET_FMT_lx
                      " cause %d\n" "    A " TARGET_FMT_lx " D "
                      TARGET_FMT_lx " vector = %d ExC " TARGET_FMT_lx "ExS"
                      TARGET_FMT_lx "\n",
                      __func__, env->pc, env->CSR_ERA,
                      cause, env->CSR_BADV, env->CSR_DERA, vector,
                      env->CSR_ECFG, env->CSR_ESTAT);
    } else {
        if (tlbfill) {
            env->tlbr_count ++;
            set_pc(env, env->CSR_TLBRENTRY);
        } else {
            env->ecounter[cs->exception_index] ++;
            set_pc(env, env->CSR_EENTRY + EXCODE_MCODE(cause) * vec_size);
        }
        qemu_log_mask(CPU_LOG_INT,
                      "%s: PC " TARGET_FMT_lx " ERA " TARGET_FMT_lx
                      " cause %d%s\n, ESTAT " TARGET_FMT_lx
                      " EXCFG " TARGET_FMT_lx " BADVA " TARGET_FMT_lx
                      "BADI " TARGET_FMT_lx " SYS_NUM " TARGET_FMT_lu
                      " cpu %d asid " TARGET_FMT_lx "\n", __func__, env->pc,
                      tlbfill ? env->CSR_TLBRERA : env->CSR_ERA,
                      cause, tlbfill ? "(refill)" : "", env->CSR_ESTAT,
                      env->CSR_ECFG,
                      tlbfill ? env->CSR_TLBRBADV : env->CSR_BADV,
                      env->CSR_BADI, env->gpr[11], cs->cpu_index,
                      env->CSR_ASID);
    }
    cs->exception_index = -1;
}


void loongarch_cpu_set_irq(void *opaque, int irq, int level)
{
    LoongArchCPU *cpu = opaque;
    CPULoongArchState *env = &cpu->env;

    if (irq < 0 || irq >= N_IRQS) {
        lsassert(0);
        return;
    }

    env->CSR_ESTAT = deposit64(env->CSR_ESTAT, irq, 1, level != 0);
}

static const char * const excp_names[] = {
    [EXCCODE_INT] = "Interrupt",
    [EXCCODE_PIL] = "Page invalid exception for load",
    [EXCCODE_PIS] = "Page invalid exception for store",
    [EXCCODE_PIF] = "Page invalid exception for fetch",
    [EXCCODE_PME] = "Page modified exception",
    [EXCCODE_PNR] = "Page Not Readable exception",
    [EXCCODE_PNX] = "Page Not Executable exception",
    [EXCCODE_PPI] = "Page Privilege error",
    [EXCCODE_ADEF] = "Address error for instruction fetch",
    [EXCCODE_ADEM] = "Address error for Memory access",
    [EXCCODE_SYS] = "Syscall",
    [EXCCODE_BRK] = "Break",
    [EXCCODE_INE] = "Instruction Non-Existent",
    [EXCCODE_IPE] = "Instruction privilege error",
    [EXCCODE_FPD] = "Floating Point Disabled",
    [EXCCODE_FPE] = "Floating Point Exception",
    [EXCCODE_DBP] = "Debug breakpoint",
    [EXCCODE_BCE] = "Bound Check Exception",
    [EXCCODE_SXD] = "128 bit vector instructions Disable exception",
    [EXCCODE_ASXD] = "256 bit vector instructions Disable exception",
};

const char *loongarch_exception_name(int32_t exception)
{
    assert(excp_names[exception]);
    return excp_names[exception];
}

void G_NORETURN do_raise_exception(CPULoongArchState *env,
                                   uint32_t exception,
                                   uintptr_t pc)
{
    CPUState *cs = env_cpu(env);
    cpu_clear_tc(env);

    qemu_log_mask(CPU_LOG_INT, "%s: %d (%s)\n",
                  __func__,
                  exception,
                  loongarch_exception_name(exception));
    cs->exception_index = exception;

    cpu_loop_exit(cs);
    // cpu_loop_exit_restore(cs, pc);
}

#define GETBIT(__a, __index) ((__a >> __index) & 1)
#define GETBITS(__a, __index, __len) ((__a >> __index) & ((1 << __len) - 1))

__attribute__((noinline)) void show_register(CPULoongArchState *env) {
    fprintf(stderr, "pc:0x%lx\n", env->pc);
    for (int i = 0; i <32; i++) {
        fprintf(stderr, "r%02d/%-3s:%016lx    ", i, loongarch_r_alias[i], env->gpr[i]);
        if ((i + 1) % 4 == 0) {
            fprintf(stderr, "\n");
        }
    }
}

static void dump_vzoui(int fcsr) {
    fprintf(stderr, "%c%c%c%c%c", GETBIT(fcsr, 4) ? 'V' : '-',  GETBIT(fcsr, 3) ? 'Z' : '-',  GETBIT(fcsr, 2) ? 'O' : '-',  GETBIT(fcsr, 1) ? 'U' : '-',  GETBIT(fcsr, 0) ? 'I' : '-');
}

static void dump_fcsr(int fcsr) {
    int rm = (fcsr >> 8) & 0x3;
    static const char* rm_mode[4] = {
        "RNE",
        "RZ",
        "RP",
        "RM",
    };
    // printf("    Enables:V:%d, Z:%d, O:%d, U:%d, I:%d\n", GETBIT(fcsr, 4), GETBIT(fcsr, 3), GETBIT(fcsr, 2), GETBIT(fcsr, 1), GETBIT(fcsr, 0));
    // printf("    RM     :%d(%s)\n", rm, rm_mode[rm]);
    // printf("    Flags  :V:%d, Z:%d, O:%d, U:%d, I:%d\n", GETBIT(fcsr, 20), GETBIT(fcsr, 19), GETBIT(fcsr, 18), GETBIT(fcsr, 17), GETBIT(fcsr, 16));
    // printf("    Cause  :V:%d, Z:%d, O:%d, U:%d, I:%d\n", GETBIT(fcsr, 28), GETBIT(fcsr, 27), GETBIT(fcsr, 26), GETBIT(fcsr, 25), GETBIT(fcsr, 24));

    fprintf(stderr, "RM:%d(%s)", rm, rm_mode[rm]);
    fprintf(stderr, ",Enables:");dump_vzoui(GETBITS(fcsr, 0, 5));
    fprintf(stderr, ",Flags:");dump_vzoui(GETBITS(fcsr, 16, 5));
    fprintf(stderr, ",Cause:");dump_vzoui(GETBITS(fcsr, 24, 5));
    fprintf(stderr, "\n");
}

__attribute__((noinline)) void show_register_fpr(CPULoongArchState *env) {
    for (int i = 0; i <32; i++) {
        fprintf(stderr, "f%02d   {f = 0x%08x, d = 0x%016lx} {f = %.6f\t, d = %.12f\t}\n",
            i, env->fpr[i].vreg.W[0], env->fpr[i].vreg.D[0], *(float*)&env->fpr[i], *(double*)&env->fpr[i]);
    }
    for (int i = 0; i <8; i++) {
        fprintf(stderr, "fcc%d:%d ", i, env->cf[i]);
    }
    fprintf(stderr, "\n");
    fprintf(stderr, "fcsr:%08x\n", env->fcsr0);
    dump_fcsr(env->fcsr0);
}

__attribute__((noinline)) void show_register_lsx(CPULoongArchState *env) {
    for (int i = 0; i < 32; i++) {
        fprintf(stderr, "f%02d  0x%08x 0x%08x 0x%08x 0x%08x\n", i,
            env->fpr[i].vreg.W[3], env->fpr[i].vreg.W[2], env->fpr[i].vreg.W[1], env->fpr[i].vreg.W[0]);
    }
}