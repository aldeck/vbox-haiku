/*
 *  Save/restore host registers.
 *
 *  Copyright (c) 2007 CodeSourcery
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 * Oracle LGPL Disclaimer: For the avoidance of doubt, except that if any license choice
 * other than GPL or LGPL is available it will apply instead, Oracle elects to use only
 * the Lesser General Public License version 2.1 (LGPLv2) at this time for any software where
 * a choice of LGPL license versions is made available with the language indicating
 * that LGPLv2 or any later version may be used, or where a choice of which version
 * of the LGPL is applied is otherwise unspecified.
 */

/* The GCC global register variable extension is used to reserve some
   host registers for use by dyngen.  However only the core parts of the
   translation engine are compiled with these settings.  We must manually
   save/restore these registers when called from regular code.
   It is not sufficient to save/restore T0 et. al. as these may be declared
   with a datatype smaller than the actual register.  */

#if defined(DECLARE_HOST_REGS)

#ifndef VBOX
#define DO_REG(REG)					\
    register host_reg_t reg_AREG##REG asm(AREG##REG);	\
    volatile host_reg_t saved_AREG##REG;
#else
#define DO_REG(REG)			               	           \
    REGISTER_BOUND_GLOBAL(host_reg_t, reg_AREG##REG, AREG##REG);   \
    volatile host_reg_t saved_AREG##REG;
#endif

#elif defined(SAVE_HOST_REGS)

#ifndef VBOX
#define DO_REG(REG)					\
    __asm__ __volatile__ ("" : "=r" (reg_AREG##REG));	\
    saved_AREG##REG = reg_AREG##REG;
#else /* VBOX */
#define DO_REG(REG)					\
    SAVE_GLOBAL_REGISTER(REG, reg_AREG##REG);	        \
    saved_AREG##REG = reg_AREG##REG;
#endif /* VBOX */

#else

#ifndef VBOX
#define DO_REG(REG)                                     \
    reg_AREG##REG = saved_AREG##REG;		        \
    __asm__ __volatile__ ("" : : "r" (reg_AREG##REG));
#else /* VBOX */
#define DO_REG(REG)                                     \
    reg_AREG##REG = saved_AREG##REG;		        \
    RESTORE_GLOBAL_REGISTER(REG, reg_AREG##REG);
#endif

#endif

#ifdef AREG0
DO_REG(0)
#endif

#ifdef AREG1
DO_REG(1)
#endif

#ifdef AREG2
DO_REG(2)
#endif

#ifdef AREG3
DO_REG(3)
#endif

#ifdef AREG4
DO_REG(4)
#endif

#ifdef AREG5
DO_REG(5)
#endif

#ifdef AREG6
DO_REG(6)
#endif

#ifdef AREG7
DO_REG(7)
#endif

#ifdef AREG8
DO_REG(8)
#endif

#ifdef AREG9
DO_REG(9)
#endif

#ifdef AREG10
DO_REG(10)
#endif

#ifdef AREG11
DO_REG(11)
#endif

#undef SAVE_HOST_REGS
#undef DECLARE_HOST_REGS
#undef DO_REG
