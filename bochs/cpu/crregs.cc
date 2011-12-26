/////////////////////////////////////////////////////////////////////////
// $Id$
/////////////////////////////////////////////////////////////////////////
//
//   Copyright (c) 2010-2011 Stanislav Shwartsman
//          Written by Stanislav Shwartsman [sshwarts at sourceforge net]
//
//  This library is free software; you can redistribute it and/or
//  modify it under the terms of the GNU Lesser General Public
//  License as published by the Free Software Foundation; either
//  version 2 of the License, or (at your option) any later version.
//
//  This library is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
//  Lesser General Public License for more details.
//
//  You should have received a copy of the GNU Lesser General Public
//  License along with this library; if not, write to the Free Software
//  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA B 02110-1301 USA
//
/////////////////////////////////////////////////////////////////////////

#define NEED_CPU_REG_SHORTCUTS 1
#include "bochs.h"
#include "cpu.h"
#define LOG_THIS BX_CPU_THIS_PTR

BX_INSF_TYPE BX_CPP_AttrRegparmN(1) BX_CPU_C::MOV_DdRd(bxInstruction_c *i)
{
#if BX_SUPPORT_VMX
  if (BX_CPU_THIS_PTR in_vmx_guest)
    VMexit_DR_Access(i, 0 /* write */);
#endif

#if BX_CPU_LEVEL >= 5
  if (BX_CPU_THIS_PTR cr4.get_DE()) {
    if ((i->nnn() & 0xE) == 4) {
      BX_ERROR(("MOV_DdRd: access to DR4/DR5 causes #UD"));
      exception(BX_UD_EXCEPTION, 0);
    }
  }
#endif

  // Note: processor clears GD upon entering debug exception
  // handler, to allow access to the debug registers
  if (BX_CPU_THIS_PTR dr7.get_GD()) {
    BX_ERROR(("MOV_DdRd: DR7 GD bit is set"));
    BX_CPU_THIS_PTR debug_trap |= BX_DEBUG_DR_ACCESS_BIT;
    exception(BX_DB_EXCEPTION, 0);
  }

  // CPL is always 0 in real mode
  if (/* !real_mode() && */ CPL!=0) {
    BX_ERROR(("MOV_DdRd: CPL!=0 not in real mode"));
    exception(BX_GP_EXCEPTION, 0);
  }

#if BX_SUPPORT_SVM
  if (SVM_DR_WRITE_INTERCEPTED(i->nnn())) Svm_Vmexit(SVM_VMEXIT_DR0_WRITE + i->nnn());
#endif

  invalidate_prefetch_q();

  /* This instruction is always treated as a register-to-register,
   * regardless of the encoding of the MOD field in the MODRM byte.
   */
  if (!i->modC0())
    BX_PANIC(("MOV_DdRd(): rm field not a register!"));

  Bit32u val_32 = BX_READ_32BIT_REG(i->rm());

  switch (i->nnn()) {
    case 0: // DR0
    case 1: // DR1
    case 2: // DR2
    case 3: // DR3
      BX_CPU_THIS_PTR dr[i->nnn()] = val_32;
      TLB_invlpg(val_32);
      break;

    case 4: // DR4
      // DR4 aliased to DR6 by default. With Debug Extensions on,
      // access to DR4 causes #UD
    case 6: // DR6
#if BX_CPU_LEVEL <= 4
      // On 386/486 bit12 is settable
      BX_CPU_THIS_PTR dr6.val32 = (BX_CPU_THIS_PTR dr6.val32 & 0xffff0ff0) |
                            (val_32 & 0x0000f00f);
#else
      // On Pentium+, bit12 is always zero
      BX_CPU_THIS_PTR dr6.val32 = (BX_CPU_THIS_PTR dr6.val32 & 0xffff0ff0) |
                            (val_32 & 0x0000e00f);
#endif
      break;

    case 5: // DR5
      // DR5 aliased to DR7 by default. With Debug Extensions on,
      // access to DR5 causes #UD
    case 7: // DR7
      // Note: 486+ ignore GE and LE flags.  On the 386, exact
      // data breakpoint matching does not occur unless it is enabled
      // by setting the LE and/or GE flags.

#if BX_CPU_LEVEL <= 4
      // 386/486: you can play with all the bits except b10 is always 1
      BX_CPU_THIS_PTR dr7.set32(val_32 | 0x00000400);
#else
      // Pentium+: bits15,14,12 are hardwired to 0, rest are settable.
      // Even bits 11,10 are changeable though reserved.
      BX_CPU_THIS_PTR dr7.set32((val_32 & 0xffff2fff) | 0x00000400);
#endif
#if BX_X86_DEBUGGER
      // Some sanity checks...
      if ((BX_CPU_THIS_PTR dr7.get_R_W0() == BX_HWDebugInstruction && BX_CPU_THIS_PTR dr7.get_LEN0()) ||
          (BX_CPU_THIS_PTR dr7.get_R_W1() == BX_HWDebugInstruction && BX_CPU_THIS_PTR dr7.get_LEN1()) ||
          (BX_CPU_THIS_PTR dr7.get_R_W2() == BX_HWDebugInstruction && BX_CPU_THIS_PTR dr7.get_LEN2()) ||
          (BX_CPU_THIS_PTR dr7.get_R_W3() == BX_HWDebugInstruction && BX_CPU_THIS_PTR dr7.get_LEN3()))
      {
        // Instruction breakpoint with LENx not 00b (1-byte length)
        BX_ERROR(("MOV_DdRd: write of %08x, R/W=00b LEN!=00b", val_32));
      }
#endif

      TLB_flush(); // the DR7 write could enable some breakpoints
      break;

    default:
      BX_ERROR(("MOV_DdRd: #UD - register index out of range"));
      exception(BX_UD_EXCEPTION, 0);
  }

  BX_NEXT_TRACE(i);
}

BX_INSF_TYPE BX_CPP_AttrRegparmN(1) BX_CPU_C::MOV_RdDd(bxInstruction_c *i)
{
  Bit32u val_32;

#if BX_SUPPORT_VMX
  if (BX_CPU_THIS_PTR in_vmx_guest)
    VMexit_DR_Access(i, 1 /* read */);
#endif

#if BX_CPU_LEVEL >= 5
  if (BX_CPU_THIS_PTR cr4.get_DE()) {
    if ((i->nnn() & 0xE) == 4) {
      BX_ERROR(("MOV_RdDd: access to DR4/DR5 causes #UD"));
      exception(BX_UD_EXCEPTION, 0);
    }
  }
#endif

  // Note: processor clears GD upon entering debug exception
  // handler, to allow access to the debug registers
  if (BX_CPU_THIS_PTR dr7.get_GD()) {
    BX_ERROR(("MOV_RdDd: DR7 GD bit is set"));
    BX_CPU_THIS_PTR debug_trap |= BX_DEBUG_DR_ACCESS_BIT;
    exception(BX_DB_EXCEPTION, 0);
  }

  // CPL is always 0 in real mode
  if (/* !real_mode() && */ CPL!=0) {
    BX_ERROR(("MOV_RdDd: CPL!=0 not in real mode"));
    exception(BX_GP_EXCEPTION, 0);
  }

#if BX_SUPPORT_SVM
  if (SVM_DR_READ_INTERCEPTED(i->nnn())) Svm_Vmexit(SVM_VMEXIT_DR0_READ + i->nnn());
#endif

  /* This instruction is always treated as a register-to-register,
   * regardless of the encoding of the MOD field in the MODRM byte.
   */
  if (!i->modC0())
    BX_PANIC(("MOV_RdDd(): rm field not a register!"));

  switch (i->nnn()) {
    case 0: // DR0
    case 1: // DR1
    case 2: // DR2
    case 3: // DR3
      val_32 = (Bit32u) BX_CPU_THIS_PTR dr[i->nnn()];
      break;

    case 4: // DR4
      // DR4 aliased to DR6 by default. With Debug Extensions ON,
      // access to DR4 causes #UD
    case 6: // DR6
      val_32 = BX_CPU_THIS_PTR dr6.get32();
      break;

    case 5: // DR5
      // DR5 aliased to DR7 by default. With Debug Extensions ON,
      // access to DR5 causes #UD
    case 7: // DR7
      val_32 = BX_CPU_THIS_PTR dr7.get32();
      break;

    default:
      BX_ERROR(("MOV_RdDd: #UD - register index out of range"));
      exception(BX_UD_EXCEPTION, 0);
  }

  BX_WRITE_32BIT_REGZ(i->rm(), val_32);

  BX_NEXT_INSTR(i);
}

