/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 *
 * Helpers for IOCSR reads/writes
 */
#include "qemu/osdep.h"
#include "cpu.h"
// #include "exec/exec-all.h"
#include "exec/helper-proto.h"

static inline bool X86ConditionPassed(CPULoongArchState *env, int cond)
{
    bool r = false;
    eflags_t *flags = &env->x86_flags;

    switch (cond & 0xf) {
        case  0: r = !flags->cf && !flags->zf; break;
        case  1: r = !flags->cf; break;
        case  2: r = flags->cf; break;
        case  3: r = flags->cf || flags->zf; break;
        case  4: r = flags->zf; break;
        case  5: r = !flags->zf; break;
        case  6: r = !flags->zf && (flags->sf == flags->of); break;
        case  7: r = (flags->sf == flags->of); break;
        case  8: r = (flags->sf != flags->of); break;
        case  9: r = flags->zf || (flags->sf != flags->of); break;
        case 10: r = flags->sf; break;
        case 11: r = !flags->sf; break;
        case 12: r = flags->of; break;
        case 13: r = !flags->of; break;
        case 14: r = flags->pf; break;
        case 15: r = !flags->pf; break;
        default: assert(0);
    }

    return r;
}

/*
static inline void SLL_C(uint32_t x, uint32_t shift, int cin, uint32_t *result, int *cout)
{
    if (shift == 0) {
        *result = x;
        *cout = cin;
    } else {
        uint64_t extended_x = x << shift;
        *result = (uint32_t)extended_x;
        *cout = (extended_x >> 32) & 0x1;
    }
}

static inline void SRL_C(uint32_t x, uint32_t shift, int cin, uint32_t *result, int *cout)
{
    if (shift == 0) {
        *result = x;
        *cout = cin;
    } else {
        *result = x >> shift;
        *cout = (x >> (shift-1)) & 0x1;
    }
}

static inline void SRA_C(uint32_t x, uint32_t shift, int cin, uint32_t *result, int *cout)
{
    if (shift == 0) {
        *result = x;
        *cout = cin;
    } else {
        *result = ((int32_t)x) >> shift;
        *cout = (((int32_t)x) >> (shift-1)) & 0x1;
    }
}

static inline void ROTR_C(uint32_t x, uint32_t shift, int cin, uint32_t *result, int *cout)
{
    if (shift == 0) {
        *result = x;
        *cout = cin;
    } else {
        int m = shift % 32;
        int dummy;
        uint32_t hi, lo;
        SRL_C(x, m, cin, &lo, &dummy);
        SLL_C(x, m, cin, &hi, &dummy);
        *result = hi | lo;
        *cout = (*result >> 31) & 0x1;
    }
}
*/

#define BEXT(val, pos) ((val >> pos) & 0x1)

static inline int parity1_b(int val){
    int c;
    c   = (val>>1) ^ val ^ 1;
    val = (c  >>2) ^ c  ;
    c   = (val>>4) ^ val;
    return c&1;
}

