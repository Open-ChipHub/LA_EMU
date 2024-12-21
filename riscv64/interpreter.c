#include "qemu/osdep.h"
#include "cpu.h"
#include "internals.h"
#include "tcg/tcg-gvec-desc.h"
#include "fpu/softfloat.h"

#if defined(CONFIG_USER_ONLY)
#include "user.h"
#endif

#include "util.h"

#include <stdalign.h>

#define __NOT_IMPLEMENTED__ do {fprintf(stderr, "LA_EMU NOT IMPLEMENTED %s, pc:%lx\n", __func__, env->pc); cpu_set_pc(env, env->pc + env->cur_insn_len); return false;} while(0);
#define __NOT_CORRECTED_IMPLEMENTED__ do {fprintf(stderr, "LA_EMU NOT CORRECTED IMPLEMENTED %s, pc:%lx\n", __func__, env->pc);} while(0);
#define __NOT_IMPLEMENTED_EXIT__ do {fprintf(stderr, "LA_EMU NOT IMPLEMENTED %s, pc:%lx\n", __func__, env->pc); laemu_exit(1); return false;} while(0);

#define DisasContext CPURISCVState
#define ctx env
#define tcg_env env
#define tcg_constant_i32(x) ((int32_t)(x))
#define tcg_constant_tl(x) ((target_long)(x))
typedef int32_t TCGv_i32;
typedef int64_t TCGv_i64;

#if TARGET_LONG_BITS == 32
typedef TCGv_i32 TCGv;
#elif TARGET_LONG_BITS == 64
typedef TCGv_i64 TCGv;
#else
#error Unhandled TARGET_LONG_BITS value
#endif

static int ex_plus_1(DisasContext *ctx, int nf)
{
    return nf + 1;
}

#define EX_SH(amount) \
    __attribute__((unused)) static int ex_shift_##amount(DisasContext *ctx, int imm) \
    {                                         \
        return imm << amount;                 \
    }
EX_SH(1)
EX_SH(2)
EX_SH(3)
EX_SH(4)
EX_SH(12)

#define REQUIRE_EXT(ctx, ext) do { \
    if (!has_ext(ctx, ext)) {      \
        return false;              \
    }                              \
} while (0)

#define REQUIRE_32BIT(ctx) do {    \
    if (get_xl(ctx) != MXL_RV32) { \
        return false;              \
    }                              \
} while (0)

#define REQUIRE_64BIT(ctx) do {     \
    if (get_xl(ctx) != MXL_RV64) {  \
        return false;               \
    }                               \
} while (0)

#define REQUIRE_128BIT(ctx) do {    \
    if (get_xl(ctx) != MXL_RV128) { \
        return false;               \
    }                               \
} while (0)

#define REQUIRE_64_OR_128BIT(ctx) do { \
    if (get_xl(ctx) == MXL_RV32) {     \
        return false;                  \
    }                                  \
} while (0)

#define REQUIRE_EITHER_EXT(ctx, A, B) do {       \
    if (!ctx->cfg_ptr->ext_##A &&                \
        !ctx->cfg_ptr->ext_##B) {                \
        return false;                            \
    }                                            \
} while (0)


#ifndef CONFIG_USER_ONLY
#define REQUIRE_FPU do {\
    PERF_INC(COUNTER_INST_FP);                                              \
    if (!get_field(env->mstatus, MSTATUS_FS)) {                             \
        return false;                                                       \
    }                                                                       \
    env->mstatus = set_field(env->mstatus, MSTATUS_FS, EXT_STATUS_DIRTY);   \
} while (0)
#else
#define REQUIRE_FPU do {\
    PERF_INC(COUNTER_INST_FP);\
} while (0)
#endif

#ifdef TARGET_RISCV32
#define get_xl(ctx)    MXL_RV32
#elif defined(CONFIG_USER_ONLY)
#define get_xl(ctx)    MXL_RV64
#else
#define get_xl(ctx)    ((ctx)->xl)
#endif

#ifdef TARGET_RISCV32
#define get_address_xl(ctx)    MXL_RV32
#elif defined(CONFIG_USER_ONLY)
#define get_address_xl(ctx)    MXL_RV64
#else
#define get_address_xl(ctx)    ((ctx)->address_xl)
#endif

/* The operation length, as opposed to the xlen. */
#ifdef TARGET_RISCV32
#define get_ol(ctx)    MXL_RV32
#else
#define get_ol(ctx)    ((ctx)->ol)
#endif

/*
 * RISC-V requires NaN-boxing of narrower width floating point values.
 * This applies when a 32-bit value is assigned to a 64-bit FP register.
 * For consistency and simplicity, we nanbox results even when the RVD
 * extension is not present.
 */
#define gen_nanbox_s(out, in) out = in | MAKE_64BIT_MASK(32, 32);
#define gen_nanbox_h(out, in) out = in | MAKE_64BIT_MASK(16, 48)
// static void gen_nanbox_s(TCGv_i64 out, TCGv_i64 in)
// {
//     tcg_gen_ori_i64(out, in, MAKE_64BIT_MASK(32, 32));
// }

// static void gen_nanbox_h(TCGv_i64 out, TCGv_i64 in)
// {
//     tcg_gen_ori_i64(out, in, MAKE_64BIT_MASK(16, 48));
// }

static target_ulong gen_pc_plus_diff(TCGv target, DisasContext *ctx,
                             target_long diff)
{
    target_ulong dest = ctx->pc + diff;

    if (get_xl(ctx) == MXL_RV32) {
        dest = (int32_t)dest;
    }
    return dest;
}

/*
 * If an operation is being performed on less than TARGET_LONG_BITS,
 * it may require the inputs to be sign- or zero-extended; which will
 * depend on the exact operation being performed.
 */
typedef enum {
    EXT_NONE,
    EXT_SIGN,
    EXT_ZERO,
} DisasExtend;


static target_long get_gpr(DisasContext *ctx, int reg_num, DisasExtend ext)
{
    switch (get_ol(ctx)) {
    case MXL_RV32:
        switch (ext) {
        case EXT_NONE:
            break;
        case EXT_SIGN:
            return (int32_t)env->gpr[reg_num];
        case EXT_ZERO:
            return (uint32_t)env->gpr[reg_num];
        default:
            g_assert_not_reached();
        }
        break;
    case MXL_RV64:
    case MXL_RV128:
        break;
    default:
        g_assert_not_reached();
    }
    return env->gpr[reg_num];
}

static void gen_set_gpr(DisasContext *ctx, int reg_num, TCGv t)
{
    if (reg_num != 0) {
        switch (get_ol(ctx)) {
        case MXL_RV32:
            ctx->gpr[reg_num] = (int32_t)t;
            break;
        case MXL_RV64:
        case MXL_RV128:
            ctx->gpr[reg_num] = t;
            break;
        default:
            g_assert_not_reached();
        }
    }
}

static void gen_set_gpri(DisasContext *ctx, int reg_num, target_long imm)
{
    if (reg_num != 0) {
        switch (get_ol(ctx)) {
        case MXL_RV32:
            ctx->gpr[reg_num] = (int32_t)imm;
            break;
        case MXL_RV64:
        case MXL_RV128:
            ctx->gpr[reg_num] = imm;
            break;
        default:
            g_assert_not_reached();
        }
    }
}

static uint64_t get_fpr_d(DisasContext *ctx, int reg_num)
{
    return env->fpr[reg_num];
}
/* assume it is nanboxing (for normal) or sign-extended (for zfinx) */
static void gen_set_fpr_hs(DisasContext *ctx, int reg_num, TCGv_i64 t)
{
    ctx->fpr[reg_num] = t;
}
static void gen_set_fpr_d(DisasContext *ctx, int reg_num, int64_t t)
{
    ctx->fpr[reg_num] = t;
    return;
}

/* Compute a canonical address from a register plus offset. */
static target_ulong get_address(DisasContext *ctx, int rs1, int imm)
{
    TCGv src1 = get_gpr(ctx, rs1, EXT_NONE);
    target_ulong addr = src1 + imm;
    if (get_address_xl(ctx) == MXL_RV32) {
        addr = (uint32_t)addr;
    }
    return addr;
}

static int ex_rvc_register(DisasContext *ctx, int reg)
{
    return 8 + reg;
}

static int ex_sreg_register(DisasContext *ctx, int reg)
{
    return reg < 2 ? reg + 8 : reg + 16;
}

static int ex_rvc_shiftli(DisasContext *ctx, int imm)
{
    /* For RV128 a shamt of 0 means a shift by 64. */
    if (get_ol(ctx) == MXL_RV128) {
        imm = imm ? imm : 64;
    }
    return imm;
}

static int ex_rvc_shiftri(DisasContext *ctx, int imm)
{
    /*
     * For RV128 a shamt of 0 means a shift by 64, furthermore, for right
     * shifts, the shamt is sign-extended.
     */
    if (get_ol(ctx) == MXL_RV128) {
        imm = imm | (imm & 32) << 1;
        imm = imm ? imm : 64;
    }
    return imm;
}

static void gen_set_rm(DisasContext *ctx, int rm)
{
    // if (ctx->frm == rm) {
    //     return;
    // }
    // ctx->frm = rm;

    // if (rm == RISCV_FRM_DYN) {
    //     /* The helper will return only if frm valid. */
    //     ctx->frm_valid = true;
    // }

    /* The helper may raise ILLEGAL_INSN -- record binv for unwind. */
    helper_set_rounding_mode(tcg_env, tcg_constant_i32(rm));
}

// static void gen_set_rm_chkfrm(DisasContext *ctx, int rm)
// {
//     if (ctx->frm == rm && ctx->frm_valid) {
//         return;
//     }
//     ctx->frm = rm;
//     ctx->frm_valid = true;

//     /* The helper may raise ILLEGAL_INSN -- record binv for unwind. */
//     helper_set_rounding_mode_chkfrm(tcg_env, tcg_constant_i32(rm));
// }

#include "trans_rv32.c.inc"
#include "trans_rv16.c.inc"

bool is_one_page(uint64_t addr, int bytes) {
    target_ulong pgmsk = TARGET_PAGE_MASK;
    return (addr & pgmsk) == ((addr + bytes - 1) & pgmsk);
}

bool is_two_page(uint64_t addr, int bytes) {
    return !is_one_page(addr, bytes);
}

bool is_aligned(uint64_t addr, int bytes) {
#ifdef CONFIG_USER_ONLY
        return true;
#endif
    return is_one_page(addr, bytes);
    // return !(addr & (bytes - 1));
}

bool is_unaligned(uint64_t addr, int bytes) {
    return !is_aligned(addr, bytes);
}

static hwaddr load_pa(DisasContext *env, uint64_t addr) {
    PERF_INC(COUNTER_INST_LOAD);
#ifdef CONFIG_USER_ONLY
    return addr;
#else
    return trans_pa(env, addr, MMU_DATA_LOAD);
#endif
}
static hwaddr store_pa(DisasContext *env, uint64_t addr) {
    PERF_INC(COUNTER_INST_STORE);
#ifdef CONFIG_USER_ONLY
    return addr;
#else
    return trans_pa(env, addr, MMU_DATA_STORE);
#endif
}

#if defined(CONFIG_USER_ONLY) || defined(CONFIG_DIFF)
#define is_io(...) false
#else
static bool is_io(hwaddr ha) {
    // return ha < 0x80000000;
    return ha < 0x40000000;
}
#endif

static int8_t ld_b(DisasContext *env, uint64_t va) {
    hwaddr ha = load_pa(env, va);
    int8_t data;
#if defined(CONFIG_USER_ONLY)
    data = ram_ldb(ha);
#else
    data = is_io(ha) ? do_io_ld(ha, 1) : ram_ldb(ha);
#endif
    PLUGIN_CALL(emu_load, va, SIZE_SHIFT_B, &data);
    return data;
}

static int16_t ld_h(DisasContext *env, uint64_t va) {
    uint64_t data;
    const int data_size = 2;
    hwaddr ha = load_pa(env, va);
    if (is_io(ha)) {
#if !defined(CONFIG_USER_ONLY)
        data = do_io_ld(ha, data_size);
#endif
    } else {
        if (is_aligned(va, data_size)) {
            data = ram_ldh(ha);
        } else {
            PERF_INC(COUNTER_INST_CROSS_PAGE_LOAD);
            data = 0;
            for (int i = (data_size - 1); i >= 0; i--){
                data |= (((uint16_t)ld_b(env, va + i) & 0xff) << (i * 8)) ;
            }
        }
    }
    PLUGIN_CALL(emu_load, va, SIZE_SHIFT_H, &data);
    return data;
}

static int32_t ld_w(DisasContext *env, uint64_t va) {
    uint64_t data;
    const int data_size = 4;
    hwaddr ha = load_pa(env, va);
    if (is_io(ha)) {
#if !defined(CONFIG_USER_ONLY)
        data = do_io_ld(ha, data_size);
#endif
    } else {
        if (is_aligned(va, data_size)) {
            data = ram_ldw(ha);
        } else {
            PERF_INC(COUNTER_INST_CROSS_PAGE_LOAD);
            data = 0;
            for (int i = (data_size - 1); i >= 0; i--){
                data |= (((uint32_t)ld_b(env, va + i) & 0xff) << (i * 8)) ;
            }
        }
    }
    PLUGIN_CALL(emu_load, va, SIZE_SHIFT_W, &data);
    return data;
}

static int64_t ld_d(DisasContext *env, uint64_t va) {
    uint64_t data;
    const int data_size = 8;
    hwaddr ha = load_pa(env, va);
    if (is_io(ha)) {
#if !defined(CONFIG_USER_ONLY)
        data = do_io_ld(ha, data_size);
#endif
    } else {
        if (is_aligned(va, data_size)) {
            data = ram_ldd(ha);
        } else {
            PERF_INC(COUNTER_INST_CROSS_PAGE_LOAD);
            data = 0;
            for (int i = (data_size - 1); i >= 0; i--){
                data |= (((uint64_t)ld_b(env, va + i) & 0xff) << (i * 8)) ;
            }
        }
    }
    PLUGIN_CALL(emu_load, va, SIZE_SHIFT_D, &data);
    return data;
}

// static Int128 ld_128(DisasContext *env, uint64_t va) {
//     Int128 data;
//     const int data_size = 16;
//     hwaddr ha = load_pa(env, va);
//     if (is_io(ha)) {
//         lsassert(0);
//     } else {
//         if (is_aligned(va, data_size)) {
//             data = ram_ld128(ha);
//         } else {
//             data = 0;
//             for (int i = (data_size - 1); i >= 0; i--){
//                 data |= (((Int128)ld_b(env, va + i) & 0xff) << (i * 8));
//             }
//         }
//     }
//     return data;
// }

