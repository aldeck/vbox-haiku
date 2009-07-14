#ifndef DEF_HELPER
#define DEF_HELPER(ret, name, params) ret name params;
#endif

DEF_HELPER(void, helper_lock, (void))
DEF_HELPER(void, helper_unlock, (void))
DEF_HELPER(void, helper_write_eflags, (target_ulong t0, uint32_t update_mask))
DEF_HELPER(target_ulong, helper_read_eflags, (void))
#ifdef VBOX
DEF_HELPER(void, helper_write_eflags_vme, (target_ulong t0))
DEF_HELPER(target_ulong, helper_read_eflags_vme, (void))
#endif
DEF_HELPER(void, helper_divb_AL, (target_ulong t0))
DEF_HELPER(void, helper_idivb_AL, (target_ulong t0))
DEF_HELPER(void, helper_divw_AX, (target_ulong t0))
DEF_HELPER(void, helper_idivw_AX, (target_ulong t0))
DEF_HELPER(void, helper_divl_EAX, (target_ulong t0))
DEF_HELPER(void, helper_idivl_EAX, (target_ulong t0))
#ifdef TARGET_X86_64
DEF_HELPER(void, helper_mulq_EAX_T0, (target_ulong t0))
DEF_HELPER(void, helper_imulq_EAX_T0, (target_ulong t0))
DEF_HELPER(target_ulong, helper_imulq_T0_T1, (target_ulong t0, target_ulong t1))
DEF_HELPER(void, helper_divq_EAX, (target_ulong t0))
DEF_HELPER(void, helper_idivq_EAX, (target_ulong t0))
#endif

DEF_HELPER(void, helper_aam, (int base))
DEF_HELPER(void, helper_aad, (int base))
DEF_HELPER(void, helper_aaa, (void))
DEF_HELPER(void, helper_aas, (void))
DEF_HELPER(void, helper_daa, (void))
DEF_HELPER(void, helper_das, (void))

DEF_HELPER(target_ulong, helper_lsl, (target_ulong selector1))
DEF_HELPER(target_ulong, helper_lar, (target_ulong selector1))
DEF_HELPER(void, helper_verr, (target_ulong selector1))
DEF_HELPER(void, helper_verw, (target_ulong selector1))
DEF_HELPER(void, helper_lldt, (int selector))
DEF_HELPER(void, helper_ltr, (int selector))
DEF_HELPER(void, helper_load_seg, (int seg_reg, int selector))
DEF_HELPER(void, helper_ljmp_protected, (int new_cs, target_ulong new_eip,
                           int next_eip_addend))
DEF_HELPER(void, helper_lcall_real, (int new_cs, target_ulong new_eip1,
                       int shift, int next_eip))
DEF_HELPER(void, helper_lcall_protected, (int new_cs, target_ulong new_eip,
                            int shift, int next_eip_addend))
DEF_HELPER(void, helper_iret_real, (int shift))
DEF_HELPER(void, helper_iret_protected, (int shift, int next_eip))
DEF_HELPER(void, helper_lret_protected, (int shift, int addend))
DEF_HELPER(target_ulong, helper_read_crN, (int reg))
DEF_HELPER(void, helper_write_crN, (int reg, target_ulong t0))
DEF_HELPER(void, helper_lmsw, (target_ulong t0))
DEF_HELPER(void, helper_clts, (void))
DEF_HELPER(void, helper_movl_drN_T0, (int reg, target_ulong t0))
DEF_HELPER(void, helper_invlpg, (target_ulong addr))