void helper_lbt_x86add_wu(CPULoongArchState *env, target_ulong v0, target_ulong v1)
{
    eflags_t *flags = &env->x86_flags;

    uint32_t a0 = (uint32_t)v0;
    uint32_t a1 = (uint32_t)v1;
    uint64_t temp = (uint64_t)a0 + (uint64_t)a1;

    flags->cf = BEXT(temp, 32);
    flags->of = 0;
    flags->af = BEXT(((a0 & 0xf) + (a1 & 0xf)),4);
    flags->sf = BEXT(temp, 31);
    flags->zf = (temp&0xffffffff) == 0;
    flags->pf = parity1_b(temp);
}
void helper_lbt_x86add_du(CPULoongArchState *env, target_ulong v0, target_ulong v1)
{
    eflags_t *flags = &env->x86_flags;
    flags->of = 0;
    uint64_t a0 = (uint64_t)v0;
    uint64_t a1 = (uint64_t)v1;
    uint32_t a0lo   = (uint32_t)(a0 & 0xf);
    uint32_t a1lo   = (uint32_t)(a1 & 0xf);
    uint32_t templo = a0lo + a1lo;
    flags->af = templo>>4;
    templo&= 0xf;
    uint64_t a0hi = a0>>4;
    uint64_t a1hi = a1>>4;
    uint64_t temphi = a0hi + a1hi + (uint64_t)flags->af;
    flags->cf = BEXT(temphi, 60);
    flags->sf = BEXT(temphi, 59);
    temphi <<= 4;
    temphi += templo;
    flags->zf = temphi == 0;
    flags->pf = parity1_b(temphi);
}
void helper_lbt_x86sub_wu(CPULoongArchState *env, target_ulong v0, target_ulong v1)
{
    eflags_t *flags = &env->x86_flags;

    uint32_t a0 = (uint32_t)v0;
    uint32_t a1 = (uint32_t)v1;
    uint64_t temp = (uint64_t)a0 - (uint64_t)a1;

    flags->cf = BEXT(temp, 32);
    flags->of = 0;
    flags->af = BEXT(((a0 & 0xf) - (a1 & 0xf)),4);
    flags->sf = BEXT(temp, 31);
    flags->zf = (temp&0xffffffff) == 0;
    flags->pf = parity1_b(temp);
}
void helper_lbt_x86sub_du(CPULoongArchState *env, target_ulong v0, target_ulong v1)
{
    eflags_t *flags = &env->x86_flags;
    flags->of = 0;
    uint64_t a0 = (uint64_t)v0;
    uint64_t a1 = (uint64_t)v1;
    uint32_t a0lo   = (uint32_t)(a0 & 0xf);
    uint32_t a1lo   = (uint32_t)(a1 & 0xf);
    uint32_t templo = a0lo - a1lo;
    flags->af = (templo>>4)&1;
    templo&= 0xf;
    uint64_t a0hi = a0>>4;
    uint64_t a1hi = a1>>4;
    uint64_t temphi = a0hi - a1hi - (uint64_t)flags->af;
    flags->cf = BEXT(temphi, 60);
    flags->sf = BEXT(temphi, 59);
    temphi <<= 4;
    temphi += templo;
    flags->zf = temphi == 0;
    flags->pf = parity1_b(temphi);
}
#define HELPER_LBT_X86ADDSUB(name,width,cast,sign,carry) \
void helper_lbt_x86##name(CPULoongArchState *env, target_ulong v0, target_ulong v1) \
{\
    eflags_t *flags = &env->x86_flags;\
    cast a0 = (cast)v0;\
    cast a1 = (cast)v1;\
    cast a0lo   = a0 & 0xf;\
    cast a1lo   = a1 & 0xf;\
    cast templo = a0lo sign a1lo sign carry;\
    flags->af = (templo>>4)&1;\
    templo&= 0xf;\
    cast a0hi = a0>>4;\
    cast a1hi = a1>>4;\
    cast temphi = a0hi sign a1hi sign (cast)flags->af;\
    flags->cf = BEXT(temphi, (width-4));\
    flags->sf = BEXT(temphi, (width-5));\
    temphi <<= 4;\
    temphi += templo;\
    uint8_t a0s = ((     (a0        ))>>(width-3))&0x4;\
    uint8_t a1s = ((sign (a1 + carry))>>(width-2))&0x2;\
    if ((sign 1) == (-1)) { a1s = ((~ (a1 + carry))>>(width-2))&0x2;} \
    flags->of = (0b01000010>>(a0s|a1s|flags->sf))&1;\
    flags->zf = temphi == 0;\
    flags->pf = parity1_b(temphi);\
}
HELPER_LBT_X86ADDSUB(add_b,8,uint8_t,+,0)
HELPER_LBT_X86ADDSUB(add_h,16,uint16_t,+,0)
HELPER_LBT_X86ADDSUB(add_w,32,uint32_t,+,0)
HELPER_LBT_X86ADDSUB(add_d,64,uint64_t,+,0)
HELPER_LBT_X86ADDSUB(adc_b,8,uint8_t,+,flags->cf)
HELPER_LBT_X86ADDSUB(adc_h,16,uint16_t,+,flags->cf)
HELPER_LBT_X86ADDSUB(adc_w,32,uint32_t,+,flags->cf)
HELPER_LBT_X86ADDSUB(adc_d,64,uint64_t,+,flags->cf)
HELPER_LBT_X86ADDSUB(sub_b,8,uint8_t,-,0)
HELPER_LBT_X86ADDSUB(sub_h,16,uint16_t,-,0)
HELPER_LBT_X86ADDSUB(sub_w,32,uint32_t,-,0)
HELPER_LBT_X86ADDSUB(sub_d,64,uint64_t,-,0)
HELPER_LBT_X86ADDSUB(sbc_b,8,uint8_t,-,flags->cf)
HELPER_LBT_X86ADDSUB(sbc_h,16,uint16_t,-,flags->cf)
HELPER_LBT_X86ADDSUB(sbc_w,32,uint32_t,-,flags->cf)
HELPER_LBT_X86ADDSUB(sbc_d,64,uint64_t,-,flags->cf)
#undef HELPER_LBT_X86ADDSUB