#if BX_SUPPORT_X86_64
BX_INSF_TYPE BX_CPP_AttrRegparmN(1) BX_CPU_C::MOV_DqRq(bxInstruction_c *i)
{
#if BX_SUPPORT_VMX
  if (BX_CPU_THIS_PTR in_vmx_guest)
    VMexit_DR_Access(i, 0 /* write */);
#endif

  if (BX_CPU_THIS_PTR cr4.get_DE()) {
    if ((i->nnn() & 0xE) == 4) {
      BX_ERROR(("MOV_DqRq: access to DR4/DR5 causes #UD"));
      exception(BX_UD_EXCEPTION, 0);
    }
  }

  if (i->nnn() >= 8) {
    BX_ERROR(("MOV_DqRq: #UD - register index out of range"));
    exception(BX_UD_EXCEPTION, 0);
  }

  // Note: processor clears GD upon entering debug exception
  // handler, to allow access to the debug registers
  if (BX_CPU_THIS_PTR dr7.get_GD()) {
    BX_ERROR(("MOV_DqRq: DR7 GD bit is set"));
    BX_CPU_THIS_PTR debug_trap |= BX_DEBUG_DR_ACCESS_BIT;
    exception(BX_DB_EXCEPTION, 0);
  }

  /* #GP(0) if CPL is not 0 */
  if (CPL != 0) {
    BX_ERROR(("MOV_DqRq: #GP(0) if CPL is not 0"));
    exception(BX_GP_EXCEPTION, 0);
  }

#if BX_SUPPORT_SVM
  if (SVM_DR_WRITE_INTERCEPTED(i->nnn())) Svm_Vmexit(SVM_VMEXIT_DR0_WRITE + i->nnn());
#endif

  invalidate_prefetch_q();

  /* This instruction is always treated as a register-to-register,
   * regardless of the encoding of the MOD field in the MODRM byte.
   */
  if (!i->modC0())
    BX_PANIC(("MOV_DqRq(): rm field not a register!"));

  Bit64u val_64 = BX_READ_64BIT_REG(i->rm());

  switch (i->nnn()) {
    case 0: // DR0
    case 1: // DR1
    case 2: // DR2
    case 3: // DR3
      BX_CPU_THIS_PTR dr[i->nnn()] = val_64;
      TLB_invlpg(val_64);
      break;

    case 4: // DR4
      // DR4 aliased to DR6 by default. With Debug Extensions ON,
      // access to DR4 causes #UD
    case 6: // DR6
      if (GET32H(val_64)) {
        BX_ERROR(("MOV_DqRq: attempt to set upper part of DR6"));
        exception(BX_GP_EXCEPTION, 0);
      }
      // On Pentium+, bit12 is always zero
      BX_CPU_THIS_PTR dr6.val32 = (BX_CPU_THIS_PTR dr6.val32 & 0xffff0ff0) |
                            (val_64 & 0x0000e00f);
      break;

    case 5: // DR5
      // DR5 aliased to DR7 by default. With Debug Extensions ON,
      // access to DR5 causes #UD
    case 7: // DR7
      // Note: 486+ ignore GE and LE flags.  On the 386, exact
      // data breakpoint matching does not occur unless it is enabled
      // by setting the LE and/or GE flags.

      if (GET32H(val_64)) {
        BX_ERROR(("MOV_DqRq: attempt to set upper part of DR7"));
        exception(BX_GP_EXCEPTION, 0);
      }

      // Pentium+: bits15,14,12 are hardwired to 0, rest are settable.
      // Even bits 11,10 are changeable though reserved.
      BX_CPU_THIS_PTR dr7.set32((val_64 & 0xffff2fff) | 0x00000400);

#if BX_X86_DEBUGGER
      if ((BX_CPU_THIS_PTR dr7.get_R_W0() == BX_HWDebugInstruction && BX_CPU_THIS_PTR dr7.get_LEN0()) ||
          (BX_CPU_THIS_PTR dr7.get_R_W1() == BX_HWDebugInstruction && BX_CPU_THIS_PTR dr7.get_LEN1()) ||
          (BX_CPU_THIS_PTR dr7.get_R_W2() == BX_HWDebugInstruction && BX_CPU_THIS_PTR dr7.get_LEN2()) ||
          (BX_CPU_THIS_PTR dr7.get_R_W3() == BX_HWDebugInstruction && BX_CPU_THIS_PTR dr7.get_LEN3()))
      {
        // Instruction breakpoint with LENx not 00b (1-byte length)
        BX_ERROR(("MOV_DqRq: write of %08x, R/W=00b LEN!=00b", BX_CPU_THIS_PTR dr7.get32()));
      }
#endif

      TLB_flush(); // the DR7 write could enable some breakpoints
      break;

    default:
      BX_ERROR(("MOV_DqRq: #UD - register index out of range"));
      exception(BX_UD_EXCEPTION, 0);
  }

  BX_NEXT_TRACE(i);
}

BX_INSF_TYPE BX_CPP_AttrRegparmN(1) BX_CPU_C::MOV_RqDq(bxInstruction_c *i)
{
  Bit64u val_64;

#if BX_SUPPORT_VMX
  if (BX_CPU_THIS_PTR in_vmx_guest)
    VMexit_DR_Access(i, 1 /* read */);
#endif

  if (BX_CPU_THIS_PTR cr4.get_DE()) {
    if ((i->nnn() & 0xE) == 4) {
      BX_ERROR(("MOV_RqDq: access to DR4/DR5 causes #UD"));
      exception(BX_UD_EXCEPTION, 0);
    }
  }

  if (i->nnn() >= 8) {
    BX_ERROR(("MOV_RqDq: #UD - register index out of range"));
    exception(BX_UD_EXCEPTION, 0);
  }

  // Note: processor clears GD upon entering debug exception
  // handler, to allow access to the debug registers
  if (BX_CPU_THIS_PTR dr7.get_GD()) {
    BX_ERROR(("MOV_RqDq: DR7 GD bit is set"));
    BX_CPU_THIS_PTR debug_trap |= BX_DEBUG_DR_ACCESS_BIT;
    exception(BX_DB_EXCEPTION, 0);
  }

  /* #GP(0) if CPL is not 0 */
  if (CPL != 0) {
    BX_ERROR(("MOV_RqDq: #GP(0) if CPL is not 0"));
    exception(BX_GP_EXCEPTION, 0);
  }

#if BX_SUPPORT_SVM
  if (SVM_DR_READ_INTERCEPTED(i->nnn())) Svm_Vmexit(SVM_VMEXIT_DR0_READ + i->nnn());
#endif

  /* This instruction is always treated as a register-to-register,
   * regardless of the encoding of the MOD field in the MODRM byte.
   */
  if (!i->modC0())
    BX_PANIC(("MOV_RqDq(): rm field not a register!"));

  switch (i->nnn()) {
    case 0: // DR0
    case 1: // DR1
    case 2: // DR2
    case 3: // DR3
      val_64 = BX_CPU_THIS_PTR dr[i->nnn()];
      break;

    case 4: // DR4
      // DR4 aliased to DR6 by default. With Debug Extensions ON,
      // access to DR4 causes #UD
    case 6: // DR6
      val_64 = BX_CPU_THIS_PTR dr6.get32();
      break;

    case 5: // DR5
      // DR5 aliased to DR7 by default. With Debug Extensions ON,
      // access to DR5 causes #UD
    case 7: // DR7
      val_64 = BX_CPU_THIS_PTR dr7.get32();
      break;

    default:
      BX_ERROR(("MOV_RqDq: #UD - register index out of range"));
      exception(BX_UD_EXCEPTION, 0);
  }

  BX_WRITE_64BIT_REG(i->rm(), val_64);

  BX_NEXT_INSTR(i);
}
#endif // #if BX_SUPPORT_X86_64