DEF_HELPER(void, helper_enter_level, (int level, int data32, target_ulong t1))
#ifdef TARGET_X86_64
DEF_HELPER(void, helper_enter64_level, (int level, int data64, target_ulong t1))
#endif
DEF_HELPER(void, helper_sysenter, (void))
DEF_HELPER(void, helper_sysexit, (int dflag))
#ifdef TARGET_X86_64
DEF_HELPER(void, helper_syscall, (int next_eip_addend))
DEF_HELPER(void, helper_sysret, (int dflag))
#endif
DEF_HELPER(void, helper_hlt, (int next_eip_addend))
DEF_HELPER(void, helper_monitor, (target_ulong ptr))
DEF_HELPER(void, helper_mwait, (int next_eip_addend))
DEF_HELPER(void, helper_debug, (void))
DEF_HELPER(void, helper_raise_interrupt, (int intno, int next_eip_addend))
DEF_HELPER(void, helper_raise_exception, (int exception_index))
DEF_HELPER(void, helper_cli, (void))
DEF_HELPER(void, helper_sti, (void))
#ifdef VBOX
DEF_HELPER(void, helper_cli_vme, (void))
DEF_HELPER(void, helper_sti_vme, (void))
#endif
DEF_HELPER(void, helper_set_inhibit_irq, (void))
DEF_HELPER(void, helper_reset_inhibit_irq, (void))
DEF_HELPER(void, helper_boundw, (target_ulong a0, int v))
DEF_HELPER(void, helper_boundl, (target_ulong a0, int v))
DEF_HELPER(void, helper_rsm, (void))
DEF_HELPER(void, helper_into, (int next_eip_addend))
DEF_HELPER(void, helper_cmpxchg8b, (target_ulong a0))
#ifdef TARGET_X86_64
DEF_HELPER(void, helper_cmpxchg16b, (target_ulong a0))
#endif
DEF_HELPER(void, helper_single_step, (void))
DEF_HELPER(void, helper_cpuid, (void))
DEF_HELPER(void, helper_rdtsc, (void))
DEF_HELPER(void, helper_rdpmc, (void))
DEF_HELPER(void, helper_rdmsr, (void))
DEF_HELPER(void, helper_wrmsr, (void))
#ifdef VBOX
DEF_HELPER(void, helper_rdtscp, (void))
#endif

DEF_HELPER(void, helper_check_iob, (uint32_t t0))
DEF_HELPER(void, helper_check_iow, (uint32_t t0))
DEF_HELPER(void, helper_check_iol, (uint32_t t0))
#ifdef VBOX
DEF_HELPER(void, helper_check_external_event, (void))
DEF_HELPER(void, helper_dump_state, (void))
DEF_HELPER(void, helper_sync_seg, (uint32_t t0))
#endif
DEF_HELPER(void, helper_outb, (uint32_t port, uint32_t data))
DEF_HELPER(target_ulong, helper_inb, (uint32_t port))
DEF_HELPER(void, helper_outw, (uint32_t port, uint32_t data))
DEF_HELPER(target_ulong, helper_inw, (uint32_t port))
DEF_HELPER(void, helper_outl, (uint32_t port, uint32_t data))
DEF_HELPER(target_ulong, helper_inl, (uint32_t port))

DEF_HELPER(void, helper_svm_check_intercept_param, (uint32_t type, uint64_t param))
DEF_HELPER(void, helper_vmexit, (uint32_t exit_code, uint64_t exit_info_1))
DEF_HELPER(void, helper_svm_check_io, (uint32_t port, uint32_t param,
                         uint32_t next_eip_addend))
DEF_HELPER(void, helper_vmrun, (int aflag, int next_eip_addend))
DEF_HELPER(void, helper_vmmcall, (void))
DEF_HELPER(void, helper_vmload, (int aflag))
DEF_HELPER(void, helper_vmsave, (int aflag))
DEF_HELPER(void, helper_stgi, (void))
DEF_HELPER(void, helper_clgi, (void))
DEF_HELPER(void, helper_skinit, (void))
DEF_HELPER(void, helper_invlpga, (int aflag))

/* x86 FPU */