#define HELPER_LBT_X86INCDEC(name,width,cast,op,af_value,of_value) \
void helper_lbt_x86##name(CPULoongArchState *env, target_ulong v0) \
{\
    eflags_t *flags = &env->x86_flags;\
    cast a0 = (cast)v0;\
    cast temp = a0 op 1;\
    flags->af = (a0&0xf)==af_value;\
    flags->of = a0==of_value;\
    flags->sf = temp<0;\
    flags->pf = parity1_b(temp);\
    flags->zf = temp==0;\
}
HELPER_LBT_X86INCDEC(inc_b, 8, int8_t,+,0xf,+0x7f                )
HELPER_LBT_X86INCDEC(inc_h,16,int16_t,+,0xf,+0x7fff              )
HELPER_LBT_X86INCDEC(inc_w,32,int32_t,+,0xf,+0x7fffffff          )
HELPER_LBT_X86INCDEC(inc_d,64,int64_t,+,0xf,+0x7fffffffffffffffll)
HELPER_LBT_X86INCDEC(dec_b, 8, int8_t,-,0x0,-0x80                )
HELPER_LBT_X86INCDEC(dec_h,16,int16_t,-,0x0,-0x8000              )
HELPER_LBT_X86INCDEC(dec_w,32,int32_t,-,0x0,-0x80000000          )
HELPER_LBT_X86INCDEC(dec_d,64,int64_t,-,0x0,-0x8000000000000000ll)
#undef HELPER_LBT_X86INCDEC
#define HELPER_LBT_X86SHIFT(name,top,cast,mask,shift,cf_value,of_value) \
void helper_lbt_x86##name(CPULoongArchState *env, target_ulong v0, target_ulong v1) \
{\
    uint8_t s = ((uint8_t)v1 & mask);\
    if(s==0)return;\
    eflags_t *flags = &env->x86_flags;\
    cast a0    = (cast)v0;\
    cast temp  = a0 shift s;\
    flags->cf = cf_value ;\
    if(s==1)flags->of = of_value ;\
    flags->zf = temp == 0;\
    flags->sf = BEXT(temp, top);\
    flags->pf = parity1_b(temp);\
}
#define HELPER_LBT_X86SHIFTI(name,top,cast,shift,cf_value,of_value) \
void helper_lbt_x86##name(CPULoongArchState *env, target_ulong v0, uint32_t s) \
{\
    if(s==0)return;\
    eflags_t *flags = &env->x86_flags;\
    cast a0    = (cast)v0;\
    cast temp  = a0 shift s;\
    flags->cf = cf_value ;\
    if(s==1)flags->of = of_value ;\
    flags->zf = temp == 0;\
    flags->sf = BEXT(temp, top);\
    flags->pf = parity1_b(temp);\
}
HELPER_LBT_X86SHIFT(sll_b, 7, uint8_t,0x1f,<<,(s<= 8)?BEXT(a0,( 8-s)):0          ,flags->cf^BEXT(temp, 7))
HELPER_LBT_X86SHIFT(sll_h,15,uint16_t,0x1f,<<,(s<=16)?BEXT(a0,(16-s)):0          ,flags->cf^BEXT(temp,15))
HELPER_LBT_X86SHIFT(sll_w,31,uint32_t,0x1f,<<,        BEXT(a0,(32-s))            ,flags->cf^BEXT(temp,31))
HELPER_LBT_X86SHIFT(sll_d,63,uint64_t,0x3f,<<,        BEXT(a0,(64-s))            ,flags->cf^BEXT(temp,63))
HELPER_LBT_X86SHIFT(srl_b, 7, uint8_t,0x1f,>>,(s<= 8)?BEXT(a0,( s-1)):0          ,          BEXT(  a0, 7))
HELPER_LBT_X86SHIFT(srl_h,15,uint16_t,0x1f,>>,(s<=16)?BEXT(a0,( s-1)):0          ,          BEXT(  a0,15))
HELPER_LBT_X86SHIFT(srl_w,31,uint32_t,0x1f,>>,        BEXT(a0,( s-1))            ,          BEXT(  a0,31))
HELPER_LBT_X86SHIFT(srl_d,63,uint64_t,0x3f,>>,        BEXT(a0,( s-1))            ,          BEXT(  a0,63))
HELPER_LBT_X86SHIFT(sra_b, 7,  int8_t,0x1f,>>,(s<= 8)?BEXT(a0,( s-1)):BEXT(a0, 7),                     0 )
HELPER_LBT_X86SHIFT(sra_h,15, int16_t,0x1f,>>,(s<=16)?BEXT(a0,( s-1)):BEXT(a0,15),                     0 )
HELPER_LBT_X86SHIFT(sra_w,31, int32_t,0x1f,>>,        BEXT(a0,( s-1))            ,                     0 )
HELPER_LBT_X86SHIFT(sra_d,63, int64_t,0x3f,>>,        BEXT(a0,( s-1))            ,                     0 )