// static VReg ld_256(DisasContext *env, uint64_t va) {
//     VReg data;
//     const int data_size = 32;
//     hwaddr ha = load_pa(env, va);
//     if (is_io(ha)) {
//         lsassert(0);
//     } else {
//         if (is_aligned(va, data_size)) {
//             data = ram_ld256(ha);
//         } else {
//             for (int i = (data_size - 1); i >= 0; i--){
//                 data.B[i] = (ld_b(env, va + i) & 0xff) << (i * 8);
//             }
//         }
//     }
//     return data;
// }

static void st_b(DisasContext *env, uint64_t va, uint8_t data) {
    hwaddr ha = store_pa(env, va);
#if defined(CONFIG_USER_ONLY)
    ram_stb(ha, data);
#else
    is_io(ha) ? do_io_st(ha, data, 1) : ram_stb(ha, data);
#endif
    PLUGIN_CALL(emu_store, va, SIZE_SHIFT_B, &data);
}

static void st_h(DisasContext *env, uint64_t va, uint16_t data) {
    const int data_size = 2;
    hwaddr ha = store_pa(env, va);
    if (is_io(ha)) {
#if !defined(CONFIG_USER_ONLY)
        do_io_st(ha, data, data_size);
#endif
    } else {
        if (is_aligned(va, data_size)) {
            ram_sth(ha, data);
        } else {
            PERF_INC(COUNTER_INST_CROSS_PAGE_STORE);
            for (int i = (data_size - 1); i >= 0; i--){
                st_b(env, va + i, (data >> (i * 8)) & 0xff);
            }
        }
    }
    PLUGIN_CALL(emu_store, va, SIZE_SHIFT_W, &data);
}

static void st_w(DisasContext *env, uint64_t va, uint32_t data) {
    const int data_size = 4;
    hwaddr ha = store_pa(env, va);
    if (is_io(ha)) {
#if !defined(CONFIG_USER_ONLY)
        do_io_st(ha, data, data_size);
#endif
    } else {
        if (is_aligned(va, data_size)) {
            ram_stw(ha, data);
        } else {
            PERF_INC(COUNTER_INST_CROSS_PAGE_STORE);
            for (int i = (data_size - 1); i >= 0; i--){
                st_b(env, va + i, (data >> (i * 8)) & 0xff);
            }
        }
    }
    PLUGIN_CALL(emu_store, va, SIZE_SHIFT_W, &data);
}

static void st_d(DisasContext *env, uint64_t va, uint64_t data) {
    const int data_size = 8;
    hwaddr ha = store_pa(env, va);
    if (is_io(ha)) {
#if !defined(CONFIG_USER_ONLY)
        do_io_st(ha, data, data_size);
#endif
    } else {
        if (is_aligned(va, data_size)) {
            ram_std(ha, data);
        } else {
            PERF_INC(COUNTER_INST_CROSS_PAGE_STORE);
            for (int i = (data_size - 1); i >= 0; i--){
                st_b(env, va + i, (data >> (i * 8)) & 0xff);
            }
        }
    }
    PLUGIN_CALL(emu_store, va, SIZE_SHIFT_D, &data);
}

// static void st_128(DisasContext *env, uint64_t va, Int128 data) {
//     const int data_size = 16;
//     hwaddr ha = store_pa(env, va);
//     if (is_io(ha)) {
//         lsassert(0);
//     } else {
//         if (is_aligned(va, data_size)) {
//             ram_st128(ha, data);
//         } else {
//             for (int i = (data_size - 1); i >= 0; i--){
//                 st_b(env, va + i, (data >> (i * 8)) & 0xff);
//             }
//         }
//     }
// }

// static void st_256(DisasContext *env, uint64_t va, VReg data) {
//     const int data_size = 32;
//     hwaddr ha = store_pa(env, va);
//     if (is_io(ha)) {
//         lsassert(0);
//     } else {
//         if (is_aligned(va, data_size)) {
//             ram_st256(ha, data);
//         } else {
//             for (int i = (data_size - 1); i >= 0; i--){
//                 st_b(env, va + i, data.B[i]);
//             }
//         }
//     }
// }

