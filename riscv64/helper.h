void helper_set_rounding_mode(CPURISCVState *env, uint32_t);
void helper_set_rounding_mode_chkfrm(CPURISCVState *env, uint32_t);

/* Floating Point - fused */
uint64_t helper_fmadd_s(CPURISCVState*, uint64_t, uint64_t, uint64_t);
uint64_t helper_fmadd_d(CPURISCVState*, uint64_t, uint64_t, uint64_t);
uint64_t helper_fmadd_h(CPURISCVState*, uint64_t, uint64_t, uint64_t);
uint64_t helper_fmsub_s(CPURISCVState*, uint64_t, uint64_t, uint64_t);
uint64_t helper_fmsub_d(CPURISCVState*, uint64_t, uint64_t, uint64_t);
uint64_t helper_fmsub_h(CPURISCVState*, uint64_t, uint64_t, uint64_t);
uint64_t helper_fnmsub_s(CPURISCVState*, uint64_t, uint64_t, uint64_t);
uint64_t helper_fnmsub_d(CPURISCVState*, uint64_t, uint64_t, uint64_t);
uint64_t helper_fnmsub_h(CPURISCVState*, uint64_t, uint64_t, uint64_t);
uint64_t helper_fnmadd_s(CPURISCVState*, uint64_t, uint64_t, uint64_t);
uint64_t helper_fnmadd_d(CPURISCVState*, uint64_t, uint64_t, uint64_t);
uint64_t helper_fnmadd_h(CPURISCVState*, uint64_t, uint64_t, uint64_t);

// /* Floating Point - Single Precision */
uint64_t helper_fadd_s(CPURISCVState*, uint64_t, uint64_t);
uint64_t helper_fsub_s(CPURISCVState*, uint64_t, uint64_t);
uint64_t helper_fmul_s(CPURISCVState*, uint64_t, uint64_t);
uint64_t helper_fdiv_s(CPURISCVState*, uint64_t, uint64_t);
uint64_t helper_fmin_s(CPURISCVState*, uint64_t, uint64_t);
uint64_t helper_fminm_s(CPURISCVState*, uint64_t, uint64_t);
uint64_t helper_fmax_s(CPURISCVState*, uint64_t, uint64_t);
uint64_t helper_fmaxm_s(CPURISCVState*, uint64_t, uint64_t);
uint64_t helper_fsqrt_s(CPURISCVState*, uint64_t);
target_ulong helper_fle_s(CPURISCVState*, uint64_t, uint64_t);
target_ulong helper_fleq_s(CPURISCVState*, uint64_t, uint64_t);
target_ulong helper_flt_s(CPURISCVState*, uint64_t, uint64_t);
target_ulong helper_fltq_s(CPURISCVState*, uint64_t, uint64_t);
target_ulong helper_feq_s(CPURISCVState*, uint64_t, uint64_t);
target_ulong helper_fcvt_w_s(CPURISCVState*, uint64_t);
target_ulong helper_fcvt_wu_s(CPURISCVState*, uint64_t);
target_ulong helper_fcvt_l_s(CPURISCVState*, uint64_t);
target_ulong helper_fcvt_lu_s(CPURISCVState*, uint64_t);
uint64_t helper_fcvt_s_w(CPURISCVState*, target_ulong);
uint64_t helper_fcvt_s_wu(CPURISCVState*, target_ulong);
uint64_t helper_fcvt_s_l(CPURISCVState*, target_ulong);
uint64_t helper_fcvt_s_lu(CPURISCVState*, target_ulong);
target_ulong helper_fclass_s(CPURISCVState*, uint64_t);
uint64_t helper_fround_s(CPURISCVState*, uint64_t);
uint64_t helper_froundnx_s(CPURISCVState*, uint64_t);