HELPER_LBT_X86SHIFTI(slli_b, 7, uint8_t,<<,(s<= 8)?BEXT(a0,( 8-s)):0          ,flags->cf^BEXT(temp, 7))
HELPER_LBT_X86SHIFTI(slli_h,15,uint16_t,<<,(s<=16)?BEXT(a0,(16-s)):0          ,flags->cf^BEXT(temp,15))
HELPER_LBT_X86SHIFTI(slli_w,31,uint32_t,<<,        BEXT(a0,(32-s))            ,flags->cf^BEXT(temp,31))
HELPER_LBT_X86SHIFTI(slli_d,63,uint64_t,<<,        BEXT(a0,(64-s))            ,flags->cf^BEXT(temp,63))
HELPER_LBT_X86SHIFTI(srli_b, 7, uint8_t,>>,(s<= 8)?BEXT(a0,( s-1)):0          ,          BEXT(  a0, 7))
HELPER_LBT_X86SHIFTI(srli_h,15,uint16_t,>>,(s<=16)?BEXT(a0,( s-1)):0          ,          BEXT(  a0,15))
HELPER_LBT_X86SHIFTI(srli_w,31,uint32_t,>>,        BEXT(a0,( s-1))            ,          BEXT(  a0,31))
HELPER_LBT_X86SHIFTI(srli_d,63,uint64_t,>>,        BEXT(a0,( s-1))            ,          BEXT(  a0,63))
HELPER_LBT_X86SHIFTI(srai_b, 7,  int8_t,>>,(s<= 8)?BEXT(a0,( s-1)):BEXT(a0, 7),                     0 )
HELPER_LBT_X86SHIFTI(srai_h,15, int16_t,>>,(s<=16)?BEXT(a0,( s-1)):BEXT(a0,15),                     0 )
HELPER_LBT_X86SHIFTI(srai_w,31, int32_t,>>,        BEXT(a0,( s-1))            ,                     0 )
HELPER_LBT_X86SHIFTI(srai_d,63, int64_t,>>,        BEXT(a0,( s-1))            ,                     0 )