DEF_HELPER(void, helper_flds_FT0, (uint32_t val))
DEF_HELPER(void, helper_fldl_FT0, (uint64_t val))
DEF_HELPER(void, helper_fildl_FT0, (int32_t val))
DEF_HELPER(void, helper_flds_ST0, (uint32_t val))
DEF_HELPER(void, helper_fldl_ST0, (uint64_t val))
DEF_HELPER(void, helper_fildl_ST0, (int32_t val))
DEF_HELPER(void, helper_fildll_ST0, (int64_t val))
#ifndef VBOX
DEF_HELPER(uint32_t, helper_fsts_ST0, (void))
DEF_HELPER(uint64_t, helper_fstl_ST0, (void))
DEF_HELPER(int32_t, helper_fist_ST0, (void))
DEF_HELPER(int32_t, helper_fistl_ST0, (void))
DEF_HELPER(int64_t, helper_fistll_ST0, (void))
DEF_HELPER(int32_t, helper_fistt_ST0, (void))
DEF_HELPER(int32_t, helper_fisttl_ST0, (void))
DEF_HELPER(int64_t, helper_fisttll_ST0, (void))
#else
DEF_HELPER(RTCCUINTREG, helper_fsts_ST0, (void))
DEF_HELPER(uint64_t, helper_fstl_ST0, (void))
DEF_HELPER(RTCCINTREG, helper_fist_ST0, (void))
DEF_HELPER(RTCCINTREG, helper_fistl_ST0, (void))
DEF_HELPER(int64_t, helper_fistll_ST0, (void))
DEF_HELPER(RTCCINTREG, helper_fistt_ST0, (void))
DEF_HELPER(RTCCINTREG, helper_fisttl_ST0, (void))
DEF_HELPER(int64_t, helper_fisttll_ST0, (void))
#endif
DEF_HELPER(void, helper_fldt_ST0, (target_ulong ptr))
DEF_HELPER(void, helper_fstt_ST0, (target_ulong ptr))
DEF_HELPER(void, helper_fpush, (void))
DEF_HELPER(void, helper_fpop, (void))
DEF_HELPER(void, helper_fdecstp, (void))
DEF_HELPER(void, helper_fincstp, (void))
DEF_HELPER(void, helper_ffree_STN, (int st_index))
DEF_HELPER(void, helper_fmov_ST0_FT0, (void))
DEF_HELPER(void, helper_fmov_FT0_STN, (int st_index))
DEF_HELPER(void, helper_fmov_ST0_STN, (int st_index))
DEF_HELPER(void, helper_fmov_STN_ST0, (int st_index))
DEF_HELPER(void, helper_fxchg_ST0_STN, (int st_index))
DEF_HELPER(void, helper_fcom_ST0_FT0, (void))
DEF_HELPER(void, helper_fucom_ST0_FT0, (void))
DEF_HELPER(void, helper_fcomi_ST0_FT0, (void))
DEF_HELPER(void, helper_fucomi_ST0_FT0, (void))
DEF_HELPER(void, helper_fadd_ST0_FT0, (void))
DEF_HELPER(void, helper_fmul_ST0_FT0, (void))
DEF_HELPER(void, helper_fsub_ST0_FT0, (void))
DEF_HELPER(void, helper_fsubr_ST0_FT0, (void))
DEF_HELPER(void, helper_fdiv_ST0_FT0, (void))
DEF_HELPER(void, helper_fdivr_ST0_FT0, (void))
DEF_HELPER(void, helper_fadd_STN_ST0, (int st_index))
DEF_HELPER(void, helper_fmul_STN_ST0, (int st_index))
DEF_HELPER(void, helper_fsub_STN_ST0, (int st_index))
DEF_HELPER(void, helper_fsubr_STN_ST0, (int st_index))
DEF_HELPER(void, helper_fdiv_STN_ST0, (int st_index))
DEF_HELPER(void, helper_fdivr_STN_ST0, (int st_index))
DEF_HELPER(void, helper_fchs_ST0, (void))
DEF_HELPER(void, helper_fabs_ST0, (void))
DEF_HELPER(void, helper_fxam_ST0, (void))
DEF_HELPER(void, helper_fld1_ST0, (void))
DEF_HELPER(void, helper_fldl2t_ST0, (void))
DEF_HELPER(void, helper_fldl2e_ST0, (void))
DEF_HELPER(void, helper_fldpi_ST0, (void))
DEF_HELPER(void, helper_fldlg2_ST0, (void))
DEF_HELPER(void, helper_fldln2_ST0, (void))
DEF_HELPER(void, helper_fldz_ST0, (void))
DEF_HELPER(void, helper_fldz_FT0, (void))
#ifndef VBOX
DEF_HELPER(uint32_t, helper_fnstsw, (void))
DEF_HELPER(uint32_t, helper_fnstcw, (void))
#else
DEF_HELPER(RTCCUINTREG, helper_fnstsw, (void))
DEF_HELPER(RTCCUINTREG, helper_fnstcw, (void))
#endif
DEF_HELPER(void, helper_fldcw, (uint32_t val))
DEF_HELPER(void, helper_fclex, (void))
DEF_HELPER(void, helper_fwait, (void))
DEF_HELPER(void, helper_fninit, (void))
DEF_HELPER(void, helper_fbld_ST0, (target_ulong ptr))
DEF_HELPER(void, helper_fbst_ST0, (target_ulong ptr))
DEF_HELPER(void, helper_f2xm1, (void))
DEF_HELPER(void, helper_fyl2x, (void))
DEF_HELPER(void, helper_fptan, (void))
DEF_HELPER(void, helper_fpatan, (void))
DEF_HELPER(void, helper_fxtract, (void))
DEF_HELPER(void, helper_fprem1, (void))
DEF_HELPER(void, helper_fprem, (void))
DEF_HELPER(void, helper_fyl2xp1, (void))
DEF_HELPER(void, helper_fsqrt, (void))
DEF_HELPER(void, helper_fsincos, (void))
DEF_HELPER(void, helper_frndint, (void))
DEF_HELPER(void, helper_fscale, (void))
DEF_HELPER(void, helper_fsin, (void))
DEF_HELPER(void, helper_fcos, (void))
DEF_HELPER(void, helper_fstenv, (target_ulong ptr, int data32))
DEF_HELPER(void, helper_fldenv, (target_ulong ptr, int data32))
DEF_HELPER(void, helper_fsave, (target_ulong ptr, int data32))
DEF_HELPER(void, helper_frstor, (target_ulong ptr, int data32))
DEF_HELPER(void, helper_fxsave, (target_ulong ptr, int data64))
DEF_HELPER(void, helper_fxrstor, (target_ulong ptr, int data64))
DEF_HELPER(target_ulong, helper_bsf, (target_ulong t0))
DEF_HELPER(target_ulong, helper_bsr, (target_ulong t0))