BX_INSF_TYPE BX_CPP_AttrRegparmN(1) BX_CPU_C::MOV_CR0Rd(bxInstruction_c *i)
{
  // CPL is always 0 in real mode
  if (/* !real_mode() && */ CPL!=0) {
    BX_ERROR(("MOV_CR0Rd: CPL!=0 not in real mode"));
    exception(BX_GP_EXCEPTION, 0);
  }

  invalidate_prefetch_q();

  Bit32u val_32 = BX_READ_32BIT_REG(i->rm());

  if (i->nnn() == 0) {
    // CR0
#if BX_SUPPORT_VMX
    if (BX_CPU_THIS_PTR in_vmx_guest)
      val_32 = (Bit32u) VMexit_CR0_Write(i, val_32);
#endif
    if (! SetCR0(val_32))
      exception(BX_GP_EXCEPTION, 0);

    BX_INSTR_TLB_CNTRL(BX_CPU_ID, BX_INSTR_MOV_CR0, val_32);
  }
#if BX_CPU_LEVEL >= 6
  else {
    // AMD feature: LOCK CR0 allows CR8 access even in 32-bit mode
    WriteCR8(i, val_32);
  }
#endif

  BX_NEXT_TRACE(i);
}

BX_INSF_TYPE BX_CPP_AttrRegparmN(1) BX_CPU_C::MOV_CR2Rd(bxInstruction_c *i)
{
  // CPL is always 0 in real mode
  if (/* !real_mode() && */ CPL!=0) {
    BX_ERROR(("MOV_CR2Rd: CPL!=0 not in real mode"));
    exception(BX_GP_EXCEPTION, 0);
  }

#if BX_SUPPORT_SVM
  if (BX_CPU_THIS_PTR in_svm_guest) {
    if(SVM_CR_WRITE_INTERCEPTED(2)) Svm_Vmexit(SVM_VMEXIT_CR2_WRITE);
  }
#endif

  BX_CPU_THIS_PTR cr2 = BX_READ_32BIT_REG(i->rm());

  BX_NEXT_INSTR(i);
}

BX_INSF_TYPE BX_CPP_AttrRegparmN(1) BX_CPU_C::MOV_CR3Rd(bxInstruction_c *i)
{
  // CPL is always 0 in real mode
  if (/* !real_mode() && */ CPL!=0) {
    BX_ERROR(("MOV_CR3Rd: CPL!=0 not in real mode"));
    exception(BX_GP_EXCEPTION, 0);
  }

  invalidate_prefetch_q();

  Bit32u val_32 = BX_READ_32BIT_REG(i->rm());

#if BX_SUPPORT_VMX
  if (BX_CPU_THIS_PTR in_vmx_guest)
    VMexit_CR3_Write(i, val_32);
#endif

#if BX_CPU_LEVEL >= 6
  if (BX_CPU_THIS_PTR cr0.get_PG() && BX_CPU_THIS_PTR cr4.get_PAE() && !long_mode()) {
    if (! CheckPDPTR(val_32)) {
      BX_ERROR(("SetCR3(): PDPTR check failed !"));
      exception(BX_GP_EXCEPTION, 0);
    }
  }
#endif

  if (! SetCR3(val_32))
    exception(BX_GP_EXCEPTION, 0);

  BX_INSTR_TLB_CNTRL(BX_CPU_ID, BX_INSTR_MOV_CR3, val_32);

  BX_NEXT_TRACE(i);
}

BX_INSF_TYPE BX_CPP_AttrRegparmN(1) BX_CPU_C::MOV_CR4Rd(bxInstruction_c *i)
{
#if BX_CPU_LEVEL >= 5
  // CPL is always 0 in real mode
  if (/* !real_mode() && */ CPL!=0) {
    BX_ERROR(("MOV_CR4Rd: CPL!=0 not in real mode"));
    exception(BX_GP_EXCEPTION, 0);
  }

  invalidate_prefetch_q();

  Bit32u val_32 = BX_READ_32BIT_REG(i->rm());
#if BX_SUPPORT_VMX
  if (BX_CPU_THIS_PTR in_vmx_guest)
    val_32 = (Bit32u) VMexit_CR4_Write(i, val_32);
#endif
  if (! SetCR4(val_32))
    exception(BX_GP_EXCEPTION, 0);

  BX_INSTR_TLB_CNTRL(BX_CPU_ID, BX_INSTR_MOV_CR4, val_32);
#endif

  BX_NEXT_TRACE(i);
}

BX_INSF_TYPE BX_CPP_AttrRegparmN(1) BX_CPU_C::MOV_RdCR0(bxInstruction_c *i)
{
  // CPL is always 0 in real mode
  if (/* !real_mode() && */ CPL!=0) {
    BX_ERROR(("MOV_RdCR0: CPL!=0 not in real mode"));
    exception(BX_GP_EXCEPTION, 0);
  }

  Bit32u val_32 = 0;

  if (i->nnn() == 0) {
    // CR0
    val_32 = (Bit32u) read_CR0(); /* correctly handle VMX */
  }
#if BX_CPU_LEVEL >= 6
  else {
    // AMD feature: LOCK CR0 allows CR8 access even in 32-bit mode
    val_32 = ReadCR8(i);
  }
#endif

  BX_WRITE_32BIT_REGZ(i->rm(), val_32);

  BX_NEXT_INSTR(i);
}

BX_INSF_TYPE BX_CPP_AttrRegparmN(1) BX_CPU_C::MOV_RdCR2(bxInstruction_c *i)
{
  // CPL is always 0 in real mode
  if (/* !real_mode() && */ CPL!=0) {
    BX_ERROR(("MOV_RdCd: CPL!=0 not in real mode"));
    exception(BX_GP_EXCEPTION, 0);
  }

#if BX_SUPPORT_SVM
  if (BX_CPU_THIS_PTR in_svm_guest) {
    if(SVM_CR_READ_INTERCEPTED(2)) Svm_Vmexit(SVM_VMEXIT_CR2_READ);
  }
#endif

  BX_WRITE_32BIT_REGZ(i->rm(), (Bit32u) BX_CPU_THIS_PTR cr2);

  BX_NEXT_INSTR(i);
}