#undef HELPER_LBT_X86SHIFTI
#undef HELPER_LBT_X86SHIFT
#define HELPER_LBT_X86ROTATE(name,width,cast,mask1,mask2,cf_value,of_value) \
void helper_lbt_x86##name(CPULoongArchState *env, target_ulong v0, target_ulong v1) \
{\
    uint8_t a1 =((uint8_t)v1 & mask2);\
    uint8_t s = (uint8_t)(a1 & mask1);\
    eflags_t *flags = &env->x86_flags;\
    cast a0    = (cast)v0;\
    flags->cf = cf_value ;\
    if(a1==1)flags->of = of_value ;\
}
#define HELPER_LBT_X86ROTATEC(name,width,cast,mask,s_value,cf_value,of_value) \
void helper_lbt_x86##name(CPULoongArchState *env, target_ulong v0, target_ulong v1) \
{\
    uint8_t a1 = ((uint8_t)v1 & mask);\
    uint8_t s = s_value;\
    eflags_t *flags = &env->x86_flags;\
    cast a0    = (cast)v0;\
    if(a1==1)flags->of = of_value;\
    if(s !=0)flags->cf = cf_value;\
}
#define HELPER_LBT_X86ROTATEI(name,width,cast,cf_value,of_value) \
void helper_lbt_x86##name(CPULoongArchState *env, target_ulong v0, uint32_t s) \
{\
    eflags_t *flags = &env->x86_flags;\
    cast a0    = (cast)v0;\
    flags->cf = cf_value ;\
    if(s==1)flags->of = of_value ;\
}
#define HELPER_LBT_X86ROTATECI(name,width,cast,cf_value,of_value) \
void helper_lbt_x86##name(CPULoongArchState *env, target_ulong v0, uint32_t s) \
{\
    eflags_t *flags = &env->x86_flags;\
    cast a0    = (cast)v0;\
    if(s==1)flags->of = of_value;\
    if(s!=0)flags->cf = cf_value;\
}
HELPER_LBT_X86ROTATE(rotr_b, 8, uint8_t,0x07,0x1f,(s==0)?BEXT(a0, 7):BEXT(a0,( s-1)) ,flags->cf^BEXT(a0, 7))
HELPER_LBT_X86ROTATE(rotr_h,16,uint16_t,0x0f,0x1f,(s==0)?BEXT(a0,15):BEXT(a0,( s-1)) ,flags->cf^BEXT(a0, 15))
HELPER_LBT_X86ROTATE(rotr_w,32,uint32_t,0x1f,0x1f,(s==0)?BEXT(a0,31):BEXT(a0,( s-1)) ,flags->cf^BEXT(a0, 31))
HELPER_LBT_X86ROTATE(rotr_d,64,uint64_t,0x3f,0x3f,(s==0)?BEXT(a0,63):BEXT(a0,( s-1)) ,flags->cf^BEXT(a0, 63))
HELPER_LBT_X86ROTATE(rotl_b, 8, uint8_t,0x07,0x1f,(s==0)?BEXT(a0, 0):BEXT(a0,( 8-s)) ,flags->cf^BEXT(a0, 6))
HELPER_LBT_X86ROTATE(rotl_h,16,uint16_t,0x0f,0x1f,(s==0)?BEXT(a0, 0):BEXT(a0,(16-s)) ,flags->cf^BEXT(a0,14))
HELPER_LBT_X86ROTATE(rotl_w,32,uint32_t,0x1f,0x1f,(s==0)?BEXT(a0, 0):BEXT(a0,(32-s)) ,flags->cf^BEXT(a0,30))
HELPER_LBT_X86ROTATE(rotl_d,64,uint64_t,0x3f,0x3f,(s==0)?BEXT(a0, 0):BEXT(a0,(64-s)) ,flags->cf^BEXT(a0,62))
HELPER_LBT_X86ROTATEC(rcr_b, 8, uint8_t,0x1f,a1% 9,BEXT(a0,( s-1)),flags->cf^BEXT(a0, 7))
HELPER_LBT_X86ROTATEC(rcr_h,16,uint16_t,0x1f,a1%17,BEXT(a0,( s-1)),flags->cf^BEXT(a0,15))
HELPER_LBT_X86ROTATEC(rcr_w,32,uint32_t,0x1f,a1   ,BEXT(a0,( s-1)),flags->cf^BEXT(a0,31))
HELPER_LBT_X86ROTATEC(rcr_d,64,uint64_t,0x3f,a1   ,BEXT(a0,( s-1)),flags->cf^BEXT(a0,63))
HELPER_LBT_X86ROTATEC(rcl_b, 8, uint8_t,0x1f,a1% 9,BEXT(a0,( 8-s)),BEXT((a0^(a0>>1)), 6))
HELPER_LBT_X86ROTATEC(rcl_h,16,uint16_t,0x1f,a1%17,BEXT(a0,(16-s)),BEXT((a0^(a0>>1)),14))
HELPER_LBT_X86ROTATEC(rcl_w,32,uint32_t,0x1f,a1   ,BEXT(a0,(32-s)),BEXT((a0^(a0>>1)),30))
HELPER_LBT_X86ROTATEC(rcl_d,64,uint64_t,0x3f,a1   ,BEXT(a0,(64-s)),BEXT((a0^(a0>>1)),62))
HELPER_LBT_X86ROTATEI(rotri_b, 8, uint8_t,(s==0)?BEXT(a0, 7):BEXT(a0,( s-1)) ,flags->cf^BEXT(a0, 7))
HELPER_LBT_X86ROTATEI(rotri_h,16,uint16_t,(s==0)?BEXT(a0,15):BEXT(a0,( s-1)) ,flags->cf^BEXT(a0, 15))
HELPER_LBT_X86ROTATEI(rotri_w,32,uint32_t,(s==0)?BEXT(a0,31):BEXT(a0,( s-1)) ,flags->cf^BEXT(a0, 31))
HELPER_LBT_X86ROTATEI(rotri_d,64,uint64_t,(s==0)?BEXT(a0,63):BEXT(a0,( s-1)) ,flags->cf^BEXT(a0, 63))
HELPER_LBT_X86ROTATEI(rotli_b, 8, uint8_t,(s==0)?BEXT(a0, 0):BEXT(a0,( 8-s)) ,flags->cf^BEXT(a0, 6))
HELPER_LBT_X86ROTATEI(rotli_h,16,uint16_t,(s==0)?BEXT(a0, 0):BEXT(a0,(16-s)) ,flags->cf^BEXT(a0,14))
HELPER_LBT_X86ROTATEI(rotli_w,32,uint32_t,(s==0)?BEXT(a0, 0):BEXT(a0,(32-s)) ,flags->cf^BEXT(a0,30))
HELPER_LBT_X86ROTATEI(rotli_d,64,uint64_t,(s==0)?BEXT(a0, 0):BEXT(a0,(64-s)) ,flags->cf^BEXT(a0,62))
HELPER_LBT_X86ROTATECI(rcri_b, 8, uint8_t,BEXT(a0,( s-1)),flags->cf^BEXT(a0, 7))
HELPER_LBT_X86ROTATECI(rcri_h,16,uint16_t,BEXT(a0,( s-1)),flags->cf^BEXT(a0,15))
HELPER_LBT_X86ROTATECI(rcri_w,32,uint32_t,BEXT(a0,( s-1)),flags->cf^BEXT(a0,31))
HELPER_LBT_X86ROTATECI(rcri_d,64,uint64_t,BEXT(a0,( s-1)),flags->cf^BEXT(a0,63))
HELPER_LBT_X86ROTATECI(rcli_b, 8, uint8_t,BEXT(a0,( 8-s)),BEXT((a0^(a0>>1)), 6))
HELPER_LBT_X86ROTATECI(rcli_h,16,uint16_t,BEXT(a0,(16-s)),BEXT((a0^(a0>>1)),14))
HELPER_LBT_X86ROTATECI(rcli_w,32,uint32_t,BEXT(a0,(32-s)),BEXT((a0^(a0>>1)),30))
HELPER_LBT_X86ROTATECI(rcli_d,64,uint64_t,BEXT(a0,(64-s)),BEXT((a0^(a0>>1)),62))
#undef HELPER_LBT_X86ROTATECI
#undef HELPER_LBT_X86ROTATEI
#undef HELPER_LBT_X86ROTATEC
#undef HELPER_LBT_X86ROTATE