/* MMX/SSE */

DEF_HELPER(void, helper_enter_mmx, (void))
DEF_HELPER(void, helper_emms, (void))
DEF_HELPER(void, helper_movq, (uint64_t *d, uint64_t *s))

#define SHIFT 0
#include "ops_sse_header.h"
#define SHIFT 1
#include "ops_sse_header.h"

DEF_HELPER(target_ulong, helper_rclb, (target_ulong t0, target_ulong t1))
DEF_HELPER(target_ulong, helper_rclw, (target_ulong t0, target_ulong t1))
DEF_HELPER(target_ulong, helper_rcll, (target_ulong t0, target_ulong t1))
DEF_HELPER(target_ulong, helper_rcrb, (target_ulong t0, target_ulong t1))
DEF_HELPER(target_ulong, helper_rcrw, (target_ulong t0, target_ulong t1))
DEF_HELPER(target_ulong, helper_rcrl, (target_ulong t0, target_ulong t1))
#ifdef TARGET_X86_64
DEF_HELPER(target_ulong, helper_rclq, (target_ulong t0, target_ulong t1))
DEF_HELPER(target_ulong, helper_rcrq, (target_ulong t0, target_ulong t1))
#endif

#ifdef VBOX
void helper_external_event(void);
void helper_record_call(void);

/* in op_helper.c */
void sync_seg(CPUX86State *env1, int seg_reg, int selector);
void sync_ldtr(CPUX86State *env1, int selector);

#endif

#undef DEF_HELPER