BX_INSF_TYPE BX_CPP_AttrRegparmN(1) BX_CPU_C::MOV_RdCR3(bxInstruction_c *i)
{
  // CPL is always 0 in real mode
  if (/* !real_mode() && */ CPL!=0) {
    BX_ERROR(("MOV_RdCd: CPL!=0 not in real mode"));
    exception(BX_GP_EXCEPTION, 0);
  }

#if BX_SUPPORT_SVM
  if (BX_CPU_THIS_PTR in_svm_guest) {
    if(SVM_CR_READ_INTERCEPTED(3)) Svm_Vmexit(SVM_VMEXIT_CR3_READ);
  }
#endif

#if BX_SUPPORT_VMX
  if (BX_CPU_THIS_PTR in_vmx_guest)
    VMexit_CR3_Read(i);
#endif

  Bit32u val_32 = (Bit32u) BX_CPU_THIS_PTR cr3;

  BX_WRITE_32BIT_REGZ(i->rm(), val_32);

  BX_NEXT_INSTR(i);
}

BX_INSF_TYPE BX_CPP_AttrRegparmN(1) BX_CPU_C::MOV_RdCR4(bxInstruction_c *i)
{
#if BX_CPU_LEVEL >= 5
  // CPL is always 0 in real mode
  if (/* !real_mode() && */ CPL!=0) {
    BX_ERROR(("MOV_RdCd: CPL!=0 not in real mode"));
    exception(BX_GP_EXCEPTION, 0);
  }

  Bit32u val_32 = (Bit32u) read_CR4(); /* correctly handle VMX */

  BX_WRITE_32BIT_REGZ(i->rm(), val_32);
#endif

  BX_NEXT_INSTR(i);
}

#if BX_SUPPORT_X86_64
BX_INSF_TYPE BX_CPP_AttrRegparmN(1) BX_CPU_C::MOV_CR0Rq(bxInstruction_c *i)
{
  if (CPL!=0) {
    BX_ERROR(("MOV_CR0Rq: #GP(0) if CPL is not 0"));
    exception(BX_GP_EXCEPTION, 0);
  }

  invalidate_prefetch_q();

  Bit64u val_64 = BX_READ_64BIT_REG(i->rm());

  if (i->nnn() == 0) {
    // CR0
#if BX_SUPPORT_VMX
    if (BX_CPU_THIS_PTR in_vmx_guest)
      val_64 = VMexit_CR0_Write(i, val_64);
#endif
    if (! SetCR0(val_64))
      exception(BX_GP_EXCEPTION, 0);

    BX_INSTR_TLB_CNTRL(BX_CPU_ID, BX_INSTR_MOV_CR0, (Bit32u) val_64);
  }
  else {
    WriteCR8(i, val_64);
  }

  BX_NEXT_TRACE(i);
}

BX_INSF_TYPE BX_CPP_AttrRegparmN(1) BX_CPU_C::MOV_CR2Rq(bxInstruction_c *i)
{
  if (i->nnn() != 2) {
    BX_ERROR(("MOV_CR2Rq: #UD - register index out of range"));
    exception(BX_UD_EXCEPTION, 0);
  }

  if (CPL!=0) {
    BX_ERROR(("MOV_CR2Rq: #GP(0) if CPL is not 0"));
    exception(BX_GP_EXCEPTION, 0);
  }

  BX_CPU_THIS_PTR cr2 = BX_READ_64BIT_REG(i->rm());

  BX_NEXT_INSTR(i);
}

BX_INSF_TYPE BX_CPP_AttrRegparmN(1) BX_CPU_C::MOV_CR3Rq(bxInstruction_c *i)
{
  if (i->nnn() != 3) {
    BX_ERROR(("MOV_CR3Rq: #UD - register index out of range"));
    exception(BX_UD_EXCEPTION, 0);
  }

  if (CPL!=0) {
    BX_ERROR(("MOV_CR3Rq: #GP(0) if CPL is not 0"));
    exception(BX_GP_EXCEPTION, 0);
  }

  invalidate_prefetch_q();

  Bit64u val_64 = BX_READ_64BIT_REG(i->rm());

#if BX_SUPPORT_VMX
  if (BX_CPU_THIS_PTR in_vmx_guest)
    VMexit_CR3_Write(i, val_64);
#endif

  // no PDPTR checks in long mode
  if (! SetCR3(val_64))
    exception(BX_GP_EXCEPTION, 0);

  BX_INSTR_TLB_CNTRL(BX_CPU_ID, BX_INSTR_MOV_CR3, val_64);

  BX_NEXT_TRACE(i);
}

BX_INSF_TYPE BX_CPP_AttrRegparmN(1) BX_CPU_C::MOV_CR4Rq(bxInstruction_c *i)
{
  if (i->nnn() != 4) {
    BX_ERROR(("MOV_CR4Rq: #UD - register index out of range"));
    exception(BX_UD_EXCEPTION, 0);
  }

  if (CPL!=0) {
    BX_ERROR(("MOV_CR4Rq: #GP(0) if CPL is not 0"));
    exception(BX_GP_EXCEPTION, 0);
  }

  invalidate_prefetch_q();

  Bit64u val_64 = BX_READ_64BIT_REG(i->rm());
#if BX_SUPPORT_VMX
  if (BX_CPU_THIS_PTR in_vmx_guest)
    val_64 = VMexit_CR4_Write(i, val_64);
#endif
  BX_DEBUG(("MOV_CqRq: write to CR4 of %08x:%08x", GET32H(val_64), GET32L(val_64)));
  if (! SetCR4(val_64))
    exception(BX_GP_EXCEPTION, 0);

  BX_INSTR_TLB_CNTRL(BX_CPU_ID, BX_INSTR_MOV_CR4, (Bit32u) val_64);

  BX_NEXT_TRACE(i);
}

BX_INSF_TYPE BX_CPP_AttrRegparmN(1) BX_CPU_C::MOV_RqCR0(bxInstruction_c *i)
{
  if (CPL!=0) {
    BX_ERROR(("MOV_RqCq: #GP(0) if CPL is not 0"));
    exception(BX_GP_EXCEPTION, 0);
  }

  Bit64u val_64;

  if (i->nnn() == 0) {
    // CR0
    val_64 = read_CR0(); /* correctly handle VMX */
  }
  else {
    // CR8
    val_64 = ReadCR8(i);
  }

  BX_WRITE_64BIT_REG(i->rm(), val_64);

  BX_NEXT_INSTR(i);
}

BX_INSF_TYPE BX_CPP_AttrRegparmN(1) BX_CPU_C::MOV_RqCR2(bxInstruction_c *i)
{
  if (i->nnn() != 2) {
    BX_ERROR(("MOV_RqCR2: #UD - register index out of range"));
    exception(BX_UD_EXCEPTION, 0);
  }

  if (CPL!=0) {
    BX_ERROR(("MOV_RqCR2: #GP(0) if CPL is not 0"));
    exception(BX_GP_EXCEPTION, 0);
  }

  BX_WRITE_64BIT_REG(i->rm(), BX_CPU_THIS_PTR cr2);

  BX_NEXT_INSTR(i);
}