#define HELPER_LBT_X86LOGIC(name,width,cast,op) \
void helper_lbt_x86##name(CPULoongArchState *env, target_ulong v0, target_ulong v1) \
{\
    cast a0 = (cast)v0;\
    cast a1 = (cast)v1;\
    cast temp = a0 op a1;\
    eflags_t *flags = &env->x86_flags;\
    flags->cf = 0;\
    flags->of = 0;\
    flags->af = 0;\
    flags->sf = BEXT(temp, (width-1));\
    flags->zf = temp==0;\
    flags->pf = parity1_b(temp);\
}
HELPER_LBT_X86LOGIC(and_b, 8, uint8_t,&)
HELPER_LBT_X86LOGIC(and_h,16,uint16_t,&)
HELPER_LBT_X86LOGIC(and_w,32,uint32_t,&)
HELPER_LBT_X86LOGIC(and_d,64,uint64_t,&)
HELPER_LBT_X86LOGIC(or_b, 8, uint8_t,|)
HELPER_LBT_X86LOGIC(or_h,16,uint16_t,|)
HELPER_LBT_X86LOGIC(or_w,32,uint32_t,|)
HELPER_LBT_X86LOGIC(or_d,64,uint64_t,|)
HELPER_LBT_X86LOGIC(xor_b, 8, uint8_t,^)
HELPER_LBT_X86LOGIC(xor_h,16,uint16_t,^)
HELPER_LBT_X86LOGIC(xor_w,32,uint32_t,^)
HELPER_LBT_X86LOGIC(xor_d,64,uint64_t,^)
#undef HELPER_LBT_X86LOGIC
#define HELPER_LBT_X86MUL(name,cast_t,wide_t,flag_value) \
void helper_lbt_x86##name(CPULoongArchState *env, target_ulong v0, target_ulong v1) \
{\
    cast_t a0 = (cast_t)v0;\
    cast_t a1 = (cast_t)v1;\
    wide_t temp = (wide_t)a0 * (wide_t)a1;\
    uint32_t flag = flag_value;\
    eflags_t *flags = &env->x86_flags;\
    flags->cf = flag;\
    flags->of = flag;\
}
HELPER_LBT_X86MUL(mul_b ,  int8_t, int16_t,(0!=((-(temp>> 8)) ^ BEXT(temp, 7))))
HELPER_LBT_X86MUL(mul_bu, uint8_t,uint16_t,(0!=   (temp>> 8)                  ))
HELPER_LBT_X86MUL(mul_h , int16_t, int32_t,(0!=((-(temp>>16)) ^ BEXT(temp,15))))
HELPER_LBT_X86MUL(mul_hu,uint16_t,uint32_t,(0!=   (temp>>16)                  ))
HELPER_LBT_X86MUL(mul_w , int32_t, int64_t,(0!=((-(temp>>32)) ^ BEXT(temp,31))))
HELPER_LBT_X86MUL(mul_wu,uint32_t,uint64_t,(0!=   (temp>>32)                  ))
HELPER_LBT_X86MUL(mul_d , int64_t, __int128_t,(0!=((-(temp>>64)) ^ BEXT(temp,63))))
HELPER_LBT_X86MUL(mul_du,uint64_t,__uint128_t,(0!=   (temp>>64)                  ))
#undef HELPER_LBT_X86MUL

target_ulong helper_lbt_adc_b(CPULoongArchState *env, target_ulong a0, target_ulong a1)
{
    int8_t v0 = (int8_t)a0;
    int8_t v1 = (int8_t)a1;
    int8_t v2 = v0 + v1 + env->x86_flags.cf;
    return (target_ulong)(int64_t)v2;
}