static bool trans_addd(DisasContext *ctx, arg_addd *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_addid(DisasContext *ctx, arg_addid *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_subd(DisasContext *ctx, arg_subd *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_slld(DisasContext *ctx, arg_slld *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_sllid(DisasContext *ctx, arg_sllid *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_srad(DisasContext *ctx, arg_srad *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_sraid(DisasContext *ctx, arg_sraid *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_srld(DisasContext *ctx, arg_srld *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_muld(DisasContext *ctx, arg_muld *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_srlid(DisasContext *ctx, arg_srlid *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_divd(DisasContext *ctx, arg_divd *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_divud(DisasContext *ctx, arg_divud *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_remud(DisasContext *ctx, arg_remud *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_remd(DisasContext *ctx, arg_remd *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_slli_uw(DisasContext *ctx, arg_slli_uw *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_ldu(DisasContext *ctx, arg_ldu *a) {__NOT_IMPLEMENTED_EXIT__}

static bool trans_lq(DisasContext *ctx, arg_lq *a) {return false;}
static bool trans_sq(DisasContext *ctx, arg_sq *a) {return false;}

static bool trans_addi(DisasContext *ctx, arg_addi *a) {
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv dest = src1 + a->imm;
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}
static bool trans_addiw(DisasContext *ctx, arg_addiw *a) {
    ctx->ol = MXL_RV32;
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv dest = src1 + a->imm;
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}
static bool trans_addw(DisasContext *ctx, arg_addw *a) {
    ctx->ol = MXL_RV32;
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv src2 = get_gpr(ctx, a->rs2, EXT_NONE);
    TCGv dest = (int32_t)src1 + (int32_t)src2;
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}
static bool trans_subw(DisasContext *ctx, arg_subw *a) {
    ctx->ol = MXL_RV32;
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv src2 = get_gpr(ctx, a->rs2, EXT_NONE);
    TCGv dest = (int32_t)src1 - (int32_t)src2;
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}
static bool trans_slliw(DisasContext *ctx, arg_slliw *a) {
    ctx->ol = MXL_RV32;
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv dest = src1 << a->shamt;
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}
static bool trans_sllw(DisasContext *ctx, arg_sllw *a) {
    ctx->ol = MXL_RV32;
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv src2 = get_gpr(ctx, a->rs2, EXT_NONE);
    TCGv dest = src1 << (src2 & 0x1f);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}
static bool trans_srliw(DisasContext *ctx, arg_srliw *a) {
    ctx->ol = MXL_RV32;
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv dest = (uint32_t)src1 >> a->shamt;
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}
static bool trans_srlw(DisasContext *ctx, arg_srlw *a) {
    ctx->ol = MXL_RV32;
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv src2 = get_gpr(ctx, a->rs2, EXT_NONE);
    TCGv dest = (uint32_t)src1 >> (src2 & 0x1f);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}
static bool trans_sraiw(DisasContext *ctx, arg_sraiw *a) {
    ctx->ol = MXL_RV32;
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv dest = (int32_t)src1 >> a->shamt;
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}
static bool trans_sraw(DisasContext *ctx, arg_sraw *a) {
    ctx->ol = MXL_RV32;
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv src2 = get_gpr(ctx, a->rs2, EXT_NONE);
    TCGv dest = (int32_t)src1 >> (src2 & 0x1f);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}
static bool trans_add(DisasContext *ctx, arg_add *a) {
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv src2 = get_gpr(ctx, a->rs2, EXT_NONE);
    TCGv dest = src1 + src2;
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}
static bool trans_sub(DisasContext *ctx, arg_sub *a) {
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv src2 = get_gpr(ctx, a->rs2, EXT_NONE);
    TCGv dest = src1 - src2;
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}
static bool trans_add_uw(DisasContext *ctx, arg_add_uw *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_orn(DisasContext *ctx, arg_orn *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_xnor(DisasContext *ctx, arg_xnor *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_andn(DisasContext *ctx, arg_andn *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_bclr(DisasContext *ctx, arg_bclr *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_bclri(DisasContext *ctx, arg_bclri *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_bext(DisasContext *ctx, arg_bext *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_bexti(DisasContext *ctx, arg_bexti *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_binv(DisasContext *ctx, arg_binv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_binvi(DisasContext *ctx, arg_binvi *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_brev8(DisasContext *ctx, arg_brev8 *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_bset(DisasContext *ctx, arg_bset *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_bseti(DisasContext *ctx, arg_bseti *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_max(DisasContext *ctx, arg_max *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_maxu(DisasContext *ctx, arg_maxu *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_min(DisasContext *ctx, arg_min *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_minu(DisasContext *ctx, arg_minu *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_orc_b(DisasContext *ctx, arg_orc_b *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_pack(DisasContext *ctx, arg_pack *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_packh(DisasContext *ctx, arg_packh *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_packw(DisasContext *ctx, arg_packw *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_rev8_32(DisasContext *ctx, arg_rev8_32 *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_rev8_64(DisasContext *ctx, arg_rev8_64 *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_rol(DisasContext *ctx, arg_rol *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_rolw(DisasContext *ctx, arg_rolw *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_ror(DisasContext *ctx, arg_ror *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_rori(DisasContext *ctx, arg_rori *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_roriw(DisasContext *ctx, arg_roriw *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_rorw(DisasContext *ctx, arg_rorw *a) {__NOT_IMPLEMENTED_EXIT__}


static bool trans_aes32dsi(DisasContext *ctx, arg_aes32dsi *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_aes32dsmi(DisasContext *ctx, arg_aes32dsmi *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_aes32esi(DisasContext *ctx, arg_aes32esi *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_aes32esmi(DisasContext *ctx, arg_aes32esmi *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_aes64ds(DisasContext *ctx, arg_aes64ds *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_aes64dsm(DisasContext *ctx, arg_aes64dsm *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_aes64es(DisasContext *ctx, arg_aes64es *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_aes64esm(DisasContext *ctx, arg_aes64esm *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_aes64im(DisasContext *ctx, arg_aes64im *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_aes64ks1i(DisasContext *ctx, arg_aes64ks1i *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_aes64ks2(DisasContext *ctx, arg_aes64ks2 *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_amoadd_b(DisasContext *ctx, arg_amoadd_b *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_amoadd_h(DisasContext *ctx, arg_amoadd_h *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_amoadd_w(DisasContext *ctx, arg_amoadd_w *a) {
    target_ulong addr = get_address(ctx, a->rs1, 0);
    target_ulong dest = ld_w(env, addr);
    target_ulong data = get_gpr(ctx, a->rs2, EXT_NONE);
    st_w(ctx, addr, dest + data);
    gen_set_gpr(ctx, a->rd, (int32_t)dest);
    return true;
}
static bool trans_amoadd_d(DisasContext *ctx, arg_amoadd_d *a) {
    target_ulong addr = get_address(ctx, a->rs1, 0);
    target_ulong dest = ld_d(env, addr);
    target_ulong data = get_gpr(ctx, a->rs2, EXT_NONE);
    st_d(ctx, addr, dest + data);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}
static bool trans_amoand_b(DisasContext *ctx, arg_amoand_b *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_amoand_h(DisasContext *ctx, arg_amoand_h *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_amoand_w(DisasContext *ctx, arg_amoand_w *a) {
    target_ulong addr = get_address(ctx, a->rs1, 0);
    target_ulong dest = ld_w(env, addr);
    target_ulong data = get_gpr(ctx, a->rs2, EXT_NONE);
    st_w(ctx, addr, dest & data);
    gen_set_gpr(ctx, a->rd, (int32_t)dest);
    return true;
}
static bool trans_amoand_d(DisasContext *ctx, arg_amoand_d *a) {
    target_ulong addr = get_address(ctx, a->rs1, 0);
    target_ulong dest = ld_d(env, addr);
    target_ulong data = get_gpr(ctx, a->rs2, EXT_NONE);
    st_d(ctx, addr, dest & data);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}

static bool trans_amoor_b(DisasContext *ctx, arg_amoor_b *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_amoor_h(DisasContext *ctx, arg_amoor_h *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_amoor_w(DisasContext *ctx, arg_amoor_w *a) {
    target_ulong addr = get_address(ctx, a->rs1, 0);
    target_ulong dest = ld_w(env, addr);
    target_ulong data = get_gpr(ctx, a->rs2, EXT_NONE);
    st_w(ctx, addr, dest | data);
    gen_set_gpr(ctx, a->rd, (int32_t)dest);
    return true;
}
static bool trans_amoor_d(DisasContext *ctx, arg_amoor_d *a) {
    target_ulong addr = get_address(ctx, a->rs1, 0);
    target_ulong dest = ld_d(env, addr);
    target_ulong data = get_gpr(ctx, a->rs2, EXT_NONE);
    st_d(ctx, addr, dest | data);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}
static bool trans_amoxor_b(DisasContext *ctx, arg_amoxor_b *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_amoxor_h(DisasContext *ctx, arg_amoxor_h *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_amoxor_w(DisasContext *ctx, arg_amoxor_w *a) {
    target_ulong addr = get_address(ctx, a->rs1, 0);
    target_ulong dest = ld_w(env, addr);
    target_ulong data = get_gpr(ctx, a->rs2, EXT_NONE);
    st_w(ctx, addr, dest ^ data);
    gen_set_gpr(ctx, a->rd, (int32_t)dest);
    return true;
}
static bool trans_amoxor_d(DisasContext *ctx, arg_amoxor_d *a) {
    target_ulong addr = get_address(ctx, a->rs1, 0);
    target_ulong dest = ld_d(env, addr);
    target_ulong data = get_gpr(ctx, a->rs2, EXT_NONE);
    st_d(ctx, addr, dest ^ data);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}
static bool trans_amocas_b(DisasContext *ctx, arg_amocas_b *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_amocas_h(DisasContext *ctx, arg_amocas_h *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_amocas_w(DisasContext *ctx, arg_amocas_w *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_amocas_d(DisasContext *ctx, arg_amocas_d *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_amocas_q(DisasContext *ctx, arg_amocas_q *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_amomax_b(DisasContext *ctx, arg_amomax_b *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_amomax_h(DisasContext *ctx, arg_amomax_h *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_amomax_w(DisasContext *ctx, arg_amomax_w *a) {
    target_ulong addr = get_address(ctx, a->rs1, 0);
    target_ulong dest = ld_w(env, addr);
    target_ulong data = get_gpr(ctx, a->rs2, EXT_NONE);
    st_w(ctx, addr, MAX((int32_t)dest, (int32_t)data));
    gen_set_gpr(ctx, a->rd, (int32_t)dest);
    return true;
}
static bool trans_amomax_d(DisasContext *ctx, arg_amomax_d *a) {
    target_ulong addr = get_address(ctx, a->rs1, 0);
    target_ulong dest = ld_d(env, addr);
    target_ulong data = get_gpr(ctx, a->rs2, EXT_NONE);
    st_d(ctx, addr, MAX((int64_t)dest, (int64_t)data));
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}
static bool trans_amomaxu_b(DisasContext *ctx, arg_amomaxu_b *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_amomaxu_h(DisasContext *ctx, arg_amomaxu_h *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_amomaxu_w(DisasContext *ctx, arg_amomaxu_w *a) {
    target_ulong addr = get_address(ctx, a->rs1, 0);
    target_ulong dest = ld_w(env, addr);
    target_ulong data = get_gpr(ctx, a->rs2, EXT_NONE);
    st_w(ctx, addr, MAX((uint32_t)dest, (uint32_t)data));
    gen_set_gpr(ctx, a->rd, (int32_t)dest);
    return true;
}
static bool trans_amomaxu_d(DisasContext *ctx, arg_amomaxu_d *a) {
    target_ulong addr = get_address(ctx, a->rs1, 0);
    target_ulong dest = ld_d(env, addr);
    target_ulong data = get_gpr(ctx, a->rs2, EXT_NONE);
    st_d(ctx, addr, MAX((uint64_t)dest, (uint64_t)data));
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}
static bool trans_amomin_b(DisasContext *ctx, arg_amomin_b *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_amomin_h(DisasContext *ctx, arg_amomin_h *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_amomin_w(DisasContext *ctx, arg_amomin_w *a) {
    target_ulong addr = get_address(ctx, a->rs1, 0);
    target_ulong dest = ld_w(env, addr);
    target_ulong data = get_gpr(ctx, a->rs2, EXT_NONE);
    st_w(ctx, addr, MIN((int32_t)dest, (int32_t)data));
    gen_set_gpr(ctx, a->rd, (int32_t)dest);
    return true;
}
static bool trans_amomin_d(DisasContext *ctx, arg_amomin_d *a) {
    target_ulong addr = get_address(ctx, a->rs1, 0);
    target_ulong dest = ld_d(env, addr);
    target_ulong data = get_gpr(ctx, a->rs2, EXT_NONE);
    st_d(ctx, addr, MIN((int64_t)dest, (int64_t)data));
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}
static bool trans_amominu_b(DisasContext *ctx, arg_amominu_b *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_amominu_h(DisasContext *ctx, arg_amominu_h *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_amominu_w(DisasContext *ctx, arg_amominu_w *a) {
    target_ulong addr = get_address(ctx, a->rs1, 0);
    target_ulong dest = ld_w(env, addr);
    target_ulong data = get_gpr(ctx, a->rs2, EXT_NONE);
    st_w(ctx, addr, MIN((uint32_t)dest, (uint32_t)data));
    gen_set_gpr(ctx, a->rd, (int32_t)dest);
    return true;
}
static bool trans_amominu_d(DisasContext *ctx, arg_amominu_d *a) {
    target_ulong addr = get_address(ctx, a->rs1, 0);
    target_ulong dest = ld_d(env, addr);
    target_ulong data = get_gpr(ctx, a->rs2, EXT_NONE);
    st_d(ctx, addr, MIN((uint64_t)dest, (uint64_t)data));
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}
static bool trans_amoswap_b(DisasContext *ctx, arg_amoswap_b *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_amoswap_h(DisasContext *ctx, arg_amoswap_h *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_amoswap_w(DisasContext *ctx, arg_amoswap_w *a) {
    target_ulong addr = get_address(ctx, a->rs1, 0);
    target_ulong dest = ld_w(env, addr);
    target_ulong data = get_gpr(ctx, a->rs2, EXT_NONE);
    st_w(ctx, addr, data);
    gen_set_gpr(ctx, a->rd, (int32_t)dest);
    return true;
}
static bool trans_amoswap_d(DisasContext *ctx, arg_amoswap_d *a) {
    target_ulong addr = get_address(ctx, a->rs1, 0);
    target_ulong dest = ld_d(env, addr);
    target_ulong data = get_gpr(ctx, a->rs2, EXT_NONE);
    st_d(ctx, addr, data);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}
static bool trans_and(DisasContext *ctx, arg_and *a) {
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv src2 = get_gpr(ctx, a->rs2, EXT_NONE);
    TCGv dest = src1 & src2;
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}
static bool trans_andi(DisasContext *ctx, arg_andi *a) {
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv dest = src1 & (target_long)a->imm;
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}
static bool trans_or(DisasContext *ctx, arg_or *a) {
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv src2 = get_gpr(ctx, a->rs2, EXT_NONE);
    TCGv dest = src1 | src2;
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}
static bool trans_ori(DisasContext *ctx, arg_ori *a) {
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv dest = src1 | (target_long)a->imm;
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}
static bool trans_xor(DisasContext *ctx, arg_xor *a) {
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv src2 = get_gpr(ctx, a->rs2, EXT_NONE);
    TCGv dest = src1 ^ src2;
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}
static bool trans_xori(DisasContext *ctx, arg_xori *a) {
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv dest = src1 ^ (target_long)a->imm;
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}
static bool trans_auipc(DisasContext *ctx, arg_auipc *a) {
    target_ulong pc = gen_pc_plus_diff(0, ctx, a->imm);
    gen_set_gpr(ctx, a->rd, pc);
    return true;
}
static bool trans_beq(DisasContext *ctx, arg_beq *a) {
    PERF_INC(COUNTER_INST_BRANCH);
    target_ulong src1 = get_gpr(ctx, a->rs1, EXT_SIGN);
    target_ulong src2 = get_gpr(ctx, a->rs2, EXT_SIGN);
    bool taken = src1 == src2;
    if (taken) {
        cpu_set_pc(env, env->pc + a->imm);
        env->cur_insn_len = 0;
    }
#ifdef RECORD_BRANCH
    env->taken = taken;
    env->target = env->pc + a->imm;
#endif
    return true;
}
static bool trans_bne(DisasContext *ctx, arg_bne *a) {
    PERF_INC(COUNTER_INST_BRANCH);
    target_ulong src1 = get_gpr(ctx, a->rs1, EXT_SIGN);
    target_ulong src2 = get_gpr(ctx, a->rs2, EXT_SIGN);
    bool taken = src1 != src2;
    if (taken) {
        cpu_set_pc(env, env->pc + a->imm);
        env->cur_insn_len = 0;
    }
#ifdef RECORD_BRANCH
    env->taken = taken;
    env->target = env->pc + a->imm;
#endif
    return true;
}
static bool trans_blt(DisasContext *ctx, arg_blt *a) {
    PERF_INC(COUNTER_INST_BRANCH);
    target_ulong src1 = get_gpr(ctx, a->rs1, EXT_SIGN);
    target_ulong src2 = get_gpr(ctx, a->rs2, EXT_SIGN);
    bool taken = (target_long)src1 < (target_long)src2;
    if (taken) {
        cpu_set_pc(env, env->pc + a->imm);
        env->cur_insn_len = 0;
    }
#ifdef RECORD_BRANCH
    env->taken = taken;
    env->target = env->pc + a->imm;
#endif
    return true;
}
static bool trans_bltu(DisasContext *ctx, arg_bltu *a) {
    PERF_INC(COUNTER_INST_BRANCH);
    target_ulong src1 = get_gpr(ctx, a->rs1, EXT_SIGN);
    target_ulong src2 = get_gpr(ctx, a->rs2, EXT_SIGN);
    bool taken = (target_ulong)src1 < (target_ulong)src2;
    if (taken) {
        cpu_set_pc(env, env->pc + a->imm);
        env->cur_insn_len = 0;
    }
#ifdef RECORD_BRANCH
    env->taken = taken;
    env->target = env->pc + a->imm;
#endif
    return true;
}
static bool trans_bge(DisasContext *ctx, arg_bge *a) {
    PERF_INC(COUNTER_INST_BRANCH);
    target_ulong src1 = get_gpr(ctx, a->rs1, EXT_SIGN);
    target_ulong src2 = get_gpr(ctx, a->rs2, EXT_SIGN);
    bool taken = (target_long)src1 >= (target_long)src2;
    if (taken) {
        cpu_set_pc(env, env->pc + a->imm);
        env->cur_insn_len = 0;
    }
#ifdef RECORD_BRANCH
    env->taken = taken;
    env->target = env->pc + a->imm;
#endif
    return true;
}
static bool trans_bgeu(DisasContext *ctx, arg_bgeu *a) {
    PERF_INC(COUNTER_INST_BRANCH);
    target_ulong src1 = get_gpr(ctx, a->rs1, EXT_SIGN);
    target_ulong src2 = get_gpr(ctx, a->rs2, EXT_SIGN);
    bool taken = (target_ulong)src1 >= (target_ulong)src2;
    if (taken) {
        cpu_set_pc(env, env->pc + a->imm);
        env->cur_insn_len = 0;
    }
#ifdef RECORD_BRANCH
    env->taken = taken;
    env->target = env->pc + a->imm;
#endif
    return true;
}
static bool trans_cbo_clean(DisasContext *ctx, arg_cbo_clean *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_cbo_flush(DisasContext *ctx, arg_cbo_flush *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_cbo_inval(DisasContext *ctx, arg_cbo_inval *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_cbo_zero(DisasContext *ctx, arg_cbo_zero *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_clmul(DisasContext *ctx, arg_clmul *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_clmulh(DisasContext *ctx, arg_clmulh *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_clmulr(DisasContext *ctx, arg_clmulr *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_clz(DisasContext *ctx, arg_clz *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_clzw(DisasContext *ctx, arg_clzw *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_cpop(DisasContext *ctx, arg_cpop *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_cpopw(DisasContext *ctx, arg_cpopw *a) {__NOT_IMPLEMENTED_EXIT__}

static bool do_csrr(DisasContext *ctx, int rd, int rc)
{
    TCGv dest;
    TCGv_i32 csr = tcg_constant_i32(rc);

    // translator_io_start(&ctx->base);
    dest = helper_csrr(tcg_env, csr);
    gen_set_gpr(ctx, rd, dest);
    return true;
    // return do_csr_post(ctx);
}

static bool do_csrw(DisasContext *ctx, int rc, TCGv src)
{
    TCGv_i32 csr = tcg_constant_i32(rc);

    // translator_io_start(&ctx->base);
    helper_csrw(tcg_env, csr, src);
    return true;
    // return do_csr_post(ctx);
}

static bool do_csrrw(DisasContext *ctx, int rd, int rc, TCGv src, TCGv mask)
{
    TCGv dest;
    TCGv_i32 csr = tcg_constant_i32(rc);

    dest = helper_csrrw(tcg_env, csr, src, mask);
    gen_set_gpr(ctx, rd, dest);
    return true;
    // return do_csr_post(ctx);
}

static bool trans_csrrc(DisasContext *ctx, arg_csrrc *a) {
    /*
     * If rs1 == 0, the insn shall not write to the csr at all, nor
     * cause any of the side effects that might occur on a csr write.
     * Note that if rs1 specifies a register other than x0, holding
     * a zero value, the instruction will still attempt to write the
     * unmodified value back to the csr and will cause side effects.
     */
    if (get_xl(ctx) < MXL_RV128) {
        if (a->rs1 == 0) {
            return do_csrr(ctx, a->rd, a->csr);
        }

        TCGv mask = get_gpr(ctx, a->rs1, EXT_ZERO);
        return do_csrrw(ctx, a->rd, a->csr, ctx->zero, mask);
    } else {
        return false;
        // if (a->rs1 == 0) {
        //     return do_csrr_i128(ctx, a->rd, a->csr);
        // }

        // TCGv maskl = get_gpr(ctx, a->rs1, EXT_ZERO);
        // TCGv maskh = get_gprh(ctx, a->rs1);
        // return do_csrrw_i128(ctx, a->rd, a->csr,
        //                      ctx->zero, ctx->zero, maskl, maskh);
    }
}
static bool trans_csrrci(DisasContext *ctx, arg_csrrci *a) {
    /*
     * If rs1 == 0, the insn shall not write to the csr at all, nor
     * cause any of the side effects that might occur on a csr write.
     * Note that if rs1 specifies a register other than x0, holding
     * a zero value, the instruction will still attempt to write the
     * unmodified value back to the csr and will cause side effects.
     */
    if (get_xl(ctx) < MXL_RV128) {
        if (a->rs1 == 0) {
            return do_csrr(ctx, a->rd, a->csr);
        }

        TCGv mask = tcg_constant_tl(a->rs1);
        return do_csrrw(ctx, a->rd, a->csr, ctx->zero, mask);
    } else {
        return false;
        // if (a->rs1 == 0) {
        //     return do_csrr_i128(ctx, a->rd, a->csr);
        // }

        // TCGv mask = tcg_constant_tl(a->rs1);
        // return do_csrrw_i128(ctx, a->rd, a->csr,
        //                      ctx->zero, ctx->zero, mask, ctx->zero);
    }
}
static bool trans_csrrs(DisasContext *ctx, arg_csrrs *a) {
    /*
     * If rs1 == 0, the insn shall not write to the csr at all, nor
     * cause any of the side effects that might occur on a csr write.
     * Note that if rs1 specifies a register other than x0, holding
     * a zero value, the instruction will still attempt to write the
     * unmodified value back to the csr and will cause side effects.
     */
    if (get_xl(ctx) < MXL_RV128) {
        if (a->rs1 == 0) {
            return do_csrr(ctx, a->rd, a->csr);
        }

        TCGv ones = tcg_constant_tl(-1);
        TCGv mask = get_gpr(ctx, a->rs1, EXT_ZERO);
        return do_csrrw(ctx, a->rd, a->csr, ones, mask);
    } else {
        return false;
        // if (a->rs1 == 0) {
        //     return do_csrr_i128(ctx, a->rd, a->csr);
        // }

        // TCGv ones = tcg_constant_tl(-1);
        // TCGv maskl = get_gpr(ctx, a->rs1, EXT_ZERO);
        // TCGv maskh = get_gprh(ctx, a->rs1);
        // return do_csrrw_i128(ctx, a->rd, a->csr, ones, ones, maskl, maskh);
    }
}
static bool trans_csrrsi(DisasContext *ctx, arg_csrrsi *a) {
    /*
     * If rs1 == 0, the insn shall not write to the csr at all, nor
     * cause any of the side effects that might occur on a csr write.
     * Note that if rs1 specifies a register other than x0, holding
     * a zero value, the instruction will still attempt to write the
     * unmodified value back to the csr and will cause side effects.
     */
    if (get_xl(ctx) < MXL_RV128) {
        if (a->rs1 == 0) {
            return do_csrr(ctx, a->rd, a->csr);
        }

        TCGv ones = tcg_constant_tl(-1);
        TCGv mask = tcg_constant_tl(a->rs1);
        return do_csrrw(ctx, a->rd, a->csr, ones, mask);
    } else {
        lsassert(0);
        // if (a->rs1 == 0) {
        //     return do_csrr_i128(ctx, a->rd, a->csr);
        // }

        // TCGv ones = tcg_constant_tl(-1);
        // TCGv mask = tcg_constant_tl(a->rs1);
        // return do_csrrw_i128(ctx, a->rd, a->csr, ones, ones, mask, ctx->zero);
    }
}
static bool trans_csrrw(DisasContext *ctx, arg_csrrw *a) {
    RISCVMXL xl = get_xl(ctx);
    if (xl < MXL_RV128) {
        TCGv src = get_gpr(ctx, a->rs1, EXT_NONE);

        /*
         * If rd == 0, the insn shall not read the csr, nor cause any of the
         * side effects that might occur on a csr read.
         */
        if (a->rd == 0) {
            return do_csrw(ctx, a->csr, src);
        }

        TCGv mask = tcg_constant_tl(xl == MXL_RV32 ? UINT32_MAX :
                                                     (target_ulong)-1);
        return do_csrrw(ctx, a->rd, a->csr, src, mask);
    } else {
        return false;
        // TCGv srcl = get_gpr(ctx, a->rs1, EXT_NONE);
        // TCGv srch = get_gprh(ctx, a->rs1);

        // /*
        //  * If rd == 0, the insn shall not read the csr, nor cause any of the
        //  * side effects that might occur on a csr read.
        //  */
        // if (a->rd == 0) {
        //     return do_csrw_i128(ctx, a->csr, srcl, srch);
        // }

        // TCGv mask = tcg_constant_tl(-1);
        // return do_csrrw_i128(ctx, a->rd, a->csr, srcl, srch, mask, mask);
    }
}
static bool trans_csrrwi(DisasContext *ctx, arg_csrrwi *a) {
    RISCVMXL xl = get_xl(ctx);
    if (xl < MXL_RV128) {
        TCGv src = tcg_constant_tl(a->rs1);

        /*
         * If rd == 0, the insn shall not read the csr, nor cause any of the
         * side effects that might occur on a csr read.
         */
        if (a->rd == 0) {
            return do_csrw(ctx, a->csr, src);
        }

        TCGv mask = tcg_constant_tl(xl == MXL_RV32 ? UINT32_MAX :
                                                     (target_ulong)-1);
        return do_csrrw(ctx, a->rd, a->csr, src, mask);
    } else {
        lsassert(0);
        // TCGv src = tcg_constant_tl(a->rs1);

        // /*
        //  * If rd == 0, the insn shall not read the csr, nor cause any of the
        //  * side effects that might occur on a csr read.
        //  */
        // if (a->rd == 0) {
        //     return do_csrw_i128(ctx, a->csr, src, ctx->zero);
        // }

        // TCGv mask = tcg_constant_tl(-1);
        // return do_csrrw_i128(ctx, a->rd, a->csr, src, ctx->zero, mask, mask);
    }
}
static bool trans_ctz(DisasContext *ctx, arg_ctz *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_ctzw(DisasContext *ctx, arg_ctzw *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_czero_eqz(DisasContext *ctx, arg_czero_eqz *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_czero_nez(DisasContext *ctx, arg_czero_nez *a) {__NOT_IMPLEMENTED_EXIT__}


/* Vector Floating-Point Classify Instruction */
target_ulong fclass_h(uint64_t frs1)
{
    float16 f = frs1;
    bool sign = float16_is_neg(f);

    if (float16_is_infinity(f)) {
        return sign ? 1 << 0 : 1 << 7;
    } else if (float16_is_zero(f)) {
        return sign ? 1 << 3 : 1 << 4;
    } else if (float16_is_zero_or_denormal(f)) {
        return sign ? 1 << 2 : 1 << 5;
    } else if (float16_is_any_nan(f)) {
        float_status s = { }; /* for snan_bit_is_one */
        return float16_is_quiet_nan(f, &s) ? 1 << 9 : 1 << 8;
    } else {
        return sign ? 1 << 1 : 1 << 6;
    }
}

target_ulong fclass_s(uint64_t frs1)
{
    float32 f = frs1;
    bool sign = float32_is_neg(f);

    if (float32_is_infinity(f)) {
        return sign ? 1 << 0 : 1 << 7;
    } else if (float32_is_zero(f)) {
        return sign ? 1 << 3 : 1 << 4;
    } else if (float32_is_zero_or_denormal(f)) {
        return sign ? 1 << 2 : 1 << 5;
    } else if (float32_is_any_nan(f)) {
        float_status s = { }; /* for snan_bit_is_one */
        return float32_is_quiet_nan(f, &s) ? 1 << 9 : 1 << 8;
    } else {
        return sign ? 1 << 1 : 1 << 6;
    }
}

target_ulong fclass_d(uint64_t frs1)
{
    float64 f = frs1;
    bool sign = float64_is_neg(f);

    if (float64_is_infinity(f)) {
        return sign ? 1 << 0 : 1 << 7;
    } else if (float64_is_zero(f)) {
        return sign ? 1 << 3 : 1 << 4;
    } else if (float64_is_zero_or_denormal(f)) {
        return sign ? 1 << 2 : 1 << 5;
    } else if (float64_is_any_nan(f)) {
        float_status s = { }; /* for snan_bit_is_one */
        return float64_is_quiet_nan(f, &s) ? 1 << 9 : 1 << 8;
    } else {
        return sign ? 1 << 1 : 1 << 6;
    }
}

static bool trans_fclass_d(DisasContext *ctx, arg_fclass_d *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fclass_h(DisasContext *ctx, arg_fclass_h *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fclass_s(DisasContext *ctx, arg_fclass_s *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fcvt_bf16_s(DisasContext *ctx, arg_fcvt_bf16_s *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fcvt_d_h(DisasContext *ctx, arg_fcvt_d_h *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fcvt_d_l(DisasContext *ctx, arg_fcvt_d_l *a) {
    REQUIRE_FPU;
    TCGv src = get_gpr(ctx, a->rs1, EXT_SIGN);
    gen_set_rm(ctx, a->rm);
    uint64_t dest = helper_fcvt_d_l(tcg_env, src);
    gen_set_fpr_d(ctx, a->rd, dest);
    return true;
}
static bool trans_fcvt_d_lu(DisasContext *ctx, arg_fcvt_d_lu *a) {
    REQUIRE_FPU;
    TCGv src = get_gpr(ctx, a->rs1, EXT_ZERO);
    gen_set_rm(ctx, a->rm);
    int64_t dest = helper_fcvt_d_lu(tcg_env, src);
    gen_set_fpr_d(ctx, a->rd, dest);
    return true;
}
static bool trans_fcvt_d_s(DisasContext *ctx, arg_fcvt_d_s *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fcvt_d_w(DisasContext *ctx, arg_fcvt_d_w *a) {
    REQUIRE_FPU;
    TCGv src = get_gpr(ctx, a->rs1, EXT_ZERO);
    gen_set_rm(ctx, a->rm);
    int64_t dest = helper_fcvt_d_w(tcg_env, (int32_t)src);
    gen_set_fpr_d(ctx, a->rd, dest);
    return true;
}
static bool trans_fcvt_d_wu(DisasContext *ctx, arg_fcvt_d_wu *a) {
    REQUIRE_FPU;
    TCGv src = get_gpr(ctx, a->rs1, EXT_ZERO);
    gen_set_rm(ctx, a->rm);
    int64_t dest = helper_fcvt_d_wu(tcg_env, (uint32_t)src);
    gen_set_fpr_d(ctx, a->rd, dest);
    return true;
}
static bool trans_fcvt_h_d(DisasContext *ctx, arg_fcvt_h_d *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fcvt_h_l(DisasContext *ctx, arg_fcvt_h_l *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fcvt_h_lu(DisasContext *ctx, arg_fcvt_h_lu *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fcvt_h_s(DisasContext *ctx, arg_fcvt_h_s *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fcvt_h_w(DisasContext *ctx, arg_fcvt_h_w *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fcvt_h_wu(DisasContext *ctx, arg_fcvt_h_wu *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fcvt_l_d(DisasContext *ctx, arg_fcvt_l_d *a) {
    REQUIRE_FPU;
    int64_t src = get_fpr_d(ctx, a->rs1);
    gen_set_rm(ctx, a->rm);
    int64_t dest = helper_fcvt_l_d(tcg_env, src);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}
static bool trans_fcvt_l_h(DisasContext *ctx, arg_fcvt_l_h *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fcvt_l_s(DisasContext *ctx, arg_fcvt_l_s *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fcvt_lu_d(DisasContext *ctx, arg_fcvt_lu_d *a) {
    REQUIRE_FPU;
    int64_t src = get_fpr_d(ctx, a->rs1);
    gen_set_rm(ctx, a->rm);
    int64_t dest = helper_fcvt_lu_d(tcg_env, src);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}
static bool trans_fcvt_lu_h(DisasContext *ctx, arg_fcvt_lu_h *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fcvt_lu_s(DisasContext *ctx, arg_fcvt_lu_s *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fcvtmod_w_d(DisasContext *ctx, arg_fcvtmod_w_d *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fcvt_s_bf16(DisasContext *ctx, arg_fcvt_s_bf16 *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fcvt_s_d(DisasContext *ctx, arg_fcvt_s_d *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fcvt_s_h(DisasContext *ctx, arg_fcvt_s_h *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fcvt_s_l(DisasContext *ctx, arg_fcvt_s_l *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fcvt_s_lu(DisasContext *ctx, arg_fcvt_s_lu *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fcvt_s_w(DisasContext *ctx, arg_fcvt_s_w *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fcvt_s_wu(DisasContext *ctx, arg_fcvt_s_wu *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fcvt_w_d(DisasContext *ctx, arg_fcvt_w_d *a) {
    REQUIRE_FPU;
    int64_t src = get_fpr_d(ctx, a->rs1);
    gen_set_rm(ctx, a->rm);
    int64_t dest = helper_fcvt_w_d(tcg_env, src);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}
static bool trans_fcvt_wu_d(DisasContext *ctx, arg_fcvt_wu_d *a) {
    REQUIRE_FPU;
    int64_t src = get_fpr_d(ctx, a->rs1);
    gen_set_rm(ctx, a->rm);
    int64_t dest = helper_fcvt_wu_d(tcg_env, src);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}
static bool trans_fcvt_w_h(DisasContext *ctx, arg_fcvt_w_h *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fcvt_w_s(DisasContext *ctx, arg_fcvt_w_s *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fcvt_wu_h(DisasContext *ctx, arg_fcvt_wu_h *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fcvt_wu_s(DisasContext *ctx, arg_fcvt_wu_s *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fadd_d(DisasContext *ctx, arg_fadd_d *a) {
    REQUIRE_FPU;
    int64_t src1 = get_fpr_d(ctx, a->rs1);
    int64_t src2 = get_fpr_d(ctx, a->rs2);
    gen_set_rm(ctx, a->rm);
    int64_t dest = helper_fadd_d(tcg_env, src1, src2);
    gen_set_fpr_d(ctx, a->rd, dest);
    return true;
}
static bool trans_fadd_h(DisasContext *ctx, arg_fadd_h *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fadd_s(DisasContext *ctx, arg_fadd_s *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fsub_d(DisasContext *ctx, arg_fsub_d *a) {
    REQUIRE_FPU;
    int64_t src1 = get_fpr_d(ctx, a->rs1);
    int64_t src2 = get_fpr_d(ctx, a->rs2);
    gen_set_rm(ctx, a->rm);
    int64_t dest = helper_fsub_d(tcg_env, src1, src2);
    gen_set_fpr_d(ctx, a->rd, dest);
    return true;
}
static bool trans_fsub_h(DisasContext *ctx, arg_fsub_h *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fsub_s(DisasContext *ctx, arg_fsub_s *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fmul_d(DisasContext *ctx, arg_fmul_d *a) {
    REQUIRE_FPU;
    int64_t src1 = get_fpr_d(ctx, a->rs1);
    int64_t src2 = get_fpr_d(ctx, a->rs2);
    gen_set_rm(ctx, a->rm);
    int64_t dest = helper_fmul_d(tcg_env, src1, src2);
    gen_set_fpr_d(ctx, a->rd, dest);
    return true;
}
static bool trans_fmul_h(DisasContext *ctx, arg_fmul_h *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fmul_s(DisasContext *ctx, arg_fmul_s *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fdiv_d(DisasContext *ctx, arg_fdiv_d *a) {
    REQUIRE_FPU;
    int64_t src1 = get_fpr_d(ctx, a->rs1);
    int64_t src2 = get_fpr_d(ctx, a->rs2);
    gen_set_rm(ctx, a->rm);
    int64_t dest = helper_fdiv_d(tcg_env, src1, src2);
    gen_set_fpr_d(ctx, a->rd, dest);
    return true;
}
static bool trans_fdiv_h(DisasContext *ctx, arg_fdiv_h *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fdiv_s(DisasContext *ctx, arg_fdiv_s *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fence(DisasContext *ctx, arg_fence *a) {return true;}
static bool trans_fence_i(DisasContext *ctx, arg_fence_i *a) {return true;}
static bool trans_feq_d(DisasContext *ctx, arg_feq_d *a) {
    REQUIRE_FPU;
    int64_t src1 = get_fpr_d(ctx, a->rs1);
    int64_t src2 = get_fpr_d(ctx, a->rs2);
    int64_t dest = helper_feq_d(tcg_env, src1, src2);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}
static bool trans_feq_h(DisasContext *ctx, arg_feq_h *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_feq_s(DisasContext *ctx, arg_feq_s *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fle_d(DisasContext *ctx, arg_fle_d *a) {
    REQUIRE_FPU;
    int64_t src1 = get_fpr_d(ctx, a->rs1);
    int64_t src2 = get_fpr_d(ctx, a->rs2);
    int64_t dest = helper_fle_d(tcg_env, src1, src2);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}
static bool trans_fle_h(DisasContext *ctx, arg_fle_h *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fleq_d(DisasContext *ctx, arg_fleq_d *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fleq_h(DisasContext *ctx, arg_fleq_h *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fleq_s(DisasContext *ctx, arg_fleq_s *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fle_s(DisasContext *ctx, arg_fle_s *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_flh(DisasContext *ctx, arg_flh *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fli_d(DisasContext *ctx, arg_fli_d *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fli_h(DisasContext *ctx, arg_fli_h *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fli_s(DisasContext *ctx, arg_fli_s *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_flt_d(DisasContext *ctx, arg_flt_d *a) {
    REQUIRE_FPU;
    int64_t src1 = get_fpr_d(ctx, a->rs1);
    int64_t src2 = get_fpr_d(ctx, a->rs2);
    int64_t dest = helper_flt_d(tcg_env, src1, src2);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}
static bool trans_flt_h(DisasContext *ctx, arg_flt_h *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fltq_d(DisasContext *ctx, arg_fltq_d *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fltq_h(DisasContext *ctx, arg_fltq_h *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fltq_s(DisasContext *ctx, arg_fltq_s *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_flt_s(DisasContext *ctx, arg_flt_s *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_flw(DisasContext *ctx, arg_flw *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fmadd_d(DisasContext *ctx, arg_fmadd_d *a) {
    REQUIRE_FPU;
    int64_t src1 = get_fpr_d(ctx, a->rs1);
    int64_t src2 = get_fpr_d(ctx, a->rs2);
    int64_t src3 = get_fpr_d(ctx, a->rs3);
    gen_set_rm(ctx, a->rm);
    int64_t dest = helper_fmadd_d(tcg_env, src1, src2, src3);
    gen_set_fpr_d(ctx, a->rd, dest);
    return true;
}
static bool trans_fmadd_h(DisasContext *ctx, arg_fmadd_h *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fmadd_s(DisasContext *ctx, arg_fmadd_s *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fmax_d(DisasContext *ctx, arg_fmax_d *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fmax_h(DisasContext *ctx, arg_fmax_h *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fmaxm_d(DisasContext *ctx, arg_fmaxm_d *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fmaxm_h(DisasContext *ctx, arg_fmaxm_h *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fmaxm_s(DisasContext *ctx, arg_fmaxm_s *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fmax_s(DisasContext *ctx, arg_fmax_s *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fmin_d(DisasContext *ctx, arg_fmin_d *a) {
    REQUIRE_FPU;
    int64_t src1 = get_fpr_d(ctx, a->rs1);
    int64_t src2 = get_fpr_d(ctx, a->rs2);
    int64_t dest = helper_fmin_d(tcg_env, src1, src2);
    gen_set_fpr_d(ctx, a->rd, dest);
    return true;
}
static bool trans_fmin_h(DisasContext *ctx, arg_fmin_h *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fminm_d(DisasContext *ctx, arg_fminm_d *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fminm_h(DisasContext *ctx, arg_fminm_h *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fminm_s(DisasContext *ctx, arg_fminm_s *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fmin_s(DisasContext *ctx, arg_fmin_s *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fmsub_d(DisasContext *ctx, arg_fmsub_d *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fmsub_h(DisasContext *ctx, arg_fmsub_h *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fmsub_s(DisasContext *ctx, arg_fmsub_s *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fmv_d_x(DisasContext *ctx, arg_fmv_d_x *a) {
    REQUIRE_FPU;
    gen_set_fpr_d(ctx, a->rd, get_gpr(ctx, a->rs1, EXT_NONE));
    return true;
}
static bool trans_fmvh_x_d(DisasContext *ctx, arg_fmvh_x_d *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fmv_h_x(DisasContext *ctx, arg_fmv_h_x *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fmvp_d_x(DisasContext *ctx, arg_fmvp_d_x *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fmv_w_x(DisasContext *ctx, arg_fmv_w_x *a) {
    REQUIRE_FPU;
    TCGv_i64 dest;
    TCGv src = get_gpr(ctx, a->rs1, EXT_ZERO);
    dest = src;
    // tcg_gen_extu_tl_i64(dest, src);
    gen_nanbox_s(dest, dest);
    gen_set_fpr_hs(ctx, a->rd, dest);
    // mark_fs_dirty(ctx);
    return true;
}
static bool trans_fmv_x_d(DisasContext *ctx, arg_fmv_x_d *a) {
    REQUIRE_FPU;
    gen_set_gpr(ctx, a->rd, get_fpr_d(ctx, a->rs1));
    return true;
}
static bool trans_fmv_x_h(DisasContext *ctx, arg_fmv_x_h *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fmv_x_w(DisasContext *ctx, arg_fmv_x_w *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fnmadd_d(DisasContext *ctx, arg_fnmadd_d *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fnmadd_h(DisasContext *ctx, arg_fnmadd_h *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fnmadd_s(DisasContext *ctx, arg_fnmadd_s *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fnmsub_d(DisasContext *ctx, arg_fnmsub_d *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fnmsub_h(DisasContext *ctx, arg_fnmsub_h *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fnmsub_s(DisasContext *ctx, arg_fnmsub_s *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fround_d(DisasContext *ctx, arg_fround_d *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fround_h(DisasContext *ctx, arg_fround_h *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_froundnx_d(DisasContext *ctx, arg_froundnx_d *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_froundnx_h(DisasContext *ctx, arg_froundnx_h *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_froundnx_s(DisasContext *ctx, arg_froundnx_s *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fround_s(DisasContext *ctx, arg_fround_s *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fld(DisasContext *ctx, arg_fld *a) {
    REQUIRE_FPU;
    target_ulong addr = get_address(ctx, a->rs1, a->imm);
    target_ulong dest = ld_d(env, addr);
    gen_set_fpr_d(ctx, a->rd, dest);
    return true;
}

static bool trans_fsd(DisasContext *ctx, arg_fsd *a) {
    REQUIRE_FPU;
    target_ulong addr = get_address(ctx, a->rs1, a->imm);
    target_ulong data = get_fpr_d(ctx, a->rs2);
    st_d(env, addr, data);
    return true;
}
static bool trans_fsgnj_d(DisasContext *ctx, arg_fsgnj_d *a) {
    REQUIRE_FPU;
    int64_t src1 = get_fpr_d(ctx, a->rs1);
    uint64_t dest;
    if (a->rs1 == a->rs2) { /* FMOV */
        dest = get_fpr_d(ctx, a->rs1);
    } else {
        int64_t src2 = get_fpr_d(ctx, a->rs2);
        dest = deposit64(src2, 0, 63, src1);
    }
    gen_set_fpr_d(ctx, a->rd, dest);
    return true;
}
static bool trans_fsgnj_h(DisasContext *ctx, arg_fsgnj_h *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fsgnjn_d(DisasContext *ctx, arg_fsgnjn_d *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fsgnjn_h(DisasContext *ctx, arg_fsgnjn_h *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fsgnjn_s(DisasContext *ctx, arg_fsgnjn_s *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fsgnj_s(DisasContext *ctx, arg_fsgnj_s *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fsgnjx_d(DisasContext *ctx, arg_fsgnjx_d *a) {
    REQUIRE_FPU;
    int64_t src1 = get_fpr_d(ctx, a->rs1);
    uint64_t dest;
    if (a->rs1 == a->rs2) { /* FABS */
        dest = src1 & (~INT64_MIN);
    } else {
        int64_t src2 = get_fpr_d(ctx, a->rs2);
        dest = src1 ^ (src2 & INT64_MIN);
    }
    gen_set_fpr_d(ctx, a->rd, dest);
    return true;
}
static bool trans_fsgnjx_h(DisasContext *ctx, arg_fsgnjx_h *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fsgnjx_s(DisasContext *ctx, arg_fsgnjx_s *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fsh(DisasContext *ctx, arg_fsh *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fsqrt_d(DisasContext *ctx, arg_fsqrt_d *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fsqrt_h(DisasContext *ctx, arg_fsqrt_h *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fsqrt_s(DisasContext *ctx, arg_fsqrt_s *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_fsw(DisasContext *ctx, arg_fsw *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_hfence_gvma(DisasContext *ctx, arg_hfence_gvma *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_hfence_vvma(DisasContext *ctx, arg_hfence_vvma *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_hinval_gvma(DisasContext *ctx, arg_hinval_gvma *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_hinval_vvma(DisasContext *ctx, arg_hinval_vvma *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_hlv_b(DisasContext *ctx, arg_hlv_b *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_hlv_bu(DisasContext *ctx, arg_hlv_bu *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_hlv_d(DisasContext *ctx, arg_hlv_d *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_hlv_h(DisasContext *ctx, arg_hlv_h *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_hlv_hu(DisasContext *ctx, arg_hlv_hu *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_hlv_w(DisasContext *ctx, arg_hlv_w *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_hlv_wu(DisasContext *ctx, arg_hlv_wu *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_hlvx_hu(DisasContext *ctx, arg_hlvx_hu *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_hlvx_wu(DisasContext *ctx, arg_hlvx_wu *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_hsv_b(DisasContext *ctx, arg_hsv_b *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_hsv_d(DisasContext *ctx, arg_hsv_d *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_hsv_h(DisasContext *ctx, arg_hsv_h *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_hsv_w(DisasContext *ctx, arg_hsv_w *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_jal(DisasContext *ctx, arg_jal *a) {
    PERF_INC(COUNTER_INST_BRANCH);
    TCGv target_pc = gen_pc_plus_diff(0, ctx, a->imm);
    TCGv succ_pc = gen_pc_plus_diff(0, ctx, env->cur_insn_len);
    gen_set_gpr(ctx, a->rd, succ_pc);
    cpu_set_pc(ctx, target_pc);
    env->cur_insn_len = 0;
    return true;
}
static bool trans_jalr(DisasContext *ctx, arg_jalr *a) {
    PERF_INC(COUNTER_INST_BRANCH);
    TCGv target_pc = get_gpr(ctx, a->rs1, EXT_NONE) + a->imm;
    TCGv succ_pc = gen_pc_plus_diff(0, ctx, env->cur_insn_len);
    gen_set_gpr(ctx, a->rd, succ_pc);
    cpu_set_pc(ctx, target_pc);
    env->cur_insn_len = 0;
    return true;
}
static bool trans_lb(DisasContext *ctx, arg_lb *a) {
    target_ulong addr = get_address(ctx, a->rs1, a->imm);
    target_ulong dest = (int64_t)ld_b(env, addr);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}
static bool trans_lbu(DisasContext *ctx, arg_lbu *a) {
    target_ulong addr = get_address(ctx, a->rs1, a->imm);
    target_ulong dest = (uint8_t)ld_b(env, addr);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}
static bool trans_lh(DisasContext *ctx, arg_lh *a) {
    target_ulong addr = get_address(ctx, a->rs1, a->imm);
    target_ulong dest = (int64_t)ld_h(env, addr);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}
static bool trans_lhu(DisasContext *ctx, arg_lhu *a) {
    target_ulong addr = get_address(ctx, a->rs1, a->imm);
    target_ulong dest = (uint16_t)ld_h(env, addr);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}
static bool trans_lw(DisasContext *ctx, arg_lw *a) {
    target_ulong addr = get_address(ctx, a->rs1, a->imm);
    target_ulong dest = (int64_t)ld_w(env, addr);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}
static bool trans_lwu(DisasContext *ctx, arg_lwu *a) {
    target_ulong addr = get_address(ctx, a->rs1, a->imm);
    target_ulong dest = (uint32_t)ld_w(env, addr);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}
static bool trans_ld(DisasContext *ctx, arg_ld *a) {
    target_ulong addr = get_address(ctx, a->rs1, a->imm);
    target_ulong dest = (int64_t)ld_d(env, addr);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}
static bool trans_lpad(DisasContext *ctx, arg_lpad *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_lr_w(DisasContext *ctx, arg_lr_w *a) {
    target_ulong addr = get_address(ctx, a->rs1, 0);
    target_ulong dest = ld_w(env, addr);
    env->load_res = addr;
    env->load_val = dest;
    gen_set_gpr(ctx, a->rd, (int32_t)dest);
    return true;
}
static bool trans_lr_d(DisasContext *ctx, arg_lr_d *a) {
    target_ulong addr = get_address(ctx, a->rs1, 0);
    target_ulong dest = ld_d(env, addr);
    env->load_res = addr;
    env->load_val = dest;
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}
static bool trans_sc_w(DisasContext *ctx, arg_sc_w *a) {
    target_ulong addr = get_address(ctx, a->rs1, 0);
    target_ulong data = get_gpr(ctx, a->rs2, EXT_NONE);
    if (addr != env->load_res || ld_w(env, addr) != env->load_val) {
        gen_set_gpr(ctx, a->rd, 1);
    } else {
        st_w(ctx, addr, data);
        gen_set_gpr(ctx, a->rd, 0);
    }
    env->load_res = -1;
    return true;
}
static bool trans_sc_d(DisasContext *ctx, arg_sc_d *a) {
    target_ulong addr = get_address(ctx, a->rs1, 0);
    target_ulong data = get_gpr(ctx, a->rs2, EXT_NONE);
    if (addr != env->load_res || ld_d(env, addr) != env->load_val) {
        gen_set_gpr(ctx, a->rd, 1);
    } else {
        st_d(ctx, addr, data);
        gen_set_gpr(ctx, a->rd, 0);
    }
    env->load_res = -1;
    return true;
}
static bool trans_lui(DisasContext *ctx, arg_lui *a) {
    gen_set_gpri(ctx, a->rd, a->imm);
    return true;
}
static bool trans_mop_r_n(DisasContext *ctx, arg_mop_r_n *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_mop_rr_n(DisasContext *ctx, arg_mop_rr_n *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_mul(DisasContext *ctx, arg_mul *a) {
    target_long src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    target_long src2 = get_gpr(ctx, a->rs2, EXT_NONE);
    target_long dest = (target_long)src1 * (target_long)src2;
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}
static bool trans_div(DisasContext *ctx, arg_div *a) {
    target_long src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    target_long src2 = get_gpr(ctx, a->rs2, EXT_NONE);
    target_long dest;
    if (src2 == 0) {
        dest = -1;
    } else if (get_xl(ctx) == MXL_RV32 && src1 == INT32_MIN && (int32_t)src2 == -1) {
        dest = src1;
    } else if (get_xl(ctx) == MXL_RV64 && src1 == INT64_MIN && (int64_t)src2 == -1) {
        dest = src1;
    } else {
        dest = (target_long)src1 / (target_long)src2;
    }
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}
static bool trans_rem(DisasContext *ctx, arg_rem *a) {
    target_long src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    target_long src2 = get_gpr(ctx, a->rs2, EXT_NONE);
    target_long dest = (target_long)src1 % (target_long)src2;
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}
static bool trans_divu(DisasContext *ctx, arg_divu *a) {
    target_long src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    target_long src2 = get_gpr(ctx, a->rs2, EXT_NONE);
    target_ulong dest = (target_ulong)src1 / (target_ulong)src2;
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}
static bool trans_remu(DisasContext *ctx, arg_remu *a) {
    target_long src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    target_long src2 = get_gpr(ctx, a->rs2, EXT_NONE);
    target_ulong dest = (target_ulong)src1 % (target_ulong)src2;
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}
static bool trans_mulh(DisasContext *ctx, arg_mulh *a) {
    target_long src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    target_long src2 = get_gpr(ctx, a->rs2, EXT_NONE);
    target_long dest;
    if (get_xl(ctx) == MXL_RV32) {
        dest = (int64_t)(int32_t)src1 * (int64_t)(int32_t)src2;
        dest >>= 32;
    } else {
        __int128_t t = (__int128_t)(int64_t)src1 * (__int128_t)(int64_t)src2;
        dest = t >> 64;
    }
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}
static bool trans_mulhsu(DisasContext *ctx, arg_mulhsu *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_mulhu(DisasContext *ctx, arg_mulhu *a) {
    target_long src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    target_long src2 = get_gpr(ctx, a->rs2, EXT_NONE);
    target_long dest;
    if (get_xl(ctx) == MXL_RV32) {
        dest = (uint64_t)(uint32_t)src1 * (uint64_t)(uint32_t)src2;
        dest >>= 32;
    } else {
        __uint128_t t = (__uint128_t)(uint64_t)src1 * (__uint128_t)(uint64_t)src2;
        dest = t >> 64;
    }
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}
static bool trans_mulw(DisasContext *ctx, arg_mulw *a) {
    ctx->ol = MXL_RV32;
    target_long src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    target_long src2 = get_gpr(ctx, a->rs2, EXT_NONE);
    target_long dest = (int32_t)src1 * (int32_t)src2;
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}
static bool trans_divw(DisasContext *ctx, arg_divw *a) {
    ctx->ol = MXL_RV32;
    target_long src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    target_long src2 = get_gpr(ctx, a->rs2, EXT_NONE);
    target_long dest = (int32_t)src1 / (int32_t)src2;
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}
static bool trans_divuw(DisasContext *ctx, arg_divuw *a) {
    ctx->ol = MXL_RV32;
    target_long src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    target_long src2 = get_gpr(ctx, a->rs2, EXT_NONE);
    target_ulong dest = (uint32_t)src1 / (uint32_t)src2;
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}
static bool trans_remw(DisasContext *ctx, arg_remw *a) {
    ctx->ol = MXL_RV32;
    target_long src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    target_long src2 = get_gpr(ctx, a->rs2, EXT_NONE);
    target_ulong dest = (int32_t)src1 % (int32_t)src2;
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}
static bool trans_remuw(DisasContext *ctx, arg_remuw *a) {
    ctx->ol = MXL_RV32;
    target_long src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    target_long src2 = get_gpr(ctx, a->rs2, EXT_NONE);
    target_ulong dest = (uint32_t)src1 % (uint32_t)src2;
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}
static bool trans_sb(DisasContext *ctx, arg_sb *a) {
    target_ulong addr = get_address(ctx, a->rs1, a->imm);
    target_ulong data = get_gpr(ctx, a->rs2, EXT_NONE);
    st_b(env, addr, data);
    return true;
}
static bool trans_sh(DisasContext *ctx, arg_sh *a) {
    target_ulong addr = get_address(ctx, a->rs1, a->imm);
    target_ulong data = get_gpr(ctx, a->rs2, EXT_NONE);
    st_h(env, addr, data);
    return true;
}
static bool trans_sw(DisasContext *ctx, arg_sw *a) {
    target_ulong addr = get_address(ctx, a->rs1, a->imm);
    target_ulong data = get_gpr(ctx, a->rs2, EXT_NONE);
    st_w(env, addr, data);
    return true;
}
static bool trans_sd(DisasContext *ctx, arg_sd *a) {
    target_ulong addr = get_address(ctx, a->rs1, a->imm);
    target_ulong data = get_gpr(ctx, a->rs2, EXT_NONE);
    st_d(env, addr, data);
    return true;
}
static bool trans_sext_b(DisasContext *ctx, arg_sext_b *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_sext_h(DisasContext *ctx, arg_sext_h *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_sh1add(DisasContext *ctx, arg_sh1add *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_sh1add_uw(DisasContext *ctx, arg_sh1add_uw *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_sh2add(DisasContext *ctx, arg_sh2add *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_sh2add_uw(DisasContext *ctx, arg_sh2add_uw *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_sh3add(DisasContext *ctx, arg_sh3add *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_sh3add_uw(DisasContext *ctx, arg_sh3add_uw *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_sha256sig0(DisasContext *ctx, arg_sha256sig0 *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_sha256sig1(DisasContext *ctx, arg_sha256sig1 *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_sha256sum0(DisasContext *ctx, arg_sha256sum0 *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_sha256sum1(DisasContext *ctx, arg_sha256sum1 *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_sha512sig0(DisasContext *ctx, arg_sha512sig0 *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_sha512sig0h(DisasContext *ctx, arg_sha512sig0h *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_sha512sig0l(DisasContext *ctx, arg_sha512sig0l *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_sha512sig1(DisasContext *ctx, arg_sha512sig1 *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_sha512sig1h(DisasContext *ctx, arg_sha512sig1h *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_sha512sig1l(DisasContext *ctx, arg_sha512sig1l *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_sha512sum0(DisasContext *ctx, arg_sha512sum0 *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_sha512sum0r(DisasContext *ctx, arg_sha512sum0r *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_sha512sum1(DisasContext *ctx, arg_sha512sum1 *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_sha512sum1r(DisasContext *ctx, arg_sha512sum1r *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_sinval_vma(DisasContext *ctx, arg_sinval_vma *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_sll(DisasContext *ctx, arg_sll *a) {
    target_ulong src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    target_ulong src2 = get_gpr(ctx, a->rs2, EXT_NONE);
    if (get_xl(ctx) == MXL_RV32) {
        src1 = (uint32_t)src1;
        src2 &= 0x1f;
    } else {
        src2 &= 0x3f;
    }
    TCGv dest = src1 << src2;
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}
static bool trans_slli(DisasContext *ctx, arg_slli *a) {
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv dest = src1 << a->shamt;
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}
static bool trans_sra(DisasContext *ctx, arg_sra *a) {
    target_long src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    target_ulong src2 = get_gpr(ctx, a->rs2, EXT_NONE);
    if (get_xl(ctx) == MXL_RV32) {
        src1 = (int32_t)src1;
        src2 &= 0x1f;
    } else {
        src2 &= 0x3f;
    }
    TCGv dest = (target_long)src1 >> src2;
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}
static bool trans_srai(DisasContext *ctx, arg_srai *a) {
    target_long src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    if (get_xl(ctx) == MXL_RV32) {
        src1 = (int32_t)src1;
    }
    TCGv dest = src1 >> a->shamt;
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}
static bool trans_srl(DisasContext *ctx, arg_srl *a) {
    target_ulong src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    target_ulong src2 = get_gpr(ctx, a->rs2, EXT_NONE);
    if (get_xl(ctx) == MXL_RV32) {
        src1 = (uint32_t)src1;
        src2 &= 0x1f;
    } else {
        src2 &= 0x3f;
    }
    TCGv dest = src1 >> src2;
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}
static bool trans_srli(DisasContext *ctx, arg_srli *a) {
    target_ulong src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    if (get_xl(ctx) == MXL_RV32) {
        src1 = (uint32_t)src1;
    }
    TCGv dest = src1 >> a->shamt;
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}


static bool trans_slt(DisasContext *ctx, arg_slt *a) {
    target_ulong src1 = get_gpr(ctx, a->rs1, EXT_SIGN);
    target_ulong src2 = get_gpr(ctx, a->rs2, EXT_SIGN);
    bool taken = (target_long)src1 < (target_long)src2;
    gen_set_gpr(env, a->rd, taken);
    return true;
}
static bool trans_slti(DisasContext *ctx, arg_slti *a) {
    target_ulong src1 = get_gpr(ctx, a->rs1, EXT_SIGN);
    bool taken = (target_long)src1 < (target_long)(target_long)a->imm;
    gen_set_gpr(env, a->rd, taken);
    return true;
}
static bool trans_sltu(DisasContext *ctx, arg_sltu *a) {
    target_ulong src1 = get_gpr(ctx, a->rs1, EXT_SIGN);
    target_ulong src2 = get_gpr(ctx, a->rs2, EXT_SIGN);
    bool taken = (target_ulong)src1 < (target_ulong)src2;
    gen_set_gpr(env, a->rd, taken);
    return true;
}
static bool trans_sltiu(DisasContext *ctx, arg_sltiu *a) {
    target_ulong src1 = get_gpr(ctx, a->rs1, EXT_SIGN);
    bool taken = (target_ulong)src1 < (target_ulong)(target_long)a->imm;
    gen_set_gpr(env, a->rd, taken);
    return true;
}
static bool trans_sm3p0(DisasContext *ctx, arg_sm3p0 *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_sm3p1(DisasContext *ctx, arg_sm3p1 *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_sm4ed(DisasContext *ctx, arg_sm4ed *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_sm4ks(DisasContext *ctx, arg_sm4ks *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_ssamoswap_d(DisasContext *ctx, arg_ssamoswap_d *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_ssamoswap_w(DisasContext *ctx, arg_ssamoswap_w *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_sspopchk(DisasContext *ctx, arg_sspopchk *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_sspush(DisasContext *ctx, arg_sspush *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_ssrdp(DisasContext *ctx, arg_ssrdp *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_unzip(DisasContext *ctx, arg_unzip *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_uret(DisasContext *ctx, arg_uret *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_pause(DisasContext *ctx, arg_pause *a) {return true;}
static bool trans_sret(DisasContext *ctx, arg_sret *a) {
#ifndef CONFIG_USER_ONLY
    // decode_save_opc(ctx, 0);
    // translator_io_start(&ctx->base);
    env->pc = helper_sret(tcg_env);
    env->cur_insn_len = 0;
    return true;
#else
    return false;
#endif
}
static bool trans_mret(DisasContext *ctx, arg_mret *a) {
#ifndef CONFIG_USER_ONLY
    // decode_save_opc(ctx, 0);
    // translator_io_start(&ctx->base);
    env->pc = helper_mret(tcg_env);
    env->cur_insn_len = 0;
    return true;
#else
    return false;
#endif
}
static bool trans_wfi(DisasContext *ctx, arg_wfi *a) {
#ifndef CONFIG_USER_ONLY
    if (!determined) {
        while (loongarch_cpu_check_irq(env), !loongarch_cpu_has_irq(env)) {
            sleep(1);
        }
    }
    return true;
#else
    return false;
#endif
}
static bool trans_ebreak(DisasContext *ctx, arg_ebreak *a) {
    env->badaddr = env->pc;
    helper_raise_exception(env, RISCV_EXCP_BREAKPOINT);
    return true;
}
static bool trans_ecall(DisasContext *ctx, arg_ecall *a) {
    env->syscall_count ++;
#if defined(CONFIG_USER_ONLY)
    target_long ret = do_syscall(env, env->gpr[17],
                        env->gpr[10], env->gpr[11],
                        env->gpr[12], env->gpr[13],
                        env->gpr[14], env->gpr[15],
                        -1, -1);
    env->gpr[10] = ret;
#else
    // rv test
    // if (env->gpr[17] == 93) {
    //     if (env->gpr[10] == 0) {
    //         printf("\n%ld pass\n", env->gpr[3]);
    //     } else {
    //         printf("\n%ld fail\n", env->gpr[10]);
    //     }
    // }
    // exit(0);
    riscv_raise_exception(env, RISCV_EXCP_U_ECALL, 0);
#endif
    return true;
}

static bool trans_sfence_inval_ir(DisasContext *ctx, arg_sfence_inval_ir *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_sfence_vma(DisasContext *ctx, arg_sfence_vma *a) {
    tlb_flush(env_cpu(env));
    return true;
}
static bool trans_sfence_vm(DisasContext *ctx, arg_sfence_vm *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_sfence_w_inval(DisasContext *ctx, arg_sfence_w_inval *a) {__NOT_IMPLEMENTED_EXIT__}

static bool trans_vaaddu_vv(DisasContext *ctx, arg_vaaddu_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vaaddu_vx(DisasContext *ctx, arg_vaaddu_vx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vaadd_vv(DisasContext *ctx, arg_vaadd_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vaadd_vx(DisasContext *ctx, arg_vaadd_vx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vadc_vim(DisasContext *ctx, arg_vadc_vim *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vadc_vvm(DisasContext *ctx, arg_vadc_vvm *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vadc_vxm(DisasContext *ctx, arg_vadc_vxm *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vadd_vi(DisasContext *ctx, arg_vadd_vi *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vadd_vv(DisasContext *ctx, arg_vadd_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vadd_vx(DisasContext *ctx, arg_vadd_vx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vaesdf_vs(DisasContext *ctx, arg_vaesdf_vs *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vaesdf_vv(DisasContext *ctx, arg_vaesdf_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vaesdm_vs(DisasContext *ctx, arg_vaesdm_vs *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vaesdm_vv(DisasContext *ctx, arg_vaesdm_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vaesef_vs(DisasContext *ctx, arg_vaesef_vs *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vaesef_vv(DisasContext *ctx, arg_vaesef_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vaesem_vs(DisasContext *ctx, arg_vaesem_vs *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vaesem_vv(DisasContext *ctx, arg_vaesem_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vaeskf1_vi(DisasContext *ctx, arg_vaeskf1_vi *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vaeskf2_vi(DisasContext *ctx, arg_vaeskf2_vi *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vaesz_vs(DisasContext *ctx, arg_vaesz_vs *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vandn_vv(DisasContext *ctx, arg_vandn_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vandn_vx(DisasContext *ctx, arg_vandn_vx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vand_vi(DisasContext *ctx, arg_vand_vi *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vand_vv(DisasContext *ctx, arg_vand_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vand_vx(DisasContext *ctx, arg_vand_vx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vasubu_vv(DisasContext *ctx, arg_vasubu_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vasubu_vx(DisasContext *ctx, arg_vasubu_vx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vasub_vv(DisasContext *ctx, arg_vasub_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vasub_vx(DisasContext *ctx, arg_vasub_vx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vbrev8_v(DisasContext *ctx, arg_vbrev8_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vbrev_v(DisasContext *ctx, arg_vbrev_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vclmulh_vv(DisasContext *ctx, arg_vclmulh_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vclmulh_vx(DisasContext *ctx, arg_vclmulh_vx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vclmul_vv(DisasContext *ctx, arg_vclmul_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vclmul_vx(DisasContext *ctx, arg_vclmul_vx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vclz_v(DisasContext *ctx, arg_vclz_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vcompress_vm(DisasContext *ctx, arg_vcompress_vm *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vcpop_m(DisasContext *ctx, arg_vcpop_m *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vcpop_v(DisasContext *ctx, arg_vcpop_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vctz_v(DisasContext *ctx, arg_vctz_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vdivu_vv(DisasContext *ctx, arg_vdivu_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vdivu_vx(DisasContext *ctx, arg_vdivu_vx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vdiv_vv(DisasContext *ctx, arg_vdiv_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vdiv_vx(DisasContext *ctx, arg_vdiv_vx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfadd_vf(DisasContext *ctx, arg_vfadd_vf *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfadd_vv(DisasContext *ctx, arg_vfadd_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfclass_v(DisasContext *ctx, arg_vfclass_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfcvt_f_xu_v(DisasContext *ctx, arg_vfcvt_f_xu_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfcvt_f_x_v(DisasContext *ctx, arg_vfcvt_f_x_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfcvt_rtz_x_f_v(DisasContext *ctx, arg_vfcvt_rtz_x_f_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfcvt_rtz_xu_f_v(DisasContext *ctx, arg_vfcvt_rtz_xu_f_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfcvt_x_f_v(DisasContext *ctx, arg_vfcvt_x_f_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfcvt_xu_f_v(DisasContext *ctx, arg_vfcvt_xu_f_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfdiv_vf(DisasContext *ctx, arg_vfdiv_vf *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfdiv_vv(DisasContext *ctx, arg_vfdiv_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfirst_m(DisasContext *ctx, arg_vfirst_m *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfmacc_vf(DisasContext *ctx, arg_vfmacc_vf *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfmacc_vv(DisasContext *ctx, arg_vfmacc_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfmadd_vf(DisasContext *ctx, arg_vfmadd_vf *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfmadd_vv(DisasContext *ctx, arg_vfmadd_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfmax_vf(DisasContext *ctx, arg_vfmax_vf *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfmax_vv(DisasContext *ctx, arg_vfmax_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfmerge_vfm(DisasContext *ctx, arg_vfmerge_vfm *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfmin_vf(DisasContext *ctx, arg_vfmin_vf *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfmin_vv(DisasContext *ctx, arg_vfmin_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfmsac_vf(DisasContext *ctx, arg_vfmsac_vf *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfmsac_vv(DisasContext *ctx, arg_vfmsac_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfmsub_vf(DisasContext *ctx, arg_vfmsub_vf *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfmsub_vv(DisasContext *ctx, arg_vfmsub_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfmul_vf(DisasContext *ctx, arg_vfmul_vf *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfmul_vv(DisasContext *ctx, arg_vfmul_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfmv_f_s(DisasContext *ctx, arg_vfmv_f_s *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfmv_s_f(DisasContext *ctx, arg_vfmv_s_f *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfmv_v_f(DisasContext *ctx, arg_vfmv_v_f *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfncvtbf16_f_f_w(DisasContext *ctx, arg_vfncvtbf16_f_f_w *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfncvt_f_f_w(DisasContext *ctx, arg_vfncvt_f_f_w *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfncvt_f_xu_w(DisasContext *ctx, arg_vfncvt_f_xu_w *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfncvt_f_x_w(DisasContext *ctx, arg_vfncvt_f_x_w *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfncvt_rod_f_f_w(DisasContext *ctx, arg_vfncvt_rod_f_f_w *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfncvt_rtz_x_f_w(DisasContext *ctx, arg_vfncvt_rtz_x_f_w *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfncvt_rtz_xu_f_w(DisasContext *ctx, arg_vfncvt_rtz_xu_f_w *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfncvt_x_f_w(DisasContext *ctx, arg_vfncvt_x_f_w *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfncvt_xu_f_w(DisasContext *ctx, arg_vfncvt_xu_f_w *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfnmacc_vf(DisasContext *ctx, arg_vfnmacc_vf *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfnmacc_vv(DisasContext *ctx, arg_vfnmacc_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfnmadd_vf(DisasContext *ctx, arg_vfnmadd_vf *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfnmadd_vv(DisasContext *ctx, arg_vfnmadd_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfnmsac_vf(DisasContext *ctx, arg_vfnmsac_vf *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfnmsac_vv(DisasContext *ctx, arg_vfnmsac_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfnmsub_vf(DisasContext *ctx, arg_vfnmsub_vf *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfnmsub_vv(DisasContext *ctx, arg_vfnmsub_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfrdiv_vf(DisasContext *ctx, arg_vfrdiv_vf *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfrec7_v(DisasContext *ctx, arg_vfrec7_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfredmax_vs(DisasContext *ctx, arg_vfredmax_vs *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfredmin_vs(DisasContext *ctx, arg_vfredmin_vs *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfredosum_vs(DisasContext *ctx, arg_vfredosum_vs *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfredusum_vs(DisasContext *ctx, arg_vfredusum_vs *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfrsqrt7_v(DisasContext *ctx, arg_vfrsqrt7_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfrsub_vf(DisasContext *ctx, arg_vfrsub_vf *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfsgnjn_vf(DisasContext *ctx, arg_vfsgnjn_vf *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfsgnjn_vv(DisasContext *ctx, arg_vfsgnjn_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfsgnj_vf(DisasContext *ctx, arg_vfsgnj_vf *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfsgnj_vv(DisasContext *ctx, arg_vfsgnj_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfsgnjx_vf(DisasContext *ctx, arg_vfsgnjx_vf *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfsgnjx_vv(DisasContext *ctx, arg_vfsgnjx_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfslide1down_vf(DisasContext *ctx, arg_vfslide1down_vf *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfslide1up_vf(DisasContext *ctx, arg_vfslide1up_vf *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfsqrt_v(DisasContext *ctx, arg_vfsqrt_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfsub_vf(DisasContext *ctx, arg_vfsub_vf *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfsub_vv(DisasContext *ctx, arg_vfsub_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfwadd_vf(DisasContext *ctx, arg_vfwadd_vf *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfwadd_vv(DisasContext *ctx, arg_vfwadd_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfwadd_wf(DisasContext *ctx, arg_vfwadd_wf *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfwadd_wv(DisasContext *ctx, arg_vfwadd_wv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfwcvtbf16_f_f_v(DisasContext *ctx, arg_vfwcvtbf16_f_f_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfwcvt_f_f_v(DisasContext *ctx, arg_vfwcvt_f_f_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfwcvt_f_xu_v(DisasContext *ctx, arg_vfwcvt_f_xu_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfwcvt_f_x_v(DisasContext *ctx, arg_vfwcvt_f_x_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfwcvt_rtz_x_f_v(DisasContext *ctx, arg_vfwcvt_rtz_x_f_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfwcvt_rtz_xu_f_v(DisasContext *ctx, arg_vfwcvt_rtz_xu_f_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfwcvt_x_f_v(DisasContext *ctx, arg_vfwcvt_x_f_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfwcvt_xu_f_v(DisasContext *ctx, arg_vfwcvt_xu_f_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfwmaccbf16_vf(DisasContext *ctx, arg_vfwmaccbf16_vf *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfwmaccbf16_vv(DisasContext *ctx, arg_vfwmaccbf16_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfwmacc_vf(DisasContext *ctx, arg_vfwmacc_vf *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfwmacc_vv(DisasContext *ctx, arg_vfwmacc_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfwmsac_vf(DisasContext *ctx, arg_vfwmsac_vf *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfwmsac_vv(DisasContext *ctx, arg_vfwmsac_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfwmul_vf(DisasContext *ctx, arg_vfwmul_vf *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfwmul_vv(DisasContext *ctx, arg_vfwmul_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfwnmacc_vf(DisasContext *ctx, arg_vfwnmacc_vf *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfwnmacc_vv(DisasContext *ctx, arg_vfwnmacc_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfwnmsac_vf(DisasContext *ctx, arg_vfwnmsac_vf *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfwnmsac_vv(DisasContext *ctx, arg_vfwnmsac_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfwredosum_vs(DisasContext *ctx, arg_vfwredosum_vs *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfwredusum_vs(DisasContext *ctx, arg_vfwredusum_vs *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfwsub_vf(DisasContext *ctx, arg_vfwsub_vf *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfwsub_vv(DisasContext *ctx, arg_vfwsub_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfwsub_wf(DisasContext *ctx, arg_vfwsub_wf *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vfwsub_wv(DisasContext *ctx, arg_vfwsub_wv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vghsh_vv(DisasContext *ctx, arg_vghsh_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vgmul_vv(DisasContext *ctx, arg_vgmul_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vid_v(DisasContext *ctx, arg_vid_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_viota_m(DisasContext *ctx, arg_viota_m *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vl1re16_v(DisasContext *ctx, arg_vl1re16_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vl1re32_v(DisasContext *ctx, arg_vl1re32_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vl1re64_v(DisasContext *ctx, arg_vl1re64_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vl1re8_v(DisasContext *ctx, arg_vl1re8_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vl2re16_v(DisasContext *ctx, arg_vl2re16_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vl2re32_v(DisasContext *ctx, arg_vl2re32_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vl2re64_v(DisasContext *ctx, arg_vl2re64_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vl2re8_v(DisasContext *ctx, arg_vl2re8_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vl4re16_v(DisasContext *ctx, arg_vl4re16_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vl4re32_v(DisasContext *ctx, arg_vl4re32_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vl4re64_v(DisasContext *ctx, arg_vl4re64_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vl4re8_v(DisasContext *ctx, arg_vl4re8_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vl8re16_v(DisasContext *ctx, arg_vl8re16_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vl8re32_v(DisasContext *ctx, arg_vl8re32_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vl8re64_v(DisasContext *ctx, arg_vl8re64_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vl8re8_v(DisasContext *ctx, arg_vl8re8_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vle16ff_v(DisasContext *ctx, arg_vle16ff_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vle16_v(DisasContext *ctx, arg_vle16_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vle32ff_v(DisasContext *ctx, arg_vle32ff_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vle32_v(DisasContext *ctx, arg_vle32_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vle64ff_v(DisasContext *ctx, arg_vle64ff_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vle64_v(DisasContext *ctx, arg_vle64_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vle8ff_v(DisasContext *ctx, arg_vle8ff_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vle8_v(DisasContext *ctx, arg_vle8_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vlm_v(DisasContext *ctx, arg_vlm_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vlse16_v(DisasContext *ctx, arg_vlse16_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vlse32_v(DisasContext *ctx, arg_vlse32_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vlse64_v(DisasContext *ctx, arg_vlse64_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vlse8_v(DisasContext *ctx, arg_vlse8_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vlxei16_v(DisasContext *ctx, arg_vlxei16_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vlxei32_v(DisasContext *ctx, arg_vlxei32_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vlxei64_v(DisasContext *ctx, arg_vlxei64_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vlxei8_v(DisasContext *ctx, arg_vlxei8_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmacc_vv(DisasContext *ctx, arg_vmacc_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmacc_vx(DisasContext *ctx, arg_vmacc_vx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmadc_vim(DisasContext *ctx, arg_vmadc_vim *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmadc_vvm(DisasContext *ctx, arg_vmadc_vvm *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmadc_vxm(DisasContext *ctx, arg_vmadc_vxm *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmadd_vv(DisasContext *ctx, arg_vmadd_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmadd_vx(DisasContext *ctx, arg_vmadd_vx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmand_mm(DisasContext *ctx, arg_vmand_mm *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmandn_mm(DisasContext *ctx, arg_vmandn_mm *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmaxu_vv(DisasContext *ctx, arg_vmaxu_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmaxu_vx(DisasContext *ctx, arg_vmaxu_vx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmax_vv(DisasContext *ctx, arg_vmax_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmax_vx(DisasContext *ctx, arg_vmax_vx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmerge_vim(DisasContext *ctx, arg_vmerge_vim *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmerge_vvm(DisasContext *ctx, arg_vmerge_vvm *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmerge_vxm(DisasContext *ctx, arg_vmerge_vxm *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmfeq_vf(DisasContext *ctx, arg_vmfeq_vf *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmfeq_vv(DisasContext *ctx, arg_vmfeq_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmfge_vf(DisasContext *ctx, arg_vmfge_vf *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmfgt_vf(DisasContext *ctx, arg_vmfgt_vf *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmfle_vf(DisasContext *ctx, arg_vmfle_vf *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmfle_vv(DisasContext *ctx, arg_vmfle_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmflt_vf(DisasContext *ctx, arg_vmflt_vf *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmflt_vv(DisasContext *ctx, arg_vmflt_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmfne_vf(DisasContext *ctx, arg_vmfne_vf *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmfne_vv(DisasContext *ctx, arg_vmfne_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vminu_vv(DisasContext *ctx, arg_vminu_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vminu_vx(DisasContext *ctx, arg_vminu_vx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmin_vv(DisasContext *ctx, arg_vmin_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmin_vx(DisasContext *ctx, arg_vmin_vx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmnand_mm(DisasContext *ctx, arg_vmnand_mm *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmnor_mm(DisasContext *ctx, arg_vmnor_mm *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmor_mm(DisasContext *ctx, arg_vmor_mm *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmorn_mm(DisasContext *ctx, arg_vmorn_mm *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmsbc_vvm(DisasContext *ctx, arg_vmsbc_vvm *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmsbc_vxm(DisasContext *ctx, arg_vmsbc_vxm *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmsbf_m(DisasContext *ctx, arg_vmsbf_m *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmseq_vi(DisasContext *ctx, arg_vmseq_vi *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmseq_vv(DisasContext *ctx, arg_vmseq_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmseq_vx(DisasContext *ctx, arg_vmseq_vx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmsgtu_vi(DisasContext *ctx, arg_vmsgtu_vi *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmsgtu_vx(DisasContext *ctx, arg_vmsgtu_vx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmsgt_vi(DisasContext *ctx, arg_vmsgt_vi *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmsgt_vx(DisasContext *ctx, arg_vmsgt_vx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmsif_m(DisasContext *ctx, arg_vmsif_m *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmsleu_vi(DisasContext *ctx, arg_vmsleu_vi *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmsleu_vv(DisasContext *ctx, arg_vmsleu_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmsleu_vx(DisasContext *ctx, arg_vmsleu_vx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmsle_vi(DisasContext *ctx, arg_vmsle_vi *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmsle_vv(DisasContext *ctx, arg_vmsle_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmsle_vx(DisasContext *ctx, arg_vmsle_vx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmsltu_vv(DisasContext *ctx, arg_vmsltu_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmsltu_vx(DisasContext *ctx, arg_vmsltu_vx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmslt_vv(DisasContext *ctx, arg_vmslt_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmslt_vx(DisasContext *ctx, arg_vmslt_vx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmsne_vi(DisasContext *ctx, arg_vmsne_vi *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmsne_vv(DisasContext *ctx, arg_vmsne_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmsne_vx(DisasContext *ctx, arg_vmsne_vx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmsof_m(DisasContext *ctx, arg_vmsof_m *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmulhsu_vv(DisasContext *ctx, arg_vmulhsu_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmulhsu_vx(DisasContext *ctx, arg_vmulhsu_vx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmulhu_vv(DisasContext *ctx, arg_vmulhu_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmulhu_vx(DisasContext *ctx, arg_vmulhu_vx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmulh_vv(DisasContext *ctx, arg_vmulh_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmulh_vx(DisasContext *ctx, arg_vmulh_vx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmul_vv(DisasContext *ctx, arg_vmul_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmul_vx(DisasContext *ctx, arg_vmul_vx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmv1r_v(DisasContext *ctx, arg_vmv1r_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmv2r_v(DisasContext *ctx, arg_vmv2r_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmv4r_v(DisasContext *ctx, arg_vmv4r_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmv8r_v(DisasContext *ctx, arg_vmv8r_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmv_s_x(DisasContext *ctx, arg_vmv_s_x *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmv_v_i(DisasContext *ctx, arg_vmv_v_i *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmv_v_v(DisasContext *ctx, arg_vmv_v_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmv_v_x(DisasContext *ctx, arg_vmv_v_x *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmv_x_s(DisasContext *ctx, arg_vmv_x_s *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmxnor_mm(DisasContext *ctx, arg_vmxnor_mm *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vmxor_mm(DisasContext *ctx, arg_vmxor_mm *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vnclipu_wi(DisasContext *ctx, arg_vnclipu_wi *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vnclipu_wv(DisasContext *ctx, arg_vnclipu_wv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vnclipu_wx(DisasContext *ctx, arg_vnclipu_wx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vnclip_wi(DisasContext *ctx, arg_vnclip_wi *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vnclip_wv(DisasContext *ctx, arg_vnclip_wv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vnclip_wx(DisasContext *ctx, arg_vnclip_wx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vnmsac_vv(DisasContext *ctx, arg_vnmsac_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vnmsac_vx(DisasContext *ctx, arg_vnmsac_vx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vnmsub_vv(DisasContext *ctx, arg_vnmsub_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vnmsub_vx(DisasContext *ctx, arg_vnmsub_vx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vnsra_wi(DisasContext *ctx, arg_vnsra_wi *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vnsra_wv(DisasContext *ctx, arg_vnsra_wv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vnsra_wx(DisasContext *ctx, arg_vnsra_wx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vnsrl_wi(DisasContext *ctx, arg_vnsrl_wi *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vnsrl_wv(DisasContext *ctx, arg_vnsrl_wv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vnsrl_wx(DisasContext *ctx, arg_vnsrl_wx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vor_vi(DisasContext *ctx, arg_vor_vi *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vor_vv(DisasContext *ctx, arg_vor_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vor_vx(DisasContext *ctx, arg_vor_vx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vredand_vs(DisasContext *ctx, arg_vredand_vs *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vredmaxu_vs(DisasContext *ctx, arg_vredmaxu_vs *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vredmax_vs(DisasContext *ctx, arg_vredmax_vs *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vredminu_vs(DisasContext *ctx, arg_vredminu_vs *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vredmin_vs(DisasContext *ctx, arg_vredmin_vs *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vredor_vs(DisasContext *ctx, arg_vredor_vs *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vredsum_vs(DisasContext *ctx, arg_vredsum_vs *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vredxor_vs(DisasContext *ctx, arg_vredxor_vs *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vremu_vv(DisasContext *ctx, arg_vremu_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vremu_vx(DisasContext *ctx, arg_vremu_vx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vrem_vv(DisasContext *ctx, arg_vrem_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vrem_vx(DisasContext *ctx, arg_vrem_vx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vrev8_v(DisasContext *ctx, arg_vrev8_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vrgatherei16_vv(DisasContext *ctx, arg_vrgatherei16_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vrgather_vi(DisasContext *ctx, arg_vrgather_vi *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vrgather_vv(DisasContext *ctx, arg_vrgather_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vrgather_vx(DisasContext *ctx, arg_vrgather_vx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vrol_vv(DisasContext *ctx, arg_vrol_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vrol_vx(DisasContext *ctx, arg_vrol_vx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vror_vi(DisasContext *ctx, arg_vror_vi *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vror_vv(DisasContext *ctx, arg_vror_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vror_vx(DisasContext *ctx, arg_vror_vx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vrsub_vi(DisasContext *ctx, arg_vrsub_vi *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vrsub_vx(DisasContext *ctx, arg_vrsub_vx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vs1r_v(DisasContext *ctx, arg_vs1r_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vs2r_v(DisasContext *ctx, arg_vs2r_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vs4r_v(DisasContext *ctx, arg_vs4r_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vs8r_v(DisasContext *ctx, arg_vs8r_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vsaddu_vi(DisasContext *ctx, arg_vsaddu_vi *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vsaddu_vv(DisasContext *ctx, arg_vsaddu_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vsaddu_vx(DisasContext *ctx, arg_vsaddu_vx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vsadd_vi(DisasContext *ctx, arg_vsadd_vi *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vsadd_vv(DisasContext *ctx, arg_vsadd_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vsadd_vx(DisasContext *ctx, arg_vsadd_vx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vsbc_vvm(DisasContext *ctx, arg_vsbc_vvm *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vsbc_vxm(DisasContext *ctx, arg_vsbc_vxm *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vse16_v(DisasContext *ctx, arg_vse16_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vse32_v(DisasContext *ctx, arg_vse32_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vse64_v(DisasContext *ctx, arg_vse64_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vse8_v(DisasContext *ctx, arg_vse8_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vsetivli(DisasContext *ctx, arg_vsetivli *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vsetvl(DisasContext *ctx, arg_vsetvl *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vsetvli(DisasContext *ctx, arg_vsetvli *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vsext_vf2(DisasContext *ctx, arg_vsext_vf2 *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vsext_vf4(DisasContext *ctx, arg_vsext_vf4 *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vsext_vf8(DisasContext *ctx, arg_vsext_vf8 *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vsha2ch_vv(DisasContext *ctx, arg_vsha2ch_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vsha2cl_vv(DisasContext *ctx, arg_vsha2cl_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vsha2ms_vv(DisasContext *ctx, arg_vsha2ms_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vslide1down_vx(DisasContext *ctx, arg_vslide1down_vx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vslide1up_vx(DisasContext *ctx, arg_vslide1up_vx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vslidedown_vi(DisasContext *ctx, arg_vslidedown_vi *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vslidedown_vx(DisasContext *ctx, arg_vslidedown_vx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vslideup_vi(DisasContext *ctx, arg_vslideup_vi *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vslideup_vx(DisasContext *ctx, arg_vslideup_vx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vsll_vi(DisasContext *ctx, arg_vsll_vi *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vsll_vv(DisasContext *ctx, arg_vsll_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vsll_vx(DisasContext *ctx, arg_vsll_vx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vsm3c_vi(DisasContext *ctx, arg_vsm3c_vi *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vsm3me_vv(DisasContext *ctx, arg_vsm3me_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vsm4k_vi(DisasContext *ctx, arg_vsm4k_vi *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vsm4r_vs(DisasContext *ctx, arg_vsm4r_vs *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vsm4r_vv(DisasContext *ctx, arg_vsm4r_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vsmul_vv(DisasContext *ctx, arg_vsmul_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vsmul_vx(DisasContext *ctx, arg_vsmul_vx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vsm_v(DisasContext *ctx, arg_vsm_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vsra_vi(DisasContext *ctx, arg_vsra_vi *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vsra_vv(DisasContext *ctx, arg_vsra_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vsra_vx(DisasContext *ctx, arg_vsra_vx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vsrl_vi(DisasContext *ctx, arg_vsrl_vi *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vsrl_vv(DisasContext *ctx, arg_vsrl_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vsrl_vx(DisasContext *ctx, arg_vsrl_vx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vsse16_v(DisasContext *ctx, arg_vsse16_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vsse32_v(DisasContext *ctx, arg_vsse32_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vsse64_v(DisasContext *ctx, arg_vsse64_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vsse8_v(DisasContext *ctx, arg_vsse8_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vssra_vi(DisasContext *ctx, arg_vssra_vi *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vssra_vv(DisasContext *ctx, arg_vssra_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vssra_vx(DisasContext *ctx, arg_vssra_vx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vssrl_vi(DisasContext *ctx, arg_vssrl_vi *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vssrl_vv(DisasContext *ctx, arg_vssrl_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vssrl_vx(DisasContext *ctx, arg_vssrl_vx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vssubu_vv(DisasContext *ctx, arg_vssubu_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vssubu_vx(DisasContext *ctx, arg_vssubu_vx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vssub_vv(DisasContext *ctx, arg_vssub_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vssub_vx(DisasContext *ctx, arg_vssub_vx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vsub_vv(DisasContext *ctx, arg_vsub_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vsub_vx(DisasContext *ctx, arg_vsub_vx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vsxei16_v(DisasContext *ctx, arg_vsxei16_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vsxei32_v(DisasContext *ctx, arg_vsxei32_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vsxei64_v(DisasContext *ctx, arg_vsxei64_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vsxei8_v(DisasContext *ctx, arg_vsxei8_v *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vwaddu_vv(DisasContext *ctx, arg_vwaddu_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vwaddu_vx(DisasContext *ctx, arg_vwaddu_vx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vwaddu_wv(DisasContext *ctx, arg_vwaddu_wv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vwaddu_wx(DisasContext *ctx, arg_vwaddu_wx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vwadd_vv(DisasContext *ctx, arg_vwadd_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vwadd_vx(DisasContext *ctx, arg_vwadd_vx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vwadd_wv(DisasContext *ctx, arg_vwadd_wv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vwadd_wx(DisasContext *ctx, arg_vwadd_wx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vwmaccsu_vv(DisasContext *ctx, arg_vwmaccsu_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vwmaccsu_vx(DisasContext *ctx, arg_vwmaccsu_vx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vwmaccus_vx(DisasContext *ctx, arg_vwmaccus_vx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vwmaccu_vv(DisasContext *ctx, arg_vwmaccu_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vwmaccu_vx(DisasContext *ctx, arg_vwmaccu_vx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vwmacc_vv(DisasContext *ctx, arg_vwmacc_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vwmacc_vx(DisasContext *ctx, arg_vwmacc_vx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vwmulsu_vv(DisasContext *ctx, arg_vwmulsu_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vwmulsu_vx(DisasContext *ctx, arg_vwmulsu_vx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vwmulu_vv(DisasContext *ctx, arg_vwmulu_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vwmulu_vx(DisasContext *ctx, arg_vwmulu_vx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vwmul_vv(DisasContext *ctx, arg_vwmul_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vwmul_vx(DisasContext *ctx, arg_vwmul_vx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vwredsumu_vs(DisasContext *ctx, arg_vwredsumu_vs *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vwredsum_vs(DisasContext *ctx, arg_vwredsum_vs *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vwsll_vi(DisasContext *ctx, arg_vwsll_vi *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vwsll_vv(DisasContext *ctx, arg_vwsll_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vwsll_vx(DisasContext *ctx, arg_vwsll_vx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vwsubu_vv(DisasContext *ctx, arg_vwsubu_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vwsubu_vx(DisasContext *ctx, arg_vwsubu_vx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vwsubu_wv(DisasContext *ctx, arg_vwsubu_wv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vwsubu_wx(DisasContext *ctx, arg_vwsubu_wx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vwsub_vv(DisasContext *ctx, arg_vwsub_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vwsub_vx(DisasContext *ctx, arg_vwsub_vx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vwsub_wv(DisasContext *ctx, arg_vwsub_wv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vwsub_wx(DisasContext *ctx, arg_vwsub_wx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vxor_vi(DisasContext *ctx, arg_vxor_vi *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vxor_vv(DisasContext *ctx, arg_vxor_vv *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vxor_vx(DisasContext *ctx, arg_vxor_vx *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vzext_vf2(DisasContext *ctx, arg_vzext_vf2 *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vzext_vf4(DisasContext *ctx, arg_vzext_vf4 *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_vzext_vf8(DisasContext *ctx, arg_vzext_vf8 *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_wrs_nto(DisasContext *ctx, arg_wrs_nto *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_wrs_sto(DisasContext *ctx, arg_wrs_sto *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_xperm4(DisasContext *ctx, arg_xperm4 *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_xperm8(DisasContext *ctx, arg_xperm8 *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_zext_h_32(DisasContext *ctx, arg_zext_h_32 *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_zext_h_64(DisasContext *ctx, arg_zext_h_64 *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_zip(DisasContext *ctx, arg_zip *a) {__NOT_IMPLEMENTED_EXIT__}


static bool trans_c64_illegal(DisasContext *ctx, arg_c64_illegal *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_c_fld(DisasContext *ctx, arg_c_fld *a) {
    return trans_fld(ctx, a);
}
static bool trans_c_flw(DisasContext *ctx, arg_c_flw *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_c_fsd(DisasContext *ctx, arg_c_fsd *a) {
    return trans_fsd(ctx, a);
}
static bool trans_c_fsw(DisasContext *ctx, arg_c_fsw *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_c_lbu(DisasContext *ctx, arg_c_lbu *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_c_lh(DisasContext *ctx, arg_c_lh *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_c_lhu(DisasContext *ctx, arg_c_lhu *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_cm_jalt(DisasContext *ctx, arg_cm_jalt *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_cm_mva01s(DisasContext *ctx, arg_cm_mva01s *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_cm_mvsa01(DisasContext *ctx, arg_cm_mvsa01 *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_c_mop_n(DisasContext *ctx, arg_c_mop_n *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_cm_pop(DisasContext *ctx, arg_cm_pop *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_cm_popret(DisasContext *ctx, arg_cm_popret *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_cm_popretz(DisasContext *ctx, arg_cm_popretz *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_cm_push(DisasContext *ctx, arg_cm_push *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_c_mul(DisasContext *ctx, arg_c_mul *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_c_not(DisasContext *ctx, arg_c_not *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_c_sb(DisasContext *ctx, arg_c_sb *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_c_sext_b(DisasContext *ctx, arg_c_sext_b *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_c_sext_h(DisasContext *ctx, arg_c_sext_h *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_c_sh(DisasContext *ctx, arg_c_sh *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_c_zext_b(DisasContext *ctx, arg_c_zext_b *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_c_zext_h(DisasContext *ctx, arg_c_zext_h *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_c_zext_w(DisasContext *ctx, arg_c_zext_w *a) {__NOT_IMPLEMENTED_EXIT__}
static bool trans_illegal(DisasContext *ctx, arg_illegal *a) {__NOT_IMPLEMENTED_EXIT__}


bool interpreter(CPURISCVState *env, uint32_t insn, INSCache* ic) {
    ctx->ol = MXL_RV64;
    if (likely(ic)) {
        ic->trans_func(env, ic->arg);
    } else {
        if (env->cur_insn_len == 2) {
            if (!decode_insn16(env, insn)) {
                return false;
            }
        } else {
            if (!decode_insn32(env, insn)) {
                return false;
            }
        }
    }
    env->gpr[0] = 0;
    env->pc = env->pc + env->cur_insn_len;
    return true;
}