BX_INSF_TYPE BX_CPP_AttrRegparmN(1) BX_CPU_C::MOV_RqCR3(bxInstruction_c *i)
{
  if (i->nnn() != 3) {
    BX_ERROR(("MOV_RqCR3: #UD - register index out of range"));
    exception(BX_UD_EXCEPTION, 0);
  }

  if (CPL!=0) {
    BX_ERROR(("MOV_RqCR3: #GP(0) if CPL is not 0"));
    exception(BX_GP_EXCEPTION, 0);
  }

#if BX_SUPPORT_VMX
  if (BX_CPU_THIS_PTR in_vmx_guest)
    VMexit_CR3_Read(i);
#endif

  BX_WRITE_64BIT_REG(i->rm(), BX_CPU_THIS_PTR cr3);

  BX_NEXT_INSTR(i);
}

BX_INSF_TYPE BX_CPP_AttrRegparmN(1) BX_CPU_C::MOV_RqCR4(bxInstruction_c *i)
{
  if (i->nnn() != 4) {
    BX_ERROR(("MOV_RqCR4: #UD - register index out of range"));
    exception(BX_UD_EXCEPTION, 0);
  }

  if (CPL!=0) {
    BX_ERROR(("MOV_RqCR4: #GP(0) if CPL is not 0"));
    exception(BX_GP_EXCEPTION, 0);
  }

  Bit64u val_64 = read_CR4(); /* correctly handle VMX */

  BX_WRITE_64BIT_REG(i->rm(), val_64);

  BX_NEXT_INSTR(i);
}
#endif // #if BX_SUPPORT_X86_64

BX_INSF_TYPE BX_CPP_AttrRegparmN(1) BX_CPU_C::LMSW_Ew(bxInstruction_c *i)
{
  Bit16u msw;

  // CPL is always 0 in real mode
  if (/* !real_mode() && */ CPL!=0) {
    BX_ERROR(("LMSW: CPL!=0 not in real mode"));
    exception(BX_GP_EXCEPTION, 0);
  }

#if BX_SUPPORT_SVM
  if (BX_CPU_THIS_PTR in_svm_guest) {
    if(SVM_CR_WRITE_INTERCEPTED(0)) Svm_Vmexit(SVM_VMEXIT_CR0_WRITE);
  }  
#endif

  if (i->modC0()) {
    msw = BX_READ_16BIT_REG(i->rm());
  }
  else {
    /* use RMAddr(i) to save address for VMexit */
    RMAddr(i) = BX_CPU_CALL_METHODR(i->ResolveModrm, (i));
    /* pointer, segment address pair */
    msw = read_virtual_word(i->seg(), RMAddr(i));
  }

  // LMSW does not affect PG,CD,NW,AM,WP,NE,ET bits, and cannot clear PE

#if BX_SUPPORT_VMX
  if (BX_CPU_THIS_PTR in_vmx_guest)
    msw = VMexit_LMSW(i, msw);
#endif

  // LMSW cannot clear PE
  if (BX_CPU_THIS_PTR cr0.get_PE())
    msw |= BX_CR0_PE_MASK; // adjust PE bit to current value of 1

  msw &= 0xf; // LMSW only affects last 4 flags

  Bit32u cr0 = (BX_CPU_THIS_PTR cr0.get32() & 0xfffffff0) | msw;
  if (! SetCR0(cr0))
    exception(BX_GP_EXCEPTION, 0);

  BX_NEXT_TRACE(i);
}

BX_INSF_TYPE BX_CPP_AttrRegparmN(1) BX_CPU_C::SMSW_EwR(bxInstruction_c *i)
{
  Bit32u msw = (Bit32u) read_CR0();  // handle CR0 shadow in VMX

  if (i->os32L()) {
    BX_WRITE_32BIT_REGZ(i->rm(), msw);
  }
  else {
    BX_WRITE_16BIT_REG(i->rm(), msw & 0xffff);
  }

  BX_NEXT_INSTR(i);
}

BX_INSF_TYPE BX_CPP_AttrRegparmN(1) BX_CPU_C::SMSW_EwM(bxInstruction_c *i)
{
  Bit16u msw = read_CR0() & 0xffff;   // handle CR0 shadow in VMX
  bx_address eaddr = BX_CPU_CALL_METHODR(i->ResolveModrm, (i));
  write_virtual_word(i->seg(), eaddr, msw);

  BX_NEXT_INSTR(i);
}

bx_address BX_CPU_C::read_CR0(void)
{
  bx_address cr0_val = BX_CPU_THIS_PTR cr0.get32();

#if BX_SUPPORT_SVM
  if (BX_CPU_THIS_PTR in_vmx_guest) {
    if(SVM_CR_READ_INTERCEPTED(0)) Svm_Vmexit(SVM_VMEXIT_CR0_READ);
  }
#endif

#if BX_SUPPORT_VMX
  if (BX_CPU_THIS_PTR in_vmx_guest) {
    VMCS_CACHE *vm = &BX_CPU_THIS_PTR vmcs;
    cr0_val = (cr0_val & ~vm->vm_cr0_mask) | (vm->vm_cr0_read_shadow & vm->vm_cr0_mask);
  }
#endif

  return cr0_val;
}

#if BX_CPU_LEVEL >= 5
bx_address BX_CPU_C::read_CR4(void)
{
  bx_address cr4_val = BX_CPU_THIS_PTR cr4.get32();

#if BX_SUPPORT_SVM
  if (BX_CPU_THIS_PTR in_vmx_guest) {
    if(SVM_CR_READ_INTERCEPTED(4)) Svm_Vmexit(SVM_VMEXIT_CR4_READ);
  }
#endif

#if BX_SUPPORT_VMX
  if (BX_CPU_THIS_PTR in_vmx_guest) {
    VMCS_CACHE *vm = &BX_CPU_THIS_PTR vmcs;
    cr4_val = (cr4_val & ~vm->vm_cr4_mask) | (vm->vm_cr4_read_shadow & vm->vm_cr4_mask);
  }
#endif

  return cr4_val;
}
#endif

bx_bool BX_CPP_AttrRegparmN(1) BX_CPU_C::check_CR0(bx_address cr0_val)
{
  bx_cr0_t temp_cr0;

#if BX_SUPPORT_X86_64
  if (GET32H(cr0_val)) {
    BX_ERROR(("check_CR0(): trying to set CR0 > 32 bits"));
    return 0;
  }
#endif

  temp_cr0.set32((Bit32u) cr0_val);

  if (temp_cr0.get_PG() && !temp_cr0.get_PE()) {
    BX_ERROR(("check_CR0(0x%08x): attempt to set CR0.PG with CR0.PE cleared !", temp_cr0.get32()));
    return 0;
  }

#if BX_CPU_LEVEL >= 4
  if (temp_cr0.get_NW() && !temp_cr0.get_CD()) {
    BX_ERROR(("check_CR0(0x%08x): attempt to set CR0.NW with CR0.CD cleared !", temp_cr0.get32()));
    return 0;
  }
#endif

#if BX_SUPPORT_VMX
  if (BX_CPU_THIS_PTR in_vmx) {
    if (!temp_cr0.get_NE()) {
      BX_ERROR(("check_CR0(0x%08x): attempt to clear CR0.NE in vmx mode !", temp_cr0.get32()));
      return 0;
    }
    if (!BX_CPU_THIS_PTR in_vmx_guest && !SECONDARY_VMEXEC_CONTROL(VMX_VM_EXEC_CTRL3_UNRESTRICTED_GUEST)) {
      if (!temp_cr0.get_PE() || !temp_cr0.get_PG()) {
        BX_ERROR(("check_CR0(0x%08x): attempt to clear CR0.PE/CR0.PG in vmx mode !", temp_cr0.get32()));
        return 0;
      }
    }
  }
#endif

  return 1;
}