target_ulong helper_lbt_adc_h(CPULoongArchState *env, target_ulong a0, target_ulong a1)
{
    int16_t v0 = (int16_t)a0;
    int16_t v1 = (int16_t)a1;
    int16_t v2 = v0 + v1 + env->x86_flags.cf;
    return (target_ulong)(int64_t)v2;
}

target_ulong helper_lbt_adc_w(CPULoongArchState *env, target_ulong a0, target_ulong a1)
{
    int32_t v0 = (int32_t)a0;
    int32_t v1 = (int32_t)a1;
    int32_t v2 = v0 + v1 + env->x86_flags.cf;
    return (target_ulong)(int64_t)v2;
}

target_ulong helper_lbt_adc_d(CPULoongArchState *env, target_ulong a0, target_ulong a1)
{
    int64_t v0 = (int64_t)a0;
    int64_t v1 = (int64_t)a1;
    int64_t v2 = v0 + v1 + env->x86_flags.cf;
    return (target_ulong)(int64_t)v2;
}

target_ulong helper_lbt_sbc_b(CPULoongArchState *env, target_ulong a0, target_ulong a1)
{
    int8_t v0 = (int8_t)a0;
    int8_t v1 = (int8_t)a1;
    int8_t v2 = v0 - v1 - env->x86_flags.cf;
    return (target_ulong)(int64_t)v2;
}

target_ulong helper_lbt_sbc_h(CPULoongArchState *env, target_ulong a0, target_ulong a1)
{
    int16_t v0 = (int16_t)a0;
    int16_t v1 = (int16_t)a1;
    int16_t v2 = v0 - v1 - env->x86_flags.cf;
    return (target_ulong)(int64_t)v2;
}

target_ulong helper_lbt_sbc_w(CPULoongArchState *env, target_ulong a0, target_ulong a1)
{
    int32_t v0 = (int32_t)a0;
    int32_t v1 = (int32_t)a1;
    int32_t v2 = v0 - v1 - env->x86_flags.cf;
    return (target_ulong)(int64_t)v2;
}

target_ulong helper_lbt_sbc_d(CPULoongArchState *env, target_ulong a0, target_ulong a1)
{
    int64_t v0 = (int64_t)a0;
    int64_t v1 = (int64_t)a1;
    int64_t v2 = v0 - v1 - env->x86_flags.cf;
    return (target_ulong)(int64_t)v2;
}

target_ulong helper_lbt_rcr_b(CPULoongArchState *env, target_ulong a0, target_ulong a1)
{
    int8_t r;
    int s;
    const int L = 8;

    s = a1 % (L+1);
    if (s == 0) {
        r = a0 & 0xff;
    } else {
        uint64_t hi = a0 & ((1 << (s-1)) - 1);
        uint64_t lo = (a0 >> s) & ((1 << (L-s)) -1);
        r = (hi << (L-s+1)) | (env->x86_flags.cf << (L-s)) | lo;
    }

    return (target_ulong)(int64_t)r;
}

target_ulong helper_lbt_rcr_h(CPULoongArchState *env, target_ulong a0, target_ulong a1)
{
    int16_t r;
    int s;
    const int L = 16;

    s = a1 % (L+1);
    if (s == 0) {
        r = a0 & 0xffff;
    } else {
        uint64_t hi = a0 & ((1 << (s-1)) - 1);
        uint64_t lo = (a0 >> s) & ((1 << (L-s)) -1);
        r = (hi << (L-s+1)) | (env->x86_flags.cf << (L-s)) | lo;
    }

    return (target_ulong)(int64_t)r;
}

target_ulong helper_lbt_rcr_w(CPULoongArchState *env, target_ulong a0, target_ulong a1)
{
    int32_t r;
    int s;
    const int L = 32;

    s = a1 % (L+1);
    if (s == 0) {
        r = a0 & 0xffffffff;
    } else {
        uint64_t hi = a0 & ((1 << (s-1)) - 1);
        uint64_t lo = (a0 >> s) & ((1 << (L-s)) -1);
        r = (hi << (L-s+1)) | (env->x86_flags.cf << (L-s)) | lo;
    }

    return (target_ulong)(int64_t)r;
}

target_ulong helper_lbt_rcr_d(CPULoongArchState *env, target_ulong a0, target_ulong a1)
{
    int64_t r;
    int s;
    const int L = 32;

    s = a1 & 0x3f;
    if (s == 0) {
        r = a0;
    } else {
        uint64_t hi = a0 & ((1 << (s-1)) - 1);
        uint64_t lo = (a0 >> s) & ((1 << (L-s)) -1);
        r = (hi << (L-s+1)) | (env->x86_flags.cf << (L-s)) | lo;
    }

    return (target_ulong)r;
}