// /* Floating Point - Double Precision */
uint64_t helper_fadd_d(CPURISCVState *env, uint64_t, uint64_t);
uint64_t helper_fsub_d(CPURISCVState *env, uint64_t, uint64_t);
uint64_t helper_fmul_d(CPURISCVState *env, uint64_t, uint64_t);
uint64_t helper_fdiv_d(CPURISCVState *env, uint64_t, uint64_t);
uint64_t helper_fmin_d(CPURISCVState *env, uint64_t, uint64_t);
uint64_t helper_fminm_d(CPURISCVState *env, uint64_t, uint64_t);
uint64_t helper_fmax_d(CPURISCVState *env, uint64_t, uint64_t);
uint64_t helper_fmaxm_d(CPURISCVState *env, uint64_t, uint64_t);
uint64_t helper_fcvt_s_d(CPURISCVState *env, uint64_t);
uint64_t helper_fcvt_d_s(CPURISCVState *env, uint64_t);
uint64_t helper_fsqrt_d(CPURISCVState *env, uint64_t);
target_ulong helper_fle_d(CPURISCVState *env, uint64_t, uint64_t);
target_ulong helper_fleq_d(CPURISCVState *env, uint64_t, uint64_t);
target_ulong helper_flt_d(CPURISCVState *env, uint64_t, uint64_t);
target_ulong helper_fltq_d(CPURISCVState *env, uint64_t, uint64_t);
target_ulong helper_feq_d(CPURISCVState *env, uint64_t, uint64_t);
target_ulong helper_fcvt_w_d(CPURISCVState *env, uint64_t);
uint64_t helper_fcvtmod_w_d(CPURISCVState *env, uint64_t);
target_ulong helper_fcvt_wu_d(CPURISCVState *env, uint64_t);
target_ulong helper_fcvt_l_d(CPURISCVState *env, uint64_t);
target_ulong helper_fcvt_lu_d(CPURISCVState *env, uint64_t);
uint64_t helper_fcvt_d_w(CPURISCVState *env, target_ulong);
uint64_t helper_fcvt_d_wu(CPURISCVState *env, target_ulong);
uint64_t helper_fcvt_d_l(CPURISCVState *env, target_ulong);
uint64_t helper_fcvt_d_lu(CPURISCVState *env, target_ulong);
target_ulong helper_fclass_d(uint64_t);
uint64_t helper_fround_d(CPURISCVState *env, uint64_t);
uint64_t helper_froundnx_d(CPURISCVState *env, uint64_t);


#define tl target_ulong
#define env struct CPUArchState*

#define DEF_HELPER_1(name, ret, t1) ret glue(helper_,name)(t1);
#define DEF_HELPER_2(name, ret, t1, t2) ret glue(helper_,name)(t1, t2);
#define DEF_HELPER_3(name, ret, t1, t2, t3) ret glue(helper_,name)(t1, t2, t3);
#define DEF_HELPER_4(name, ret, t1, t2, t3, t4) ret glue(helper_,name)(t1, t2, t3, t4);
#define DEF_HELPER_5(name, ret, t1, t2, t3, t4, t5) ret glue(helper_,name)(t1, t2, t3, t4, t5);
#define DEF_HELPER_6(name, ret, t1, t2, t3, t4, t5, t6) ret glue(helper_,name)(t1, t2, t3, t4, t5, t6);
#define DEF_HELPER_7(name, ret, t1, t2, t3, t4, t5, t6, t7) ret glue(helper_,name)(t1, t2, t3, t4, t5, t6, t7);
#define DEF_HELPER_8(name, ret, t1, t2, t3, t4, t5, t6, t7, t8) ret glue(helper_,name)(t1, t2, t3, t4, t5, t6, t7, t8);

/* Special functions */
DEF_HELPER_2(csrr, tl, env, int)
DEF_HELPER_3(csrw, void, env, int, tl)
DEF_HELPER_4(csrrw, tl, env, int, tl, tl)
DEF_HELPER_2(csrr_i128, tl, env, int)
DEF_HELPER_4(csrw_i128, void, env, int, tl, tl)
DEF_HELPER_6(csrrw_i128, tl, env, int, tl, tl, tl, tl)
#ifndef CONFIG_USER_ONLY
DEF_HELPER_1(sret, tl, env)
DEF_HELPER_1(mret, tl, env)
DEF_HELPER_1(wfi, void, env)
DEF_HELPER_1(wrs_nto, void, env)
DEF_HELPER_1(tlb_flush, void, env)
DEF_HELPER_1(tlb_flush_all, void, env)
/* Native Debug */
DEF_HELPER_1(itrigger_match, void, env)
#endif

#undef tl
#undef env