bx_bool BX_CPP_AttrRegparmN(1) BX_CPU_C::SetCR0(bx_address val)
{
  if (! check_CR0(val)) return 0;

  Bit32u val_32 = GET32L(val);

#if BX_CPU_LEVEL >= 6
  bx_bool pg = (val_32 >> 31) & 0x1;
#endif

#if BX_SUPPORT_X86_64
  if (! BX_CPU_THIS_PTR cr0.get_PG() && pg) {
    if (BX_CPU_THIS_PTR efer.get_LME()) {
      if (!BX_CPU_THIS_PTR cr4.get_PAE()) {
        BX_ERROR(("SetCR0: attempt to enter x86-64 long mode without enabling CR4.PAE !"));
        return 0;
      }
      if (BX_CPU_THIS_PTR sregs[BX_SEG_REG_CS].cache.u.segment.l) {
        BX_ERROR(("SetCR0: attempt to enter x86-64 long mode with CS.L !"));
        return 0;
      }
      if (BX_CPU_THIS_PTR tr.cache.type <= 3) {
        BX_ERROR(("SetCR0: attempt to enter x86-64 long mode with TSS286 in TR !"));
        return 0;
      }
      BX_CPU_THIS_PTR efer.set_LMA(1);
    }
  }
  else if (BX_CPU_THIS_PTR cr0.get_PG() && ! pg) {
    if (BX_CPU_THIS_PTR cpu_mode == BX_MODE_LONG_64) {
      BX_ERROR(("SetCR0(): attempt to leave 64 bit mode directly to legacy mode !"));
      return 0;
    }
    if (BX_CPU_THIS_PTR efer.get_LMA()) {
      if (BX_CPU_THIS_PTR cr4.get_PCIDE()) {
        BX_ERROR(("SetCR0(): attempt to leave 64 bit mode with CR4.PCIDE set !"));
        return 0;
      }
      if (BX_CPU_THIS_PTR gen_reg[BX_64BIT_REG_RIP].dword.hrx != 0) {
        BX_PANIC(("SetCR0(): attempt to leave x86-64 LONG mode with RIP upper != 0"));
      }
      BX_CPU_THIS_PTR efer.set_LMA(0);
    }
  }
#endif  // #if BX_SUPPORT_X86_64

  // handle reserved bits behaviour
#if BX_CPU_LEVEL == 3
  val_32 = val_32 | 0x7ffffff0;
#elif BX_CPU_LEVEL == 4
  val_32 = (val_32 | 0x00000010) & 0xe005003f;
#elif BX_CPU_LEVEL == 5
  val_32 = val_32 | 0x00000010;
#elif BX_CPU_LEVEL == 6
  val_32 = (val_32 | 0x00000010) & 0xe005003f;
#else
#error "SetCR0: implement reserved bits behaviour for this CPU_LEVEL"
#endif

  Bit32u oldCR0 = BX_CPU_THIS_PTR cr0.get32();

#if BX_SUPPORT_SVM
  if (BX_CPU_THIS_PTR in_svm_guest) {
    if(SVM_CR_WRITE_INTERCEPTED(0)) Svm_Vmexit(SVM_VMEXIT_CR0_WRITE);

    if (SVM_INTERCEPT(0, SVM_INTERCEPT0_CR0_WRITE_NO_TS_MP)) {
      if ((oldCR0 & 0xfffffff5) != (val_32 & 0xfffffff5)) {
        // any other bit except TS or MP had changed
        Svm_Vmexit(SVM_VMEXIT_CR0_SEL_WRITE);
      } 
    }
  }
#endif

#if BX_CPU_LEVEL >= 6
  if (pg && BX_CPU_THIS_PTR cr4.get_PAE() && !long_mode()) {
    if (! CheckPDPTR(BX_CPU_THIS_PTR cr3)) {
      BX_ERROR(("SetCR0(): PDPTR check failed !"));
      return 0;
    }
  }
#endif

  BX_CPU_THIS_PTR cr0.set32(val_32);

#if BX_CPU_LEVEL >= 4 && BX_SUPPORT_ALIGNMENT_CHECK
  handleAlignmentCheck(/* CR0.AC reloaded */);
#endif

  handleCpuModeChange();

#if BX_CPU_LEVEL >= 6
  handleSseModeChange();
#if BX_SUPPORT_AVX
  handleAvxModeChange();
#endif
#endif

  // Modification of PG,PE flushes TLB cache according to docs.
  // Additionally, the TLB strategy is based on the current value of
  // WP, so if that changes we must also flush the TLB.
  if ((oldCR0 & 0x80010001) != (val_32 & 0x80010001))
    TLB_flush(); // Flush Global entries also

  return 1;
}

#if BX_CPU_LEVEL >= 5
Bit32u BX_CPU_C::get_cr4_allow_mask(void)
{
  Bit32u allowMask = 0;

  // CR4 bits definitions:
  //   [31-21] Reserved, Must be Zero
  //   [20]    SMEP: Supervisor Mode Execution Protection R/W
  //   [19]    Reserved, Must be Zero
  //   [18]    OSXSAVE: Operating System XSAVE Support R/W
  //   [17]    PCIDE: PCID Support R/W
  //   [16]    FSGSBASE: FS/GS BASE access R/W
  //   [15]    Reserved, Must be Zero
  //   [14]    SMXE: SMX Extensions R/W
  //   [13]    VMXE: VMX Extensions R/W
  //   [12-11] Reserved, Must be Zero
  //   [10]    OSXMMEXCPT: Operating System Unmasked Exception Support R/W
  //   [9]     OSFXSR: Operating System FXSAVE/FXRSTOR Support R/W
  //   [8]     PCE: Performance-Monitoring Counter Enable R/W
  //   [7]     PGE: Page-Global Enable R/W
  //   [6]     MCE: Machine Check Enable R/W
  //   [5]     PAE: Physical-Address Extension R/W
  //   [4]     PSE: Page Size Extensions R/W
  //   [3]     DE: Debugging Extensions R/W
  //   [2]     TSD: Time Stamp Disable R/W
  //   [1]     PVI: Protected-Mode Virtual Interrupts R/W
  //   [0]     VME: Virtual-8086 Mode Extensions R/W

  /* VME */
  if (bx_cpuid_support_vme())
    allowMask |= BX_CR4_VME_MASK | BX_CR4_PVI_MASK;

  if (bx_cpuid_support_tsc())
    allowMask |= BX_CR4_TSD_MASK;

  if (bx_cpuid_support_debug_extensions())
    allowMask |= BX_CR4_DE_MASK;

  if (bx_cpuid_support_pse())
    allowMask |= BX_CR4_PSE_MASK;

#if BX_CPU_LEVEL >= 6
  if (bx_cpuid_support_pae())
    allowMask |= BX_CR4_PAE_MASK;
#endif

  // NOTE: exception 18 (#MC) never appears in Bochs
  allowMask |= BX_CR4_MCE_MASK;

#if BX_CPU_LEVEL >= 6
  if (bx_cpuid_support_pge())
    allowMask |= BX_CR4_PGE_MASK;

  allowMask |= BX_CR4_PCE_MASK;

  /* OSFXSR */
  if (bx_cpuid_support_fxsave_fxrstor())
    allowMask |= BX_CR4_OSFXSR_MASK;

  /* OSXMMEXCPT */
  if (bx_cpuid_support_sse())
    allowMask |= BX_CR4_OSXMMEXCPT_MASK;

#if BX_SUPPORT_VMX
  if (bx_cpuid_support_vmx())
    allowMask |= BX_CR4_VMXE_MASK;
#endif

  if (bx_cpuid_support_smx())
    allowMask |= BX_CR4_SMXE_MASK;

#if BX_SUPPORT_X86_64
  if (bx_cpuid_support_pcid())
    allowMask |= BX_CR4_PCIDE_MASK;

  if (bx_cpuid_support_fsgsbase())
    allowMask |= BX_CR4_FSGSBASE_MASK;
#endif

  /* OSXSAVE */
  if (bx_cpuid_support_xsave())
    allowMask |= BX_CR4_OSXSAVE_MASK;

  if (bx_cpuid_support_smep())
    allowMask |= BX_CR4_SMEP_MASK;
#endif

  return allowMask;
}