target_ulong helper_lbt_setx86j(CPULoongArchState *env, uint32_t cond)
{
    return X86ConditionPassed(env, cond);
}

target_ulong helper_lbt_x86mfflag(CPULoongArchState *env, uint32_t mask)
{
    uint64_t r = 0;
    eflags_t *flags = &env->x86_flags;

    if (mask & 0x1) {
        r = r | flags->cf;
    }
    if (mask & 0x2) {
        r = r | (flags->pf << 2);
    }
    if (mask & 0x4) {
        r = r | (flags->af << 4);
    }
    if (mask & 0x8) {
        r = r | (flags->zf << 6);
    }
    if (mask & 0x10) {
        r = r | (flags->sf << 7);
    }
    if (mask & 0x20) {
        r = r | (flags->of << 11);
    }

    return r;
}

void helper_lbt_x86mtflag(CPULoongArchState *env, target_ulong a0, uint32_t mask)
{
    eflags_t *flags = &env->x86_flags;

    if (mask & 0x1) {
        flags->cf = a0 & 0x1;
    }
    if (mask & 0x2) {
        flags->pf = a0 & (1 << 2);
    }
    if (mask & 0x4) {
        flags->af = a0 & (1 << 4);
    }
    if (mask & 0x8) {
        flags->zf = a0 & (1 << 6);
    }
    if (mask & 0x10) {
        flags->sf = a0 & (1 << 7);
    }
    if (mask & 0x20) {
        flags->of = a0 & (1 << 11);
    }
}

target_ulong helper_lbt_x86loop(CPULoongArchState *env, target_ulong a0, uint32_t fmt)
{
    bool eflag_cond = fmt ? !env->x86_flags.zf : env->x86_flags.zf;
    if ((a0 - 1) != 0 && eflag_cond) {
        return 1;
    } else {
        return 0;
    }
}

target_ulong helper_lbt_rcri_b(CPULoongArchState *env, target_ulong a0, uint32_t sa)
{
    int8_t r;
    int s;
    const int L = 8;

    s = sa % (L+1);
    if (s == 0) {
        r = a0 & 0xff;
    } else {
        uint64_t hi = a0 & ((1 << (s-1)) - 1);
        uint64_t lo = (a0 >> s) & ((1 << (L-s)) -1);
        r = (hi << (L-s+1)) | (env->x86_flags.cf << (L-s)) | lo;
    }

    return (target_ulong)(int64_t)r;
}

target_ulong helper_lbt_rcri_h(CPULoongArchState *env, target_ulong a0, uint32_t sa)
{
    int16_t r;
    int s;
    const int L = 16;

    s = sa % (L+1);
    if (s == 0) {
        r = a0 & 0xffff;
    } else {
        uint64_t hi = a0 & ((1 << (s-1)) - 1);
        uint64_t lo = (a0 >> s) & ((1 << (L-s)) -1);
        r = (hi << (L-s+1)) | (env->x86_flags.cf << (L-s)) | lo;
    }

    return (target_ulong)(int64_t)r;
}

target_ulong helper_lbt_rcri_w(CPULoongArchState *env, target_ulong a0, uint32_t sa)
{
    int32_t r;
    int s;
    const int L = 32;

    s = sa % (L+1);
    if (s == 0) {
        r = a0 & 0xffffffff;
    } else {
        uint64_t hi = a0 & ((1 << (s-1)) - 1);
        uint64_t lo = (a0 >> s) & ((1 << (L-s)) -1);
        r = (hi << (L-s+1)) | (env->x86_flags.cf << (L-s)) | lo;
    }

    return (target_ulong)(int64_t)r;
}

target_ulong helper_lbt_rcri_d(CPULoongArchState *env, target_ulong a0, uint32_t sa)
{
    int64_t r;
    int s;
    const int L = 32;

    s = sa & 0x3f;
    if (s == 0) {
        r = a0;
    } else {
        uint64_t hi = a0 & ((1 << (s-1)) - 1);
        uint64_t lo = (a0 >> s) & ((1 << (L-s)) -1);
        r = (hi << (L-s+1)) | (env->x86_flags.cf << (L-s)) | lo;
    }

    return (target_ulong)r;
}

void helper_lbt_gr2scr(CPULoongArchState *env, uint32_t a0, uint32_t a1){
    env->scr[a0] = env->gpr[a1];
}

void helper_lbt_scr2gr(CPULoongArchState *env, uint32_t a0, uint32_t a1){
    env->gpr[a0] = env->scr[a1];
}

void helper_lbt_grsel(CPULoongArchState *env, uint32_t a0, uint32_t a1, uint32_t cond){
    if(X86ConditionPassed(env, cond))
        env->gpr[a0] = env->gpr[a1];
}