bx_bool BX_CPP_AttrRegparmN(1) BX_CPU_C::check_CR4(bx_address cr4_val)
{
  // check if trying to set undefined bits
  if (cr4_val & ~((bx_address) BX_CPU_THIS_PTR cr4_suppmask)) {
    BX_ERROR(("check_CR4(): write of 0x%08x not supported (allowMask=0x%x)", (Bit32u) cr4_val, BX_CPU_THIS_PTR cr4_suppmask));
    return 0;
  }

  bx_cr4_t temp_cr4;
  temp_cr4.set32((Bit32u) cr4_val);

#if BX_SUPPORT_X86_64
  if (long_mode()) {
    if(! temp_cr4.get_PAE()) {
      BX_ERROR(("check_CR4(): attempt to clear CR4.PAE when EFER.LMA=1"));
      return 0;
    }
  }

  if (temp_cr4.get_PCIDE()) {
    if (! long_mode()) {
      BX_ERROR(("check_CR4(): attempt to set CR4.PCIDE when EFER.LMA=0"));
      return 0;
    }
  }
#endif

#if BX_SUPPORT_VMX
  if(! temp_cr4.get_VMXE()) {
    if (BX_CPU_THIS_PTR in_vmx) {
      BX_ERROR(("check_CR4(): attempt to clear CR4.VMXE in vmx mode"));
      return 0;
    }
  }
  else {
    if (BX_CPU_THIS_PTR in_smm) {
      BX_ERROR(("check_CR4(): attempt to set CR4.VMXE in smm mode"));
      return 0;
    }
  }
#endif

  return 1;
}

bx_bool BX_CPP_AttrRegparmN(1) BX_CPU_C::SetCR4(bx_address val)
{
  if (! check_CR4(val)) return 0;

#if BX_CPU_LEVEL >= 6
  // Modification of PGE,PAE,PSE,PCIDE,SMEP flushes TLB cache according to docs.
  if ((val & BX_CR4_FLUSH_TLB_MASK) != (BX_CPU_THIS_PTR cr4.val32 & BX_CR4_FLUSH_TLB_MASK)) {
    // reload PDPTR if needed
    if (BX_CPU_THIS_PTR cr0.get_PG() && (val & BX_CR4_PAE_MASK) != 0 && !long_mode()) {
      if (! CheckPDPTR(BX_CPU_THIS_PTR cr3)) {
        BX_ERROR(("SetCR4(): PDPTR check failed !"));
        return 0;
      }
    }
#if BX_SUPPORT_X86_64
    else {
      // if trying to enable CR4.PCIDE
      if (! BX_CPU_THIS_PTR cr4.get_PCIDE() && (val & BX_CR4_PCIDE_MASK)) {
        if (BX_CPU_THIS_PTR cr3 & 0xfff) {
          BX_ERROR(("SetCR4(): Attempt to enable CR4.PCIDE with non-zero PCID !"));
          return 0;
        }
      }
    }
#endif
    TLB_flush(); // Flush Global entries also.
  }
#endif

  BX_CPU_THIS_PTR cr4.set32((Bit32u) val);

#if BX_CPU_LEVEL >= 6
  handleSseModeChange();
#if BX_SUPPORT_AVX
  handleAvxModeChange();
#endif
#endif

  return 1;
}
#endif // BX_CPU_LEVEL >= 5

bx_bool BX_CPP_AttrRegparmN(1) BX_CPU_C::SetCR3(bx_address val)
{
#if BX_SUPPORT_X86_64
  if (long_mode()) {
    if (! IsValidPhyAddr(val)) {
      BX_ERROR(("SetCR3(): Attempt to write to reserved bits of CR3 !"));
      return 0;
    }
  }
#endif

  BX_CPU_THIS_PTR cr3 = val;

  // flush TLB even if value does not change
#if BX_CPU_LEVEL >= 6
  if (BX_CPU_THIS_PTR cr4.get_PGE())
    TLB_flushNonGlobal(); // Don't flush Global entries.
  else
#endif
    TLB_flush();          // Flush Global entries also.

  return 1;
}

#if BX_CPU_LEVEL >= 5
bx_bool BX_CPP_AttrRegparmN(1) BX_CPU_C::SetEFER(bx_address val_64)
{
  if (val_64 & ~((Bit64u) BX_CPU_THIS_PTR efer_suppmask)) {
    BX_ERROR(("SetEFER: attempt to set reserved bits of EFER MSR !"));
    return 0;
  }

  Bit32u val32 = (Bit32u) val_64;

#if BX_SUPPORT_X86_64
  /* #GP(0) if changing EFER.LME when cr0.pg = 1 */
  if ((BX_CPU_THIS_PTR efer.get_LME() != ((val32 >> 8) & 1)) &&
       BX_CPU_THIS_PTR  cr0.get_PG())
  {
    BX_ERROR(("SetEFER: attempt to change LME when CR0.PG=1"));
    return 0;
  }
#endif

  BX_CPU_THIS_PTR efer.set32((val32 & BX_CPU_THIS_PTR efer_suppmask & ~BX_EFER_LMA_MASK)
        | (BX_CPU_THIS_PTR efer.get32() & BX_EFER_LMA_MASK)); // keep LMA untouched

  return 1;
}
#endif

#if BX_CPU_LEVEL >= 6
void BX_CPU_C::WriteCR8(bxInstruction_c *i, bx_address val)
{
#if BX_SUPPORT_VMX
  if (BX_CPU_THIS_PTR in_vmx_guest)
    VMexit_CR8_Write(i);
#endif

  // CR8 is aliased to APIC->TASK PRIORITY register
  //   APIC.TPR[7:4] = CR8[3:0]
  //   APIC.TPR[3:0] = 0
  // Reads of CR8 return zero extended APIC.TPR[7:4]
  // Write to CR8 update APIC.TPR[7:4]
#if BX_SUPPORT_APIC
  if (val & BX_CONST64(0xfffffffffffffff0)) {
    BX_ERROR(("WriteCR8: Attempt to set reserved bits of CR8"));
    exception(BX_GP_EXCEPTION, 0);
  }
#if BX_SUPPORT_VMX && BX_SUPPORT_X86_64
  if (BX_CPU_THIS_PTR in_vmx_guest && VMEXIT(VMX_VM_EXEC_CTRL2_TPR_SHADOW)) {
    VMX_Write_VTPR((val & 0xf) << 4);
  }
  else
#endif
  {
    BX_CPU_THIS_PTR lapic.set_tpr((val & 0xf) << 4);
  }
#endif // BX_SUPPORT_APIC
}

Bit32u BX_CPU_C::ReadCR8(bxInstruction_c *i)
{
  Bit32u tpr = 0;

#if BX_SUPPORT_VMX && BX_SUPPORT_X86_64
  if (BX_CPU_THIS_PTR in_vmx_guest)
    VMexit_CR8_Read(i);

  if (BX_CPU_THIS_PTR in_vmx_guest && VMEXIT(VMX_VM_EXEC_CTRL2_TPR_SHADOW)) {
     tpr = (VMX_Read_VTPR() >> 4) & 0xf;
  }
  else
#endif
  {
    // CR8 is aliased to APIC->TASK PRIORITY register
    //   APIC.TPR[7:4] = CR8[3:0]
    //   APIC.TPR[3:0] = 0
    // Reads of CR8 return zero extended APIC.TPR[7:4]
    // Write to CR8 update APIC.TPR[7:4]
#if BX_SUPPORT_APIC
    tpr = (BX_CPU_THIS_PTR lapic.get_tpr() >> 4) & 0xf;
#endif
  }

  return tpr;
}

#endif

BX_INSF_TYPE BX_CPP_AttrRegparmN(1) BX_CPU_C::CLTS(bxInstruction_c *i)
{
  // CPL is always 0 in real mode
  if (/* !real_mode() && */ CPL!=0) {
    BX_ERROR(("CLTS: priveledge check failed, generate #GP(0)"));
    exception(BX_GP_EXCEPTION, 0);
  }

#if BX_SUPPORT_VMX
  if (BX_CPU_THIS_PTR in_vmx_guest) {
    if(VMexit_CLTS(i)) {
      BX_NEXT_TRACE(i);
    }
  }
#endif

  BX_CPU_THIS_PTR cr0.set_TS(0);

#if BX_CPU_LEVEL >= 6
  handleSseModeChange();
#if BX_SUPPORT_AVX
  handleAvxModeChange();
#endif
#endif

  BX_NEXT_TRACE(i);
}

#if BX_X86_DEBUGGER

bx_bool BX_CPU_C::hwbreakpoint_check(bx_address laddr, unsigned opa, unsigned opb)
{
  laddr = LPFOf(laddr);

  Bit32u dr_op[4];

  dr_op[0] = BX_CPU_THIS_PTR dr7.get_R_W0();
  dr_op[1] = BX_CPU_THIS_PTR dr7.get_R_W1();
  dr_op[2] = BX_CPU_THIS_PTR dr7.get_R_W2();
  dr_op[3] = BX_CPU_THIS_PTR dr7.get_R_W3();

  for (int n=0;n<4;n++) {
    if ((dr_op[n] == opa || dr_op[n] == opb) && laddr == LPFOf(BX_CPU_THIS_PTR dr[n])) {
      return 1;
    }
  }

  return 0;
}

Bit32u BX_CPU_C::code_breakpoint_match(bx_address laddr)
{
  if (BX_CPU_THIS_PTR get_RF() || BX_CPU_THIS_PTR in_repeat)
    return 0;

  if (BX_CPU_THIS_PTR dr7.get_bp_enabled()) {
    Bit32u dr6_bits = hwdebug_compare(laddr, 1, BX_HWDebugInstruction, BX_HWDebugInstruction);
    if (dr6_bits) {
      // Add to the list of debug events thus far.
      BX_CPU_THIS_PTR debug_trap |= dr6_bits;
      BX_ERROR(("#DB: x86 code breakpoint catched"));
      return dr6_bits;
    }
  }

  return 0;
}

void BX_CPU_C::hwbreakpoint_match(bx_address laddr, unsigned len, unsigned rw)
{
  if (BX_CPU_THIS_PTR dr7.get_bp_enabled()) {
    // Only compare debug registers if any breakpoints are enabled
    unsigned opa, opb, write = rw & 1;
    opa = BX_HWDebugMemRW; // Read or Write always compares vs 11b
    if (! write) // only compares vs 11b
      opb = opa;
    else // BX_WRITE or BX_RW; also compare vs 01b
      opb = BX_HWDebugMemW;
    Bit32u dr6_bits = hwdebug_compare(laddr, len, opa, opb);
    if (dr6_bits) {
      BX_CPU_THIS_PTR debug_trap |= dr6_bits;
      BX_CPU_THIS_PTR async_event = 1;
    }
  }
}

Bit32u BX_CPU_C::hwdebug_compare(bx_address laddr_0, unsigned size,
                          unsigned opa, unsigned opb)
{
  Bit32u dr7 = BX_CPU_THIS_PTR dr7.get32();

  static bx_address alignment_mask[4] =
    // 00b=1  01b=2  10b=undef(8)  11b=4
    {  0x0,   0x1,   0x7,          0x3   };

  bx_address laddr_n = laddr_0 + (size - 1);
  Bit32u dr_op[4], dr_len[4];
  bx_bool ibpoint_found_n[4], ibpoint_found = 0;

  dr_len[0] = BX_CPU_THIS_PTR dr7.get_LEN0();
  dr_len[1] = BX_CPU_THIS_PTR dr7.get_LEN1();
  dr_len[2] = BX_CPU_THIS_PTR dr7.get_LEN2();
  dr_len[3] = BX_CPU_THIS_PTR dr7.get_LEN3();

  dr_op[0] = BX_CPU_THIS_PTR dr7.get_R_W0();
  dr_op[1] = BX_CPU_THIS_PTR dr7.get_R_W1();
  dr_op[2] = BX_CPU_THIS_PTR dr7.get_R_W2();
  dr_op[3] = BX_CPU_THIS_PTR dr7.get_R_W3();

  for (unsigned n=0;n<4;n++) {
    bx_address dr_start = BX_CPU_THIS_PTR dr[n] & ~alignment_mask[dr_len[n]];
    bx_address dr_end = dr_start + alignment_mask[dr_len[n]];
    ibpoint_found_n[n] = 0;

    // See if this instruction address matches any breakpoints
    if (dr7 & (3 << n*2)) {
      if ((dr_op[n]==opa || dr_op[n]==opb) &&
           (laddr_0 <= dr_end) &&
           (laddr_n >= dr_start)) {
        ibpoint_found_n[n] = 1;
        ibpoint_found = 1;
      }
    }
  }

  // If *any* enabled breakpoints matched, then we need to
  // set status bits for *all* breakpoints, even disabled ones,
  // as long as they meet the other breakpoint criteria.
  // dr6_mask is the return value.  These bits represent the bits
  // to be OR'd into DR6 as a result of the debug event.
  Bit32u dr6_mask = 0;

  if (ibpoint_found) {
    if (ibpoint_found_n[0]) dr6_mask |= 0x1;
    if (ibpoint_found_n[1]) dr6_mask |= 0x2;
    if (ibpoint_found_n[2]) dr6_mask |= 0x4;
    if (ibpoint_found_n[3]) dr6_mask |= 0x8;
  }

  return dr6_mask;
}

#if BX_CPU_LEVEL >= 5
void BX_CPU_C::iobreakpoint_match(unsigned port, unsigned len)
{
  // Only compare debug registers if any breakpoints are enabled
  if (BX_CPU_THIS_PTR cr4.get_DE() && BX_CPU_THIS_PTR dr7.get_bp_enabled())
  {
    Bit32u dr6_bits = hwdebug_compare(port, len, BX_HWDebugIO, BX_HWDebugIO);
    if (dr6_bits) {
      BX_CPU_THIS_PTR debug_trap |= dr6_bits;
      BX_CPU_THIS_PTR async_event = 1;
    }
  }
}
#endif

#endif
