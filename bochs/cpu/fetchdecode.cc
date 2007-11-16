/////////////////////////////////////////////////////////////////////////
// $Id: fetchdecode.cc,v 1.125 2007-11-16 21:43:23 sshwarts Exp $
/////////////////////////////////////////////////////////////////////////
//
//  Copyright (C) 2001  MandrakeSoft S.A.
//
//    MandrakeSoft S.A.
//    43, rue d'Aboukir
//    75002 Paris - France
//    http://www.linux-mandrake.com/
//    http://www.mandrakesoft.com/
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
//  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA

#define NEED_CPU_REG_SHORTCUTS 1
#include "bochs.h"
#include "cpu.h"
#define LOG_THIS BX_CPU_THIS_PTR

///////////////////////////
// prefix bytes
// opcode bytes
// modrm/sib
// address displacement
// immediate constant
///////////////////////////

static const bx_bool BxOpcodeHasModrm32[512] = {
  /*       0 1 2 3 4 5 6 7 8 9 a b c d e f          */
  /*       -------------------------------          */
  /* 00 */ 1,1,1,1,0,0,0,0,1,1,1,1,0,0,0,0, 
  /* 10 */ 1,1,1,1,0,0,0,0,1,1,1,1,0,0,0,0, 
  /* 20 */ 1,1,1,1,0,0,0,0,1,1,1,1,0,0,0,0, 
  /* 30 */ 1,1,1,1,0,0,0,0,1,1,1,1,0,0,0,0, 
  /* 40 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 
  /* 50 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 
  /* 60 */ 0,0,1,1,0,0,0,0,0,1,0,1,0,0,0,0, 
  /* 70 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 
  /* 80 */ 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 
  /* 90 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 
  /* A0 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 
  /* B0 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 
  /* C0 */ 1,1,0,0,1,1,1,1,0,0,0,0,0,0,0,0, 
  /* D0 */ 1,1,1,1,0,0,0,0,1,1,1,1,1,1,1,1, 
  /* E0 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 
  /* F0 */ 0,0,0,0,0,0,1,1,0,0,0,0,0,0,1,1,
  /*       0 1 2 3 4 5 6 7 8 9 a b c d e f           */
  /*       -------------------------------           */
           1,1,1,1,0,0,0,0,0,0,0,0,0,1,0,1, /* 0F 00 */
           1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,1, /* 0F 10 */
           1,1,1,1,1,0,1,0,1,1,1,1,1,1,1,1, /* 0F 20 */
           0,0,0,0,0,0,0,0,1,0,1,0,0,0,0,0, /* 0F 30 */
           1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, /* 0F 40 */
           1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, /* 0F 50 */
           1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, /* 0F 60 */
           1,1,1,1,1,1,1,0,0,0,0,0,1,1,1,1, /* 0F 70 */
           0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 0F 80 */
           1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, /* 0F 90 */
           0,0,0,1,1,1,0,0,0,0,0,1,1,1,1,1, /* 0F A0 */
           1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, /* 0F B0 */
           1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0, /* 0F C0 */
           1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, /* 0F D0 */
           1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, /* 0F E0 */
           1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0  /* 0F F0 */
  /*       -------------------------------           */
  /*       0 1 2 3 4 5 6 7 8 9 a b c d e f           */
};

// UD2 opcode (according to Intel manuals):
// Use the 0F0B opcode (UD2 instruction) or the 0FB9H opcode when deliberately 
// trying to generate an invalid opcode exception (#UD).

/* *********** */
// LOCK PREFIX //
/* *********** */

/* 
 *  The  LOCK  prefix  can be prepended only to the following instructions
 *  and  only  to  those  forms  of the instructions where the destination
 *  operand  is  a  memory operand: ADD, ADC, AND, BTC, BTR, BTS, CMPXCHG,
 *  CMPXCH8B,  DEC,  INC,  NEG, NOT, OR, SBB, SUB, XOR, XADD, and XCHG. If
 *  the  LOCK prefix is used with one of these instructions and the source
 *  operand  is a memory operand, an undefined opcode exception (#UD) will
 *  be  generated. An undefined opcode exception will also be generated if
 *  the  LOCK  prefix  is used with any instruction not in the above list.
 *  The XCHG instruction always asserts the LOCK# signal regardless of the
 *  presence or absence of the LOCK prefix.
 */

static BxExecutePtr_tR Resolve16Mod0[8] = {
  &BX_CPU_C::BxResolve16Mod0Rm0,
  &BX_CPU_C::BxResolve16Mod0Rm1,
  &BX_CPU_C::BxResolve16Mod0Rm2,
  &BX_CPU_C::BxResolve16Mod0Rm3,
  &BX_CPU_C::BxResolve16Mod0Rm4,
  &BX_CPU_C::BxResolve16Mod0Rm5,
  &BX_CPU_C::BxResolve16Mod0Rm6,
  &BX_CPU_C::BxResolve16Mod0Rm7
};

static BxExecutePtr_tR Resolve16Mod1or2[8] = {
  &BX_CPU_C::BxResolve16Mod1or2Rm0,
  &BX_CPU_C::BxResolve16Mod1or2Rm1,
  &BX_CPU_C::BxResolve16Mod1or2Rm2,
  &BX_CPU_C::BxResolve16Mod1or2Rm3,
  &BX_CPU_C::BxResolve16Mod1or2Rm4,
  &BX_CPU_C::BxResolve16Mod1or2Rm5,
  &BX_CPU_C::BxResolve16Mod1or2Rm6,
  &BX_CPU_C::BxResolve16Mod1or2Rm7
};

static BxExecutePtr_tR Resolve32Mod0[8] = {
  &BX_CPU_C::BxResolve32Mod0Rm,
  &BX_CPU_C::BxResolve32Mod0Rm,
  &BX_CPU_C::BxResolve32Mod0Rm,
  &BX_CPU_C::BxResolve32Mod0Rm,
  NULL, // escape to SIB-byte
  &BX_CPU_C::BxResolve32Mod0Disp,
  &BX_CPU_C::BxResolve32Mod0Rm,
  &BX_CPU_C::BxResolve32Mod0Rm
};

static BxExecutePtr_tR Resolve32Mod0Base[8] = {
  &BX_CPU_C::BxResolve32Mod0Base,
  &BX_CPU_C::BxResolve32Mod0Base,
  &BX_CPU_C::BxResolve32Mod0Base,
  &BX_CPU_C::BxResolve32Mod0Base,
  &BX_CPU_C::BxResolve32Mod0Base,
  &BX_CPU_C::BxResolve32Mod0Disp,
  &BX_CPU_C::BxResolve32Mod0Base,
  &BX_CPU_C::BxResolve32Mod0Base,
};

static BxExecutePtr_tR Resolve32Mod0BaseIndex[8] = {
  &BX_CPU_C::BxResolve32Mod0BaseIndex,
  &BX_CPU_C::BxResolve32Mod0BaseIndex,
  &BX_CPU_C::BxResolve32Mod0BaseIndex,
  &BX_CPU_C::BxResolve32Mod0BaseIndex,
  &BX_CPU_C::BxResolve32Mod0BaseIndex,
  &BX_CPU_C::BxResolve32Mod0DispIndex,
  &BX_CPU_C::BxResolve32Mod0BaseIndex,
  &BX_CPU_C::BxResolve32Mod0BaseIndex,
};

// common fetchdecode32/64 opcode tables
#include "fetchdecode.h"

/* ************************** */
/* 512 entries for 16bit mode */
/* 512 entries for 32bit mode */
/* ************************** */

static const BxOpcodeInfo_t BxOpcodeInfo[512*2] = {
  // 512 entries for 16bit mode
  /* 00 /w */ { BxLockable, &BX_CPU_C::ADD_EbGb },
  /* 01 /w */ { BxLockable, &BX_CPU_C::ADD_EwGw },
  /* 02 /w */ { 0, &BX_CPU_C::ADD_GbEb },
  /* 03 /w */ { BxSplitMod11b, NULL, opcodesADD_GwEw },
  /* 04 /w */ { BxImmediate_Ib, &BX_CPU_C::ADD_ALIb },
  /* 05 /w */ { BxImmediate_Iv, &BX_CPU_C::ADD_AXIw },
  /* 06 /w */ { 0, &BX_CPU_C::PUSH16_ES },
  /* 07 /w */ { 0, &BX_CPU_C::POP16_ES },
  /* 08 /w */ { BxLockable, &BX_CPU_C::OR_EbGb },
  /* 09 /w */ { BxLockable, &BX_CPU_C::OR_EwGw },
  /* 0A /w */ { 0, &BX_CPU_C::OR_GbEb },
  /* 0B /w */ { 0, &BX_CPU_C::OR_GwEw },
  /* 0C /w */ { BxImmediate_Ib, &BX_CPU_C::OR_ALIb },
  /* 0D /w */ { BxImmediate_Iv, &BX_CPU_C::OR_AXIw },
  /* 0E /w */ { 0, &BX_CPU_C::PUSH16_CS },
  /* 0F /w */ { 0, &BX_CPU_C::BxError }, // 2-byte escape
  /* 10 /w */ { BxLockable, &BX_CPU_C::ADC_EbGb },
  /* 11 /w */ { BxLockable, &BX_CPU_C::ADC_EwGw },
  /* 12 /w */ { 0, &BX_CPU_C::ADC_GbEb },
  /* 13 /w */ { 0, &BX_CPU_C::ADC_GwEw },
  /* 14 /w */ { BxImmediate_Ib, &BX_CPU_C::ADC_ALIb },
  /* 15 /w */ { BxImmediate_Iv, &BX_CPU_C::ADC_AXIw },
  /* 16 /w */ { 0, &BX_CPU_C::PUSH16_SS },
  /* 17 /w */ { 0, &BX_CPU_C::POP16_SS },
  /* 18 /w */ { BxLockable, &BX_CPU_C::SBB_EbGb },
  /* 19 /w */ { BxLockable, &BX_CPU_C::SBB_EwGw },
  /* 1A /w */ { 0, &BX_CPU_C::SBB_GbEb },
  /* 1B /w */ { 0, &BX_CPU_C::SBB_GwEw },
  /* 1C /w */ { BxImmediate_Ib, &BX_CPU_C::SBB_ALIb },
  /* 1D /w */ { BxImmediate_Iv, &BX_CPU_C::SBB_AXIw },
  /* 1E /w */ { 0, &BX_CPU_C::PUSH16_DS },
  /* 1F /w */ { 0, &BX_CPU_C::POP16_DS },
  /* 20 /w */ { BxLockable, &BX_CPU_C::AND_EbGb },
  /* 21 /w */ { BxLockable, &BX_CPU_C::AND_EwGw },
  /* 22 /w */ { 0, &BX_CPU_C::AND_GbEb },
  /* 23 /w */ { 0, &BX_CPU_C::AND_GwEw },
  /* 24 /w */ { BxImmediate_Ib, &BX_CPU_C::AND_ALIb },
  /* 25 /w */ { BxImmediate_Iv, &BX_CPU_C::AND_AXIw },
  /* 26 /w */ { BxPrefix, &BX_CPU_C::BxError }, // ES:
  /* 27 /w */ { 0, &BX_CPU_C::DAA },
  /* 28 /w */ { BxLockable, &BX_CPU_C::SUB_EbGb },
  /* 29 /w */ { BxLockable, &BX_CPU_C::SUB_EwGw },
  /* 2A /w */ { 0, &BX_CPU_C::SUB_GbEb },
  /* 2B /w */ { 0, &BX_CPU_C::SUB_GwEw },
  /* 2C /w */ { BxImmediate_Ib, &BX_CPU_C::SUB_ALIb },
  /* 2D /w */ { BxImmediate_Iv, &BX_CPU_C::SUB_AXIw },
  /* 2E /w */ { BxPrefix, &BX_CPU_C::BxError }, // CS:
  /* 2F /w */ { 0, &BX_CPU_C::DAS },
  /* 30 /w */ { BxLockable, &BX_CPU_C::XOR_EbGb },
  /* 31 /w */ { BxLockable, &BX_CPU_C::XOR_EwGw },
  /* 32 /w */ { 0, &BX_CPU_C::XOR_GbEb },
  /* 33 /w */ { 0, &BX_CPU_C::XOR_GwEw },
  /* 34 /w */ { BxImmediate_Ib, &BX_CPU_C::XOR_ALIb },
  /* 35 /w */ { BxImmediate_Iv, &BX_CPU_C::XOR_AXIw },
  /* 36 /w */ { BxPrefix, &BX_CPU_C::BxError }, // SS:
  /* 37 /w */ { 0, &BX_CPU_C::AAA },
  /* 38 /w */ { 0, &BX_CPU_C::CMP_EbGb },
  /* 39 /w */ { 0, &BX_CPU_C::CMP_EwGw },
  /* 3A /w */ { 0, &BX_CPU_C::CMP_GbEb },
  /* 3B /w */ { 0, &BX_CPU_C::CMP_GwEw },
  /* 3C /w */ { BxImmediate_Ib, &BX_CPU_C::CMP_ALIb },
  /* 3D /w */ { BxImmediate_Iv, &BX_CPU_C::CMP_AXIw },
  /* 3E /w */ { BxPrefix, &BX_CPU_C::BxError }, // DS:
  /* 3F /w */ { 0, &BX_CPU_C::AAS },
  /* 40 /w */ { 0, &BX_CPU_C::INC_RX },
  /* 41 /w */ { 0, &BX_CPU_C::INC_RX },
  /* 42 /w */ { 0, &BX_CPU_C::INC_RX },
  /* 43 /w */ { 0, &BX_CPU_C::INC_RX },
  /* 44 /w */ { 0, &BX_CPU_C::INC_RX },
  /* 45 /w */ { 0, &BX_CPU_C::INC_RX },
  /* 46 /w */ { 0, &BX_CPU_C::INC_RX },
  /* 47 /w */ { 0, &BX_CPU_C::INC_RX },
  /* 48 /w */ { 0, &BX_CPU_C::DEC_RX },
  /* 49 /w */ { 0, &BX_CPU_C::DEC_RX },
  /* 4A /w */ { 0, &BX_CPU_C::DEC_RX },
  /* 4B /w */ { 0, &BX_CPU_C::DEC_RX },
  /* 4C /w */ { 0, &BX_CPU_C::DEC_RX },
  /* 4D /w */ { 0, &BX_CPU_C::DEC_RX },
  /* 4E /w */ { 0, &BX_CPU_C::DEC_RX },
  /* 4F /w */ { 0, &BX_CPU_C::DEC_RX },
  /* 50 /w */ { 0, &BX_CPU_C::PUSH_RX },
  /* 51 /w */ { 0, &BX_CPU_C::PUSH_RX },
  /* 52 /w */ { 0, &BX_CPU_C::PUSH_RX },
  /* 53 /w */ { 0, &BX_CPU_C::PUSH_RX },
  /* 54 /w */ { 0, &BX_CPU_C::PUSH_RX },
  /* 55 /w */ { 0, &BX_CPU_C::PUSH_RX },
  /* 56 /w */ { 0, &BX_CPU_C::PUSH_RX },
  /* 57 /w */ { 0, &BX_CPU_C::PUSH_RX },
  /* 58 /w */ { 0, &BX_CPU_C::POP_RX },
  /* 59 /w */ { 0, &BX_CPU_C::POP_RX },
  /* 5A /w */ { 0, &BX_CPU_C::POP_RX },
  /* 5B /w */ { 0, &BX_CPU_C::POP_RX },
  /* 5C /w */ { 0, &BX_CPU_C::POP_RX },
  /* 5D /w */ { 0, &BX_CPU_C::POP_RX },
  /* 5E /w */ { 0, &BX_CPU_C::POP_RX },
  /* 5F /w */ { 0, &BX_CPU_C::POP_RX },
  /* 60 /w */ { 0, &BX_CPU_C::PUSHAD16 },
  /* 61 /w */ { 0, &BX_CPU_C::POPAD16 },
  /* 62 /w */ { 0, &BX_CPU_C::BOUND_GwMa },
  /* 63 /w */ { 0, &BX_CPU_C::ARPL_EwGw },
  /* 64 /w */ { BxPrefix, &BX_CPU_C::BxError }, // FS:
  /* 65 /w */ { BxPrefix, &BX_CPU_C::BxError }, // GS:
  /* 66 /w */ { BxPrefix, &BX_CPU_C::BxError }, // OS:
  /* 67 /w */ { BxPrefix, &BX_CPU_C::BxError }, // AS:
  /* 68 /w */ { BxImmediate_Iv, &BX_CPU_C::PUSH_Iw },
  /* 69 /w */ { BxImmediate_Iv, &BX_CPU_C::IMUL_GwEwIw },
  /* 6A /w */ { BxImmediate_Ib_SE, &BX_CPU_C::PUSH_Iw },
  /* 6B /w */ { BxImmediate_Ib_SE, &BX_CPU_C::IMUL_GwEwIw },
  /* 6C /w */ { 0, &BX_CPU_C::REP_INSB_YbDX },
  /* 6D /w */ { 0, &BX_CPU_C::REP_INSW_YwDX },
  /* 6E /w */ { 0, &BX_CPU_C::REP_OUTSB_DXXb },
  /* 6F /w */ { 0, &BX_CPU_C::REP_OUTSW_DXXw },
  /* 70 /w */ { BxImmediate_BrOff8, &BX_CPU_C::JO_Jw },
  /* 71 /w */ { BxImmediate_BrOff8, &BX_CPU_C::JNO_Jw },
  /* 72 /w */ { BxImmediate_BrOff8, &BX_CPU_C::JB_Jw },
  /* 73 /w */ { BxImmediate_BrOff8, &BX_CPU_C::JNB_Jw },
  /* 74 /w */ { BxImmediate_BrOff8, &BX_CPU_C::JZ_Jw },
  /* 75 /w */ { BxImmediate_BrOff8, &BX_CPU_C::JNZ_Jw },
  /* 76 /w */ { BxImmediate_BrOff8, &BX_CPU_C::JBE_Jw },
  /* 77 /w */ { BxImmediate_BrOff8, &BX_CPU_C::JNBE_Jw },
  /* 78 /w */ { BxImmediate_BrOff8, &BX_CPU_C::JS_Jw },
  /* 79 /w */ { BxImmediate_BrOff8, &BX_CPU_C::JNS_Jw },
  /* 7A /w */ { BxImmediate_BrOff8, &BX_CPU_C::JP_Jw },
  /* 7B /w */ { BxImmediate_BrOff8, &BX_CPU_C::JNP_Jw },
  /* 7C /w */ { BxImmediate_BrOff8, &BX_CPU_C::JL_Jw },
  /* 7D /w */ { BxImmediate_BrOff8, &BX_CPU_C::JNL_Jw },
  /* 7E /w */ { BxImmediate_BrOff8, &BX_CPU_C::JLE_Jw },
  /* 7F /w */ { BxImmediate_BrOff8, &BX_CPU_C::JNLE_Jw },
  /* 80 /w */ { BxGroup1 | BxImmediate_Ib, NULL, BxOpcodeInfoG1EbIb },
  /* 81 /w */ { BxSplitMod11b | BxImmediate_Iv,    NULL, BxOpcodeInfoModG1Ew },
  /* 82 /w */ { BxGroup1 | BxImmediate_Ib, NULL, BxOpcodeInfoG1EbIb },
  /* 83 /w */ { BxSplitMod11b | BxImmediate_Ib_SE, NULL, BxOpcodeInfoModG1Ew },
  /* 84 /w */ { 0, &BX_CPU_C::TEST_EbGb },
  /* 85 /w */ { 0, &BX_CPU_C::TEST_EwGw },
  /* 86 /w */ { BxLockable, &BX_CPU_C::XCHG_EbGb },
  /* 87 /w */ { BxLockable, &BX_CPU_C::XCHG_EwGw },
  /* 88 /w */ { BxSplitMod11b, NULL, opcodesMOV_EbGb },
  /* 89 /w */ { BxSplitMod11b, NULL, opcodesMOV_EwGw },
  /* 8A /w */ { BxSplitMod11b, NULL, opcodesMOV_GbEb },
  /* 8B /w */ { BxSplitMod11b, NULL, opcodesMOV_GwEw },
  /* 8C /w */ { 0, &BX_CPU_C::MOV_EwSw },
  /* 8D /w */ { 0, &BX_CPU_C::LEA_GwM },
  /* 8E /w */ { 0, &BX_CPU_C::MOV_SwEw },
  /* 8F /w */ { 0, &BX_CPU_C::POP_Ew },
  /* 90 /w */ { 0, &BX_CPU_C::NOP },
  /* 91 /w */ { 0, &BX_CPU_C::XCHG_RXAX },
  /* 92 /w */ { 0, &BX_CPU_C::XCHG_RXAX },
  /* 93 /w */ { 0, &BX_CPU_C::XCHG_RXAX },
  /* 94 /w */ { 0, &BX_CPU_C::XCHG_RXAX },
  /* 95 /w */ { 0, &BX_CPU_C::XCHG_RXAX },
  /* 96 /w */ { 0, &BX_CPU_C::XCHG_RXAX },
  /* 97 /w */ { 0, &BX_CPU_C::XCHG_RXAX },
  /* 98 /w */ { 0, &BX_CPU_C::CBW },
  /* 99 /w */ { 0, &BX_CPU_C::CWD },
  /* 9A /w */ { BxImmediate_IvIw, &BX_CPU_C::CALL16_Ap },
  /* 9B /w */ { 0, &BX_CPU_C::FWAIT },
  /* 9C /w */ { 0, &BX_CPU_C::PUSHF_Fw },
  /* 9D /w */ { 0, &BX_CPU_C::POPF_Fw },
  /* 9E /w */ { 0, &BX_CPU_C::SAHF },
  /* 9F /w */ { 0, &BX_CPU_C::LAHF },
  /* A0 /w */ { BxImmediate_O, &BX_CPU_C::MOV_ALOd },
  /* A1 /w */ { BxImmediate_O, &BX_CPU_C::MOV_AXOd },
  /* A2 /w */ { BxImmediate_O, &BX_CPU_C::MOV_OdAL },
  /* A3 /w */ { BxImmediate_O, &BX_CPU_C::MOV_OdAX },
  /* A4 /w */ { 0, &BX_CPU_C::REP_MOVSB_XbYb },
  /* A5 /w */ { 0, &BX_CPU_C::REP_MOVSW_XwYw },
  /* A6 /w */ { 0, &BX_CPU_C::REP_CMPSB_XbYb },
  /* A7 /w */ { 0, &BX_CPU_C::REP_CMPSW_XwYw },
  /* A8 /w */ { BxImmediate_Ib, &BX_CPU_C::TEST_ALIb },
  /* A9 /w */ { BxImmediate_Iv, &BX_CPU_C::TEST_AXIw },
  /* AA /w */ { 0, &BX_CPU_C::REP_STOSB_YbAL },
  /* AB /w */ { 0, &BX_CPU_C::REP_STOSW_YwAX },
  /* AC /w */ { 0, &BX_CPU_C::REP_LODSB_ALXb },
  /* AD /w */ { 0, &BX_CPU_C::REP_LODSW_AXXw },
  /* AE /w */ { 0, &BX_CPU_C::REP_SCASB_ALXb },
  /* AF /w */ { 0, &BX_CPU_C::REP_SCASW_AXXw },
  /* B0 /w */ { BxImmediate_Ib, &BX_CPU_C::MOV_RLIb },
  /* B1 /w */ { BxImmediate_Ib, &BX_CPU_C::MOV_RLIb },
  /* B2 /w */ { BxImmediate_Ib, &BX_CPU_C::MOV_RLIb },
  /* B3 /w */ { BxImmediate_Ib, &BX_CPU_C::MOV_RLIb },
  /* B4 /w */ { BxImmediate_Ib, &BX_CPU_C::MOV_RHIb },
  /* B5 /w */ { BxImmediate_Ib, &BX_CPU_C::MOV_RHIb },
  /* B6 /w */ { BxImmediate_Ib, &BX_CPU_C::MOV_RHIb },
  /* B7 /w */ { BxImmediate_Ib, &BX_CPU_C::MOV_RHIb },
  /* B8 /w */ { BxImmediate_Iv, &BX_CPU_C::MOV_RXIw },
  /* B9 /w */ { BxImmediate_Iv, &BX_CPU_C::MOV_RXIw },
  /* BA /w */ { BxImmediate_Iv, &BX_CPU_C::MOV_RXIw },
  /* BB /w */ { BxImmediate_Iv, &BX_CPU_C::MOV_RXIw },
  /* BC /w */ { BxImmediate_Iv, &BX_CPU_C::MOV_RXIw },
  /* BD /w */ { BxImmediate_Iv, &BX_CPU_C::MOV_RXIw },
  /* BE /w */ { BxImmediate_Iv, &BX_CPU_C::MOV_RXIw },
  /* BF /w */ { BxImmediate_Iv, &BX_CPU_C::MOV_RXIw },
  /* C0 /w */ { BxGroup2 | BxImmediate_Ib, NULL, BxOpcodeInfoG2Eb },
  /* C1 /w */ { BxGroup2 | BxImmediate_Ib, NULL, BxOpcodeInfoG2Ew },
  /* C2 /w */ { BxImmediate_Iw, &BX_CPU_C::RETnear16_Iw },
  /* C3 /w */ { 0,              &BX_CPU_C::RETnear16 },
  /* C4 /w */ { 0, &BX_CPU_C::LES_GwMp },
  /* C5 /w */ { 0, &BX_CPU_C::LDS_GwMp },
  /* C6 /w */ { BxImmediate_Ib, &BX_CPU_C::MOV_EbIb },
  /* C7 /w */ { BxImmediate_Iv, &BX_CPU_C::MOV_EwIw },
  /* C8 /w */ { BxImmediate_IwIb, &BX_CPU_C::ENTER_IwIb },
  /* C9 /w */ { 0, &BX_CPU_C::LEAVE },
  /* CA /w */ { BxImmediate_Iw, &BX_CPU_C::RETfar16_Iw },
  /* CB /w */ { 0, &BX_CPU_C::RETfar16 },
  /* CC /w */ { 0, &BX_CPU_C::INT3 },
  /* CD /w */ { BxImmediate_Ib, &BX_CPU_C::INT_Ib },
  /* CE /w */ { 0, &BX_CPU_C::INTO },
  /* CF /w */ { 0, &BX_CPU_C::IRET16 },
  /* D0 /w */ { BxGroup2, NULL, BxOpcodeInfoG2Eb },
  /* D1 /w */ { BxGroup2, NULL, BxOpcodeInfoG2Ew },
  /* D2 /w */ { BxGroup2, NULL, BxOpcodeInfoG2Eb },
  /* D3 /w */ { BxGroup2, NULL, BxOpcodeInfoG2Ew },
  /* D4 /w */ { BxImmediate_Ib, &BX_CPU_C::AAM },
  /* D5 /w */ { BxImmediate_Ib, &BX_CPU_C::AAD },
  /* D6 /w */ { 0, &BX_CPU_C::SALC },
  /* D7 /w */ { 0, &BX_CPU_C::XLAT },
#if BX_SUPPORT_FPU
  //    by default we have here pointer to the group .. as if mod <> 11b
  /* D8 /w */ { BxFPGroup, NULL, BxOpcodeInfo_FPGroupD8 },
  /* D9 /w */ { BxFPGroup, NULL, BxOpcodeInfo_FPGroupD9 },
  /* DA /w */ { BxFPGroup, NULL, BxOpcodeInfo_FPGroupDA },
  /* DB /w */ { BxFPGroup, NULL, BxOpcodeInfo_FPGroupDB },
  /* DC /w */ { BxFPGroup, NULL, BxOpcodeInfo_FPGroupDC },
  /* DD /w */ { BxFPGroup, NULL, BxOpcodeInfo_FPGroupDD },
  /* DE /w */ { BxFPGroup, NULL, BxOpcodeInfo_FPGroupDE },
  /* DF /w */ { BxFPGroup, NULL, BxOpcodeInfo_FPGroupDF },
#else
  /* D8 /w */ { 0, &BX_CPU_C::FPU_ESC },
  /* D9 /w */ { 0, &BX_CPU_C::FPU_ESC },
  /* DA /w */ { 0, &BX_CPU_C::FPU_ESC },
  /* DB /w */ { 0, &BX_CPU_C::FPU_ESC },
  /* DC /w */ { 0, &BX_CPU_C::FPU_ESC },
  /* DD /w */ { 0, &BX_CPU_C::FPU_ESC },
  /* DE /w */ { 0, &BX_CPU_C::FPU_ESC },
  /* DF /w */ { 0, &BX_CPU_C::FPU_ESC },
#endif
  /* E0 /w */ { BxImmediate_BrOff8, &BX_CPU_C::LOOPNE_Jb },
  /* E1 /w */ { BxImmediate_BrOff8, &BX_CPU_C::LOOPE_Jb },
  /* E2 /w */ { BxImmediate_BrOff8, &BX_CPU_C::LOOP_Jb },
  /* E3 /w */ { BxImmediate_BrOff8, &BX_CPU_C::JCXZ_Jb },
  /* E4 /w */ { BxImmediate_Ib, &BX_CPU_C::IN_ALIb },
  /* E5 /w */ { BxImmediate_Ib, &BX_CPU_C::IN_AXIb },
  /* E6 /w */ { BxImmediate_Ib, &BX_CPU_C::OUT_IbAL },
  /* E7 /w */ { BxImmediate_Ib, &BX_CPU_C::OUT_IbAX },
  /* E8 /w */ { BxImmediate_BrOff16, &BX_CPU_C::CALL_Jw },
  /* E9 /w */ { BxImmediate_BrOff16, &BX_CPU_C::JMP_Jw },
  /* EA /w */ { BxImmediate_IvIw, &BX_CPU_C::JMP_Ap },
  /* EB /w */ { BxImmediate_BrOff8, &BX_CPU_C::JMP_Jw },
  /* EC /w */ { 0, &BX_CPU_C::IN_ALDX },
  /* ED /w */ { 0, &BX_CPU_C::IN_AXDX },
  /* EE /w */ { 0, &BX_CPU_C::OUT_DXAL },
  /* EF /w */ { 0, &BX_CPU_C::OUT_DXAX },
  /* F0 /w */ { BxPrefix, &BX_CPU_C::BxError }, // LOCK
  /* F1 /w */ { 0, &BX_CPU_C::INT1 },
  /* F2 /w */ { BxPrefix, &BX_CPU_C::BxError }, // REPNE/REPNZ
  /* F3 /w */ { BxPrefix, &BX_CPU_C::BxError }, // REP, REPE/REPZ
  /* F4 /w */ { 0, &BX_CPU_C::HLT },
  /* F5 /w */ { 0, &BX_CPU_C::CMC },
  /* F6 /w */ { BxGroup3, NULL, BxOpcodeInfoG3Eb },
  /* F7 /w */ { BxGroup3, NULL, BxOpcodeInfoG3Ew },
  /* F8 /w */ { 0, &BX_CPU_C::CLC },
  /* F9 /w */ { 0, &BX_CPU_C::STC },
  /* FA /w */ { 0, &BX_CPU_C::CLI },
  /* FB /w */ { 0, &BX_CPU_C::STI },
  /* FC /w */ { 0, &BX_CPU_C::CLD },
  /* FD /w */ { 0, &BX_CPU_C::STD },
  /* FE /w */ { BxSplitMod11b, NULL, BxOpcodeInfoModG4 },
  /* FF /w */ { BxGroup5, NULL, BxOpcodeInfoG5w },

  /* 0F 00 /w */ { BxGroup6, NULL, BxOpcodeInfoG6 },
  /* 0F 01 /w */ { BxSplitMod11b, NULL, BxOpcodeInfoModG7 },
  /* 0F 02 /w */ { 0, &BX_CPU_C::LAR_GvEw },
  /* 0F 03 /w */ { 0, &BX_CPU_C::LSL_GvEw },
  /* 0F 04 /w */ { 0, &BX_CPU_C::BxError },
#if BX_SUPPORT_X86_64
  /* 0F 05 /w */ { 0, &BX_CPU_C::SYSCALL },
#elif BX_CPU_LEVEL == 2
  /* 0F 05 /w */ { 0, &BX_CPU_C::LOADALL },
#else
  /* 0F 05 /w */ { 0, &BX_CPU_C::BxError },
#endif
  /* 0F 06 /w */ { 0, &BX_CPU_C::CLTS },
#if BX_SUPPORT_X86_64
  /* 0F 07 /w */ { 0, &BX_CPU_C::SYSRET },
#else
  /* 0F 07 /w */ { 0, &BX_CPU_C::BxError },
#endif
  /* 0F 08 /w */ { 0, &BX_CPU_C::INVD },
  /* 0F 09 /w */ { 0, &BX_CPU_C::WBINVD },
  /* 0F 0A /w */ { 0, &BX_CPU_C::BxError },
  /* 0F 0B /w */ { 0, &BX_CPU_C::UndefinedOpcode }, // UD2 opcode
  /* 0F 0C /w */ { 0, &BX_CPU_C::BxError },
#if BX_SUPPORT_X86_64 || BX_SUPPORT_3DNOW
  /* 0F 0D /w */ { 0, &BX_CPU_C::NOP },     // 3DNow! PREFETCH on AMD, NOP on Intel
#else
  /* 0F 0D /w */ { 0, &BX_CPU_C::BxError },
#endif
#if BX_SUPPORT_3DNOW
  /* 0F 0E /w */ { 0, &BX_CPU_C::EMMS },            // 3DNow! FEMMS
  /* 0F 0F /w */ { BxImmediate_Ib, NULL, Bx3DNowOpcodeInfo },
#else
  /* 0F 0E /w */ { 0, &BX_CPU_C::BxError },
  /* 0F 0F /w */ { 0, &BX_CPU_C::BxError },
#endif
  /* 0F 10 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f10 },
  /* 0F 11 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f11 },
  /* 0F 12 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f12 },
  /* 0F 13 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f13 },
  /* 0F 14 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f14 },
  /* 0F 15 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f15 },
  /* 0F 16 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f16 },
  /* 0F 17 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f17 },
  /* 0F 18 /w */ { 0, &BX_CPU_C::PREFETCH },  // opcode group G16, PREFETCH hints
  /* 0F 19 /w */ { 0, &BX_CPU_C::BxError },
  /* 0F 1A /w */ { 0, &BX_CPU_C::BxError },
  /* 0F 1B /w */ { 0, &BX_CPU_C::BxError },
  /* 0F 1C /w */ { 0, &BX_CPU_C::BxError },
  /* 0F 1D /w */ { 0, &BX_CPU_C::BxError },
  /* 0F 1E /w */ { 0, &BX_CPU_C::BxError },
#if BX_CPU_LEVEL < 6
  /* 0F 1F /w */ { 0, &BX_CPU_C::BxError },
#else
  /* 0F 1F /w */ { 0, &BX_CPU_C::NOP },      // multi-byte NOP
#endif
  /* 0F 20 /w */ { 0, &BX_CPU_C::MOV_RdCd },
  /* 0F 21 /w */ { 0, &BX_CPU_C::MOV_RdDd },
  /* 0F 22 /w */ { 0, &BX_CPU_C::MOV_CdRd },
  /* 0F 23 /w */ { 0, &BX_CPU_C::MOV_DdRd },
  /* 0F 24 /w */ { 0, &BX_CPU_C::MOV_RdTd },
  /* 0F 25 /w */ { 0, &BX_CPU_C::BxError },
  /* 0F 26 /w */ { 0, &BX_CPU_C::MOV_TdRd },
  /* 0F 27 /w */ { 0, &BX_CPU_C::BxError },
  /* 0F 28 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f28 },
  /* 0F 29 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f29 },
  /* 0F 2A /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f2a },
  /* 0F 2B /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f2b },
  /* 0F 2C /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f2c },
  /* 0F 2D /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f2d },
  /* 0F 2E /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f2e },
  /* 0F 2F /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f2f },
  /* 0F 30 /w */ { 0, &BX_CPU_C::WRMSR },
  /* 0F 31 /w */ { 0, &BX_CPU_C::RDTSC },
  /* 0F 32 /w */ { 0, &BX_CPU_C::RDMSR },
  /* 0F 33 /w */ { 0, &BX_CPU_C::RDPMC },
  /* 0F 34 /w */ { 0, &BX_CPU_C::SYSENTER },
  /* 0F 35 /w */ { 0, &BX_CPU_C::SYSEXIT },
  /* 0F 36 /w */ { 0, &BX_CPU_C::BxError },
  /* 0F 37 /w */ { 0, &BX_CPU_C::BxError },
#if (BX_SUPPORT_SSE >= 4) || (BX_SUPPORT_SSE >= 3 && BX_SUPPORT_SSE_EXTENSION > 0)
  /* 0F 38 /w */ { Bx3ByteOpcode | Bx3ByteOpTable, NULL, BxOpcode3ByteTableA4 }, // 3-byte escape
#else
  /* 0F 38 /w */ { 0, &BX_CPU_C::BxError },
#endif
  /* 0F 39 /w */ { 0, &BX_CPU_C::BxError },
#if (BX_SUPPORT_SSE >= 4) || (BX_SUPPORT_SSE >= 3 && BX_SUPPORT_SSE_EXTENSION > 0)
  /* 0F 3A /w */ { Bx3ByteOpcode | Bx3ByteOpTable, NULL, BxOpcode3ByteTableA5 }, // 3-byte escape
#else
  /* 0F 3A /w */ { 0, &BX_CPU_C::BxError },
#endif
  /* 0F 3B /w */ { 0, &BX_CPU_C::BxError },
  /* 0F 3C /w */ { 0, &BX_CPU_C::BxError },
  /* 0F 3D /w */ { 0, &BX_CPU_C::BxError },
  /* 0F 3E /w */ { 0, &BX_CPU_C::BxError },
  /* 0F 3F /w */ { 0, &BX_CPU_C::BxError },
  /* 0F 40 /w */ { 0, &BX_CPU_C::CMOV_GwEw },
  /* 0F 41 /w */ { 0, &BX_CPU_C::CMOV_GwEw },
  /* 0F 42 /w */ { 0, &BX_CPU_C::CMOV_GwEw },
  /* 0F 43 /w */ { 0, &BX_CPU_C::CMOV_GwEw },
  /* 0F 44 /w */ { 0, &BX_CPU_C::CMOV_GwEw },
  /* 0F 45 /w */ { 0, &BX_CPU_C::CMOV_GwEw },
  /* 0F 46 /w */ { 0, &BX_CPU_C::CMOV_GwEw },
  /* 0F 47 /w */ { 0, &BX_CPU_C::CMOV_GwEw },
  /* 0F 48 /w */ { 0, &BX_CPU_C::CMOV_GwEw },
  /* 0F 49 /w */ { 0, &BX_CPU_C::CMOV_GwEw },
  /* 0F 4A /w */ { 0, &BX_CPU_C::CMOV_GwEw },
  /* 0F 4B /w */ { 0, &BX_CPU_C::CMOV_GwEw },
  /* 0F 4C /w */ { 0, &BX_CPU_C::CMOV_GwEw },
  /* 0F 4D /w */ { 0, &BX_CPU_C::CMOV_GwEw },
  /* 0F 4E /w */ { 0, &BX_CPU_C::CMOV_GwEw },
  /* 0F 4F /w */ { 0, &BX_CPU_C::CMOV_GwEw },
  /* 0F 50 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f50 },
  /* 0F 51 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f51 },
  /* 0F 52 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f52 },
  /* 0F 53 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f53 },
  /* 0F 54 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f54 },
  /* 0F 55 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f55 },
  /* 0F 56 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f56 },
  /* 0F 57 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f57 },
  /* 0F 58 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f58 },
  /* 0F 59 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f59 },
  /* 0F 5A /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f5a },
  /* 0F 5B /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f5b },
  /* 0F 5C /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f5c },
  /* 0F 5D /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f5d },
  /* 0F 5E /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f5e },
  /* 0F 5F /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f5f },
  /* 0F 60 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f60 },
  /* 0F 61 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f61 }, 
  /* 0F 62 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f62 }, 
  /* 0F 63 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f63 }, 
  /* 0F 64 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f64 }, 
  /* 0F 65 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f65 }, 
  /* 0F 66 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f66 }, 
  /* 0F 67 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f67 }, 
  /* 0F 68 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f68 }, 
  /* 0F 69 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f69 }, 
  /* 0F 6A /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f6a }, 
  /* 0F 6B /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f6b }, 
  /* 0F 6C /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f6c },
  /* 0F 6D /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f6d },
  /* 0F 6E /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f6e }, 
  /* 0F 6F /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f6f }, 
  /* 0F 70 /w */ { BxImmediate_Ib | BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f70 },
  /* 0F 71 /w */ { BxGroup12, NULL, BxOpcodeInfoG12 },
  /* 0F 72 /w */ { BxGroup13, NULL, BxOpcodeInfoG13 },
  /* 0F 73 /w */ { BxGroup14, NULL, BxOpcodeInfoG14 },
  /* 0F 74 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f74 }, 
  /* 0F 75 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f75 }, 
  /* 0F 76 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f76 }, 
  /* 0F 77 /w */ { 0, &BX_CPU_C::EMMS },     
  /* 0F 78 /w */ { 0, &BX_CPU_C::BxError },
  /* 0F 79 /w */ { 0, &BX_CPU_C::BxError },
  /* 0F 7A /w */ { 0, &BX_CPU_C::BxError },
  /* 0F 7B /w */ { 0, &BX_CPU_C::BxError },
  /* 0F 7C /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f7c }, 
  /* 0F 7D /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f7d }, 
  /* 0F 7E /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f7e }, 
  /* 0F 7F /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f7f }, 
  /* 0F 80 /w */ { BxImmediate_BrOff16, &BX_CPU_C::JO_Jw },
  /* 0F 81 /w */ { BxImmediate_BrOff16, &BX_CPU_C::JNO_Jw },
  /* 0F 82 /w */ { BxImmediate_BrOff16, &BX_CPU_C::JB_Jw },
  /* 0F 83 /w */ { BxImmediate_BrOff16, &BX_CPU_C::JNB_Jw },
  /* 0F 84 /w */ { BxImmediate_BrOff16, &BX_CPU_C::JZ_Jw },
  /* 0F 85 /w */ { BxImmediate_BrOff16, &BX_CPU_C::JNZ_Jw },
  /* 0F 86 /w */ { BxImmediate_BrOff16, &BX_CPU_C::JBE_Jw },
  /* 0F 87 /w */ { BxImmediate_BrOff16, &BX_CPU_C::JNBE_Jw },
  /* 0F 88 /w */ { BxImmediate_BrOff16, &BX_CPU_C::JS_Jw },
  /* 0F 89 /w */ { BxImmediate_BrOff16, &BX_CPU_C::JNS_Jw },
  /* 0F 8A /w */ { BxImmediate_BrOff16, &BX_CPU_C::JP_Jw },
  /* 0F 8B /w */ { BxImmediate_BrOff16, &BX_CPU_C::JNP_Jw },
  /* 0F 8C /w */ { BxImmediate_BrOff16, &BX_CPU_C::JL_Jw },
  /* 0F 8D /w */ { BxImmediate_BrOff16, &BX_CPU_C::JNL_Jw },
  /* 0F 8E /w */ { BxImmediate_BrOff16, &BX_CPU_C::JLE_Jw },
  /* 0F 8F /w */ { BxImmediate_BrOff16, &BX_CPU_C::JNLE_Jw },
  /* 0F 90 /w */ { 0, &BX_CPU_C::SETO_Eb },
  /* 0F 91 /w */ { 0, &BX_CPU_C::SETNO_Eb },
  /* 0F 92 /w */ { 0, &BX_CPU_C::SETB_Eb },
  /* 0F 93 /w */ { 0, &BX_CPU_C::SETNB_Eb },
  /* 0F 94 /w */ { 0, &BX_CPU_C::SETZ_Eb },
  /* 0F 95 /w */ { 0, &BX_CPU_C::SETNZ_Eb },
  /* 0F 96 /w */ { 0, &BX_CPU_C::SETBE_Eb },
  /* 0F 97 /w */ { 0, &BX_CPU_C::SETNBE_Eb },
  /* 0F 98 /w */ { 0, &BX_CPU_C::SETS_Eb },
  /* 0F 99 /w */ { 0, &BX_CPU_C::SETNS_Eb },
  /* 0F 9A /w */ { 0, &BX_CPU_C::SETP_Eb },
  /* 0F 9B /w */ { 0, &BX_CPU_C::SETNP_Eb },
  /* 0F 9C /w */ { 0, &BX_CPU_C::SETL_Eb },
  /* 0F 9D /w */ { 0, &BX_CPU_C::SETNL_Eb },
  /* 0F 9E /w */ { 0, &BX_CPU_C::SETLE_Eb },
  /* 0F 9F /w */ { 0, &BX_CPU_C::SETNLE_Eb },
  /* 0F A0 /w */ { 0, &BX_CPU_C::PUSH16_FS },
  /* 0F A1 /w */ { 0, &BX_CPU_C::POP16_FS },
  /* 0F A2 /w */ { 0, &BX_CPU_C::CPUID },
  /* 0F A3 /w */ { 0, &BX_CPU_C::BT_EwGw },
  /* 0F A4 /w */ { BxImmediate_Ib, &BX_CPU_C::SHLD_EwGw },
  /* 0F A5 /w */ { 0,              &BX_CPU_C::SHLD_EwGw },
  /* 0F A6 /w */ { 0, &BX_CPU_C::CMPXCHG_XBTS },
  /* 0F A7 /w */ { 0, &BX_CPU_C::CMPXCHG_IBTS },
  /* 0F A8 /w */ { 0, &BX_CPU_C::PUSH16_GS },
  /* 0F A9 /w */ { 0, &BX_CPU_C::POP16_GS },
  /* 0F AA /w */ { 0, &BX_CPU_C::RSM },
  /* 0F AB /w */ { BxLockable, &BX_CPU_C::BTS_EwGw },
  /* 0F AC /w */ { BxImmediate_Ib, &BX_CPU_C::SHRD_EwGw },
  /* 0F AD /w */ { 0,              &BX_CPU_C::SHRD_EwGw },
  /* 0F AE /w */ { BxGroup15, NULL, BxOpcodeInfoG15 },
  /* 0F AF /w */ { 0, &BX_CPU_C::IMUL_GwEw },
  /* 0F B0 /w */ { BxLockable, &BX_CPU_C::CMPXCHG_EbGb },
  /* 0F B1 /w */ { BxLockable, &BX_CPU_C::CMPXCHG_EwGw },
  /* 0F B2 /w */ { 0, &BX_CPU_C::LSS_GwMp },
  /* 0F B3 /w */ { BxLockable, &BX_CPU_C::BTR_EwGw },
  /* 0F B4 /w */ { 0, &BX_CPU_C::LFS_GwMp },
  /* 0F B5 /w */ { 0, &BX_CPU_C::LGS_GwMp },
  /* 0F B6 /w */ { 0, &BX_CPU_C::MOVZX_GwEb },
  /* 0F B7 /w */ { BxSplitMod11b, NULL, opcodesMOV_GwEw }, // MOVZX_GwEw
  /* 0F B8 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fb8w },
  /* 0F B9 /w */ { 0, &BX_CPU_C::UndefinedOpcode }, // UD2 opcode
  /* 0F BA /w */ { BxGroup8, NULL, BxOpcodeInfoG8EwIb },
  /* 0F BB /w */ { BxLockable, &BX_CPU_C::BTC_EwGw },
  /* 0F BC /w */ { 0, &BX_CPU_C::BSF_GwEw },
  /* 0F BD /w */ { 0, &BX_CPU_C::BSR_GwEw },
  /* 0F BE /w */ { 0, &BX_CPU_C::MOVSX_GwEb },
  /* 0F BF /w */ { BxSplitMod11b, NULL, opcodesMOV_GwEw }, // MOVSX_GwEw
  /* 0F C0 /w */ { BxLockable, &BX_CPU_C::XADD_EbGb },
  /* 0F C1 /w */ { BxLockable, &BX_CPU_C::XADD_EwGw },
  /* 0F C2 /w */ { BxImmediate_Ib | BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fc2 },
  /* 0F C3 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fc3 },
  /* 0F C4 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fc4 },
  /* 0F C5 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fc5 },
  /* 0F C6 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fc6 },
  /* 0F C7 /w */ { BxGroup9, NULL, BxOpcodeInfoG9 },
  /* 0F C8 /w */ { 0, &BX_CPU_C::BSWAP_ERX },
  /* 0F C9 /w */ { 0, &BX_CPU_C::BSWAP_ERX },
  /* 0F CA /w */ { 0, &BX_CPU_C::BSWAP_ERX },
  /* 0F CB /w */ { 0, &BX_CPU_C::BSWAP_ERX },
  /* 0F CC /w */ { 0, &BX_CPU_C::BSWAP_ERX },
  /* 0F CD /w */ { 0, &BX_CPU_C::BSWAP_ERX },
  /* 0F CE /w */ { 0, &BX_CPU_C::BSWAP_ERX },
  /* 0F CF /w */ { 0, &BX_CPU_C::BSWAP_ERX },
  /* 0F D0 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fd0 },
  /* 0F D1 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fd1 },
  /* 0F D2 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fd2 },
  /* 0F D3 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fd3 },
  /* 0F D4 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fd4 },
  /* 0F D5 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fd5 }, 
  /* 0F D6 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fd6 },
  /* 0F D7 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fd7 },
  /* 0F D8 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fd8 },
  /* 0F D9 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fd9 },
  /* 0F DA /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fda },
  /* 0F DB /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fdb },
  /* 0F DC /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fdc },
  /* 0F DD /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fdd },
  /* 0F DE /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fde },
  /* 0F DF /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fdf },
  /* 0F E0 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fe0 },
  /* 0F E1 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fe1 }, 
  /* 0F E2 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fe2 }, 
  /* 0F E3 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fe3 },
  /* 0F E4 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fe4 },
  /* 0F E5 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fe5 },
  /* 0F E6 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fe6 },
  /* 0F E7 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fe7 },
  /* 0F E8 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fe8 },
  /* 0F E9 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fe9 },
  /* 0F EA /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fea },
  /* 0F EB /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0feb },
  /* 0F EC /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fec },
  /* 0F ED /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fed },
  /* 0F EE /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fee },
  /* 0F EF /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fef },
  /* 0F F0 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0ff0 }, 
  /* 0F F1 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0ff1 }, 
  /* 0F F2 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0ff2 }, 
  /* 0F F3 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0ff3 }, 
  /* 0F F4 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0ff4 }, 
  /* 0F F5 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0ff5 }, 
  /* 0F F6 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0ff6 }, 
  /* 0F F7 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0ff7 }, 
  /* 0F F8 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0ff8 }, 
  /* 0F F9 /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0ff9 }, 
  /* 0F FA /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0ffa }, 
  /* 0F FB /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0ffb }, 
  /* 0F FC /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0ffc }, 
  /* 0F FD /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0ffd }, 
  /* 0F FE /w */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0ffe }, 
  /* 0F FF /w */ { 0, &BX_CPU_C::BxError },

  // 512 entries for 32bit mode
  /* 00 /d */ { BxLockable, &BX_CPU_C::ADD_EbGb },
  /* 01 /d */ { BxLockable, &BX_CPU_C::ADD_EdGd },
  /* 02 /d */ { 0, &BX_CPU_C::ADD_GbEb },
  /* 03 /d */ { BxSplitMod11b, NULL, opcodesADD_GdEd },
  /* 04 /d */ { BxImmediate_Ib, &BX_CPU_C::ADD_ALIb },
  /* 05 /d */ { BxImmediate_Iv, &BX_CPU_C::ADD_EAXId },
  /* 06 /d */ { 0, &BX_CPU_C::PUSH32_ES },
  /* 07 /d */ { 0, &BX_CPU_C::POP32_ES },
  /* 08 /d */ { BxLockable, &BX_CPU_C::OR_EbGb },
  /* 09 /d */ { BxLockable, &BX_CPU_C::OR_EdGd },
  /* 0A /d */ { 0, &BX_CPU_C::OR_GbEb },
  /* 0B /d */ { 0, &BX_CPU_C::OR_GdEd },
  /* 0C /d */ { BxImmediate_Ib, &BX_CPU_C::OR_ALIb },
  /* 0D /d */ { BxImmediate_Iv, &BX_CPU_C::OR_EAXId },
  /* 0E /d */ { 0, &BX_CPU_C::PUSH32_CS },
  /* 0F /d */ { 0, &BX_CPU_C::BxError }, // 2-byte escape
  /* 10 /d */ { BxLockable, &BX_CPU_C::ADC_EbGb },
  /* 11 /d */ { BxLockable, &BX_CPU_C::ADC_EdGd },
  /* 12 /d */ { 0, &BX_CPU_C::ADC_GbEb },
  /* 13 /d */ { 0, &BX_CPU_C::ADC_GdEd },
  /* 14 /d */ { BxImmediate_Ib, &BX_CPU_C::ADC_ALIb },
  /* 15 /d */ { BxImmediate_Iv, &BX_CPU_C::ADC_EAXId },
  /* 16 /d */ { 0, &BX_CPU_C::PUSH32_SS },
  /* 17 /d */ { 0, &BX_CPU_C::POP32_SS },
  /* 18 /d */ { BxLockable, &BX_CPU_C::SBB_EbGb },
  /* 19 /d */ { BxLockable, &BX_CPU_C::SBB_EdGd },
  /* 1A /d */ { 0, &BX_CPU_C::SBB_GbEb },
  /* 1B /d */ { 0, &BX_CPU_C::SBB_GdEd },
  /* 1C /d */ { BxImmediate_Ib, &BX_CPU_C::SBB_ALIb },
  /* 1D /d */ { BxImmediate_Iv, &BX_CPU_C::SBB_EAXId },
  /* 1E /d */ { 0, &BX_CPU_C::PUSH32_DS },
  /* 1F /d */ { 0, &BX_CPU_C::POP32_DS },
  /* 20 /d */ { BxLockable, &BX_CPU_C::AND_EbGb },
  /* 21 /d */ { BxLockable, &BX_CPU_C::AND_EdGd },
  /* 22 /d */ { 0, &BX_CPU_C::AND_GbEb },
  /* 23 /d */ { 0, &BX_CPU_C::AND_GdEd },
  /* 24 /d */ { BxImmediate_Ib, &BX_CPU_C::AND_ALIb },
  /* 25 /d */ { BxImmediate_Iv, &BX_CPU_C::AND_EAXId },
  /* 26 /d */ { BxPrefix, &BX_CPU_C::BxError }, // ES:
  /* 27 /d */ { 0, &BX_CPU_C::DAA },
  /* 28 /d */ { BxLockable, &BX_CPU_C::SUB_EbGb },
  /* 29 /d */ { BxLockable, &BX_CPU_C::SUB_EdGd },
  /* 2A /d */ { 0, &BX_CPU_C::SUB_GbEb },
  /* 2B /d */ { 0, &BX_CPU_C::SUB_GdEd },
  /* 2C /d */ { BxImmediate_Ib, &BX_CPU_C::SUB_ALIb },
  /* 2D /d */ { BxImmediate_Iv, &BX_CPU_C::SUB_EAXId },
  /* 2E /d */ { BxPrefix, &BX_CPU_C::BxError }, // CS:
  /* 2F /d */ { 0, &BX_CPU_C::DAS },
  /* 30 /d */ { BxLockable, &BX_CPU_C::XOR_EbGb },
  /* 31 /d */ { BxLockable, &BX_CPU_C::XOR_EdGd },
  /* 32 /d */ { 0, &BX_CPU_C::XOR_GbEb },
  /* 33 /d */ { 0, &BX_CPU_C::XOR_GdEd },
  /* 34 /d */ { BxImmediate_Ib, &BX_CPU_C::XOR_ALIb },
  /* 35 /d */ { BxImmediate_Iv, &BX_CPU_C::XOR_EAXId },
  /* 36 /d */ { BxPrefix, &BX_CPU_C::BxError }, // SS:
  /* 37 /d */ { 0, &BX_CPU_C::AAA },
  /* 38 /d */ { 0, &BX_CPU_C::CMP_EbGb },
  /* 39 /d */ { 0, &BX_CPU_C::CMP_EdGd },
  /* 3A /d */ { 0, &BX_CPU_C::CMP_GbEb },
  /* 3B /d */ { 0, &BX_CPU_C::CMP_GdEd },
  /* 3C /d */ { BxImmediate_Ib, &BX_CPU_C::CMP_ALIb },
  /* 3D /d */ { BxImmediate_Iv, &BX_CPU_C::CMP_EAXId },
  /* 3E /d */ { BxPrefix, &BX_CPU_C::BxError }, // DS:
  /* 3F /d */ { 0, &BX_CPU_C::AAS },
  /* 40 /d */ { 0, &BX_CPU_C::INC_ERX },
  /* 41 /d */ { 0, &BX_CPU_C::INC_ERX },
  /* 42 /d */ { 0, &BX_CPU_C::INC_ERX },
  /* 43 /d */ { 0, &BX_CPU_C::INC_ERX },
  /* 44 /d */ { 0, &BX_CPU_C::INC_ERX },
  /* 45 /d */ { 0, &BX_CPU_C::INC_ERX },
  /* 46 /d */ { 0, &BX_CPU_C::INC_ERX },
  /* 47 /d */ { 0, &BX_CPU_C::INC_ERX },
  /* 48 /d */ { 0, &BX_CPU_C::DEC_ERX },
  /* 49 /d */ { 0, &BX_CPU_C::DEC_ERX },
  /* 4A /d */ { 0, &BX_CPU_C::DEC_ERX },
  /* 4B /d */ { 0, &BX_CPU_C::DEC_ERX },
  /* 4C /d */ { 0, &BX_CPU_C::DEC_ERX },
  /* 4D /d */ { 0, &BX_CPU_C::DEC_ERX },
  /* 4E /d */ { 0, &BX_CPU_C::DEC_ERX },
  /* 4F /d */ { 0, &BX_CPU_C::DEC_ERX },
  /* 50 /d */ { 0, &BX_CPU_C::PUSH_ERX },
  /* 51 /d */ { 0, &BX_CPU_C::PUSH_ERX },
  /* 52 /d */ { 0, &BX_CPU_C::PUSH_ERX },
  /* 53 /d */ { 0, &BX_CPU_C::PUSH_ERX },
  /* 54 /d */ { 0, &BX_CPU_C::PUSH_ERX },
  /* 55 /d */ { 0, &BX_CPU_C::PUSH_ERX },
  /* 56 /d */ { 0, &BX_CPU_C::PUSH_ERX },
  /* 57 /d */ { 0, &BX_CPU_C::PUSH_ERX },
  /* 58 /d */ { 0, &BX_CPU_C::POP_ERX },
  /* 59 /d */ { 0, &BX_CPU_C::POP_ERX },
  /* 5A /d */ { 0, &BX_CPU_C::POP_ERX },
  /* 5B /d */ { 0, &BX_CPU_C::POP_ERX },
  /* 5C /d */ { 0, &BX_CPU_C::POP_ERX },
  /* 5D /d */ { 0, &BX_CPU_C::POP_ERX },
  /* 5E /d */ { 0, &BX_CPU_C::POP_ERX },
  /* 5F /d */ { 0, &BX_CPU_C::POP_ERX },
  /* 60 /d */ { 0, &BX_CPU_C::PUSHAD32 },
  /* 61 /d */ { 0, &BX_CPU_C::POPAD32 },
  /* 62 /d */ { 0, &BX_CPU_C::BOUND_GdMa },
  /* 63 /d */ { 0, &BX_CPU_C::ARPL_EwGw },
  /* 64 /d */ { BxPrefix, &BX_CPU_C::BxError }, // FS:
  /* 65 /d */ { BxPrefix, &BX_CPU_C::BxError }, // GS:
  /* 66 /d */ { BxPrefix, &BX_CPU_C::BxError }, // OS:
  /* 67 /d */ { BxPrefix, &BX_CPU_C::BxError }, // AS:
  /* 68 /d */ { BxImmediate_Iv, &BX_CPU_C::PUSH_Id },
  /* 69 /d */ { BxImmediate_Iv, &BX_CPU_C::IMUL_GdEdId },
  /* 6A /d */ { BxImmediate_Ib_SE, &BX_CPU_C::PUSH_Id },
  /* 6B /d */ { BxImmediate_Ib_SE, &BX_CPU_C::IMUL_GdEdId },
  /* 6C /d */ { 0, &BX_CPU_C::REP_INSB_YbDX },
  /* 6D /d */ { 0, &BX_CPU_C::REP_INSD_YdDX },
  /* 6E /d */ { 0, &BX_CPU_C::REP_OUTSB_DXXb },
  /* 6F /d */ { 0, &BX_CPU_C::REP_OUTSD_DXXd },
  /* 70 /d */ { BxImmediate_BrOff8, &BX_CPU_C::JO_Jd },
  /* 71 /d */ { BxImmediate_BrOff8, &BX_CPU_C::JNO_Jd },
  /* 72 /d */ { BxImmediate_BrOff8, &BX_CPU_C::JB_Jd },
  /* 73 /d */ { BxImmediate_BrOff8, &BX_CPU_C::JNB_Jd },
  /* 74 /d */ { BxImmediate_BrOff8, &BX_CPU_C::JZ_Jd },
  /* 75 /d */ { BxImmediate_BrOff8, &BX_CPU_C::JNZ_Jd },
  /* 76 /d */ { BxImmediate_BrOff8, &BX_CPU_C::JBE_Jd },
  /* 77 /d */ { BxImmediate_BrOff8, &BX_CPU_C::JNBE_Jd },
  /* 78 /d */ { BxImmediate_BrOff8, &BX_CPU_C::JS_Jd },
  /* 79 /d */ { BxImmediate_BrOff8, &BX_CPU_C::JNS_Jd },
  /* 7A /d */ { BxImmediate_BrOff8, &BX_CPU_C::JP_Jd },
  /* 7B /d */ { BxImmediate_BrOff8, &BX_CPU_C::JNP_Jd },
  /* 7C /d */ { BxImmediate_BrOff8, &BX_CPU_C::JL_Jd },
  /* 7D /d */ { BxImmediate_BrOff8, &BX_CPU_C::JNL_Jd },
  /* 7E /d */ { BxImmediate_BrOff8, &BX_CPU_C::JLE_Jd },
  /* 7F /d */ { BxImmediate_BrOff8, &BX_CPU_C::JNLE_Jd },
  /* 80 /d */ { BxGroup1 | BxImmediate_Ib, NULL, BxOpcodeInfoG1EbIb },
  /* 81 /d */ { BxSplitMod11b | BxImmediate_Iv,    NULL, BxOpcodeInfoModG1Ed },
  /* 82 /d */ { BxGroup1 | BxImmediate_Ib, NULL, BxOpcodeInfoG1EbIb },
  /* 83 /d */ { BxSplitMod11b | BxImmediate_Ib_SE, NULL, BxOpcodeInfoModG1Ed },
  /* 84 /d */ { 0, &BX_CPU_C::TEST_EbGb },
  /* 85 /d */ { 0, &BX_CPU_C::TEST_EdGd },
  /* 86 /d */ { BxLockable, &BX_CPU_C::XCHG_EbGb },
  /* 87 /d */ { BxLockable, &BX_CPU_C::XCHG_EdGd },
  /* 88 /d */ { BxSplitMod11b, NULL, opcodesMOV_EbGb },
  /* 89 /d */ { BxSplitMod11b, NULL, opcodesMOV_EdGd },
  /* 8A /d */ { BxSplitMod11b, NULL, opcodesMOV_GbEb },
  /* 8B /d */ { BxSplitMod11b, NULL, opcodesMOV_GdEd },
  /* 8C /d */ { 0, &BX_CPU_C::MOV_EwSw },
  /* 8D /d */ { 0, &BX_CPU_C::LEA_GdM },
  /* 8E /d */ { 0, &BX_CPU_C::MOV_SwEw },
  /* 8F /d */ { 0, &BX_CPU_C::POP_Ed },
  /* 90 /d */ { 0, &BX_CPU_C::NOP },
  /* 91 /d */ { 0, &BX_CPU_C::XCHG_ERXEAX },
  /* 92 /d */ { 0, &BX_CPU_C::XCHG_ERXEAX },
  /* 93 /d */ { 0, &BX_CPU_C::XCHG_ERXEAX },
  /* 94 /d */ { 0, &BX_CPU_C::XCHG_ERXEAX },
  /* 95 /d */ { 0, &BX_CPU_C::XCHG_ERXEAX },
  /* 96 /d */ { 0, &BX_CPU_C::XCHG_ERXEAX },
  /* 97 /d */ { 0, &BX_CPU_C::XCHG_ERXEAX },
  /* 98 /d */ { 0, &BX_CPU_C::CWDE },
  /* 99 /d */ { 0, &BX_CPU_C::CDQ },
  /* 9A /d */ { BxImmediate_IvIw, &BX_CPU_C::CALL32_Ap },
  /* 9B /d */ { 0, &BX_CPU_C::FWAIT },
  /* 9C /d */ { 0, &BX_CPU_C::PUSHF_Fd },
  /* 9D /d */ { 0, &BX_CPU_C::POPF_Fd },
  /* 9E /d */ { 0, &BX_CPU_C::SAHF },
  /* 9F /d */ { 0, &BX_CPU_C::LAHF },
  /* A0 /d */ { BxImmediate_O, &BX_CPU_C::MOV_ALOd },
  /* A1 /d */ { BxImmediate_O, &BX_CPU_C::MOV_EAXOd },
  /* A2 /d */ { BxImmediate_O, &BX_CPU_C::MOV_OdAL },
  /* A3 /d */ { BxImmediate_O, &BX_CPU_C::MOV_OdEAX },
  /* A4 /d */ { 0, &BX_CPU_C::REP_MOVSB_XbYb },
  /* A5 /d */ { 0, &BX_CPU_C::REP_MOVSD_XdYd },
  /* A6 /d */ { 0, &BX_CPU_C::REP_CMPSB_XbYb },
  /* A7 /d */ { 0, &BX_CPU_C::REP_CMPSD_XdYd },
  /* A8 /d */ { BxImmediate_Ib, &BX_CPU_C::TEST_ALIb },
  /* A9 /d */ { BxImmediate_Iv, &BX_CPU_C::TEST_EAXId },
  /* AA /d */ { 0, &BX_CPU_C::REP_STOSB_YbAL },
  /* AB /d */ { 0, &BX_CPU_C::REP_STOSD_YdEAX },
  /* AC /d */ { 0, &BX_CPU_C::REP_LODSB_ALXb },
  /* AD /d */ { 0, &BX_CPU_C::REP_LODSD_EAXXd },
  /* AE /d */ { 0, &BX_CPU_C::REP_SCASB_ALXb  },
  /* AF /d */ { 0, &BX_CPU_C::REP_SCASD_EAXXd },
  /* B0 /d */ { BxImmediate_Ib, &BX_CPU_C::MOV_RLIb },
  /* B1 /d */ { BxImmediate_Ib, &BX_CPU_C::MOV_RLIb },
  /* B2 /d */ { BxImmediate_Ib, &BX_CPU_C::MOV_RLIb },
  /* B3 /d */ { BxImmediate_Ib, &BX_CPU_C::MOV_RLIb },
  /* B4 /d */ { BxImmediate_Ib, &BX_CPU_C::MOV_RHIb },
  /* B5 /d */ { BxImmediate_Ib, &BX_CPU_C::MOV_RHIb },
  /* B6 /d */ { BxImmediate_Ib, &BX_CPU_C::MOV_RHIb },
  /* B7 /d */ { BxImmediate_Ib, &BX_CPU_C::MOV_RHIb },
  /* B8 /d */ { BxImmediate_Iv, &BX_CPU_C::MOV_ERXId },
  /* B9 /d */ { BxImmediate_Iv, &BX_CPU_C::MOV_ERXId },
  /* BA /d */ { BxImmediate_Iv, &BX_CPU_C::MOV_ERXId },
  /* BB /d */ { BxImmediate_Iv, &BX_CPU_C::MOV_ERXId },
  /* BC /d */ { BxImmediate_Iv, &BX_CPU_C::MOV_ERXId },
  /* BD /d */ { BxImmediate_Iv, &BX_CPU_C::MOV_ERXId },
  /* BE /d */ { BxImmediate_Iv, &BX_CPU_C::MOV_ERXId },
  /* BF /d */ { BxImmediate_Iv, &BX_CPU_C::MOV_ERXId },
  /* C0 /d */ { BxGroup2 | BxImmediate_Ib, NULL, BxOpcodeInfoG2Eb },
  /* C1 /d */ { BxGroup2 | BxImmediate_Ib, NULL, BxOpcodeInfoG2Ed },
  /* C2 /d */ { BxImmediate_Iw, &BX_CPU_C::RETnear32_Iw },
  /* C3 /d */ { 0,              &BX_CPU_C::RETnear32 },
  /* C4 /d */ { 0, &BX_CPU_C::LES_GdMp },
  /* C5 /d */ { 0, &BX_CPU_C::LDS_GdMp },
  /* C6 /d */ { BxImmediate_Ib, &BX_CPU_C::MOV_EbIb },
  /* C7 /d */ { BxImmediate_Iv, &BX_CPU_C::MOV_EdId },
  /* C8 /d */ { BxImmediate_IwIb, &BX_CPU_C::ENTER_IwIb },
  /* C9 /d */ { 0, &BX_CPU_C::LEAVE },
  /* CA /d */ { BxImmediate_Iw, &BX_CPU_C::RETfar32_Iw },
  /* CB /d */ { 0, &BX_CPU_C::RETfar32 },
  /* CC /d */ { 0, &BX_CPU_C::INT3 },
  /* CD /d */ { BxImmediate_Ib, &BX_CPU_C::INT_Ib },
  /* CE /d */ { 0, &BX_CPU_C::INTO },
  /* CF /d */ { 0, &BX_CPU_C::IRET32 },
  /* D0 /d */ { BxGroup2, NULL, BxOpcodeInfoG2Eb },
  /* D1 /d */ { BxGroup2, NULL, BxOpcodeInfoG2Ed },
  /* D2 /d */ { BxGroup2, NULL, BxOpcodeInfoG2Eb },
  /* D3 /d */ { BxGroup2, NULL, BxOpcodeInfoG2Ed },
  /* D4 /d */ { BxImmediate_Ib, &BX_CPU_C::AAM },
  /* D5 /d */ { BxImmediate_Ib, &BX_CPU_C::AAD },
  /* D6 /d */ { 0, &BX_CPU_C::SALC },
  /* D7 /d */ { 0, &BX_CPU_C::XLAT },
#if BX_SUPPORT_FPU
  //    by default we have here pointer to the group .. as if mod <> 11b
  /* D8 /d */ { BxFPGroup, NULL, BxOpcodeInfo_FPGroupD8 },
  /* D9 /d */ { BxFPGroup, NULL, BxOpcodeInfo_FPGroupD9 },
  /* DA /d */ { BxFPGroup, NULL, BxOpcodeInfo_FPGroupDA },
  /* DB /d */ { BxFPGroup, NULL, BxOpcodeInfo_FPGroupDB },
  /* DC /d */ { BxFPGroup, NULL, BxOpcodeInfo_FPGroupDC },
  /* DD /d */ { BxFPGroup, NULL, BxOpcodeInfo_FPGroupDD },
  /* DE /d */ { BxFPGroup, NULL, BxOpcodeInfo_FPGroupDE },
  /* DF /d */ { BxFPGroup, NULL, BxOpcodeInfo_FPGroupDF },
#else
  /* D8 /d */ { 0, &BX_CPU_C::FPU_ESC },
  /* D9 /d */ { 0, &BX_CPU_C::FPU_ESC },
  /* DA /d */ { 0, &BX_CPU_C::FPU_ESC },
  /* DB /d */ { 0, &BX_CPU_C::FPU_ESC },
  /* DC /d */ { 0, &BX_CPU_C::FPU_ESC },
  /* DD /d */ { 0, &BX_CPU_C::FPU_ESC },
  /* DE /d */ { 0, &BX_CPU_C::FPU_ESC },
  /* DF /d */ { 0, &BX_CPU_C::FPU_ESC },
#endif
  /* E0 /d */ { BxImmediate_BrOff8, &BX_CPU_C::LOOPNE_Jb },
  /* E1 /d */ { BxImmediate_BrOff8, &BX_CPU_C::LOOPE_Jb },
  /* E2 /d */ { BxImmediate_BrOff8, &BX_CPU_C::LOOP_Jb },
  /* E3 /d */ { BxImmediate_BrOff8, &BX_CPU_C::JCXZ_Jb },
  /* E4 /d */ { BxImmediate_Ib, &BX_CPU_C::IN_ALIb },
  /* E5 /d */ { BxImmediate_Ib, &BX_CPU_C::IN_EAXIb },
  /* E6 /d */ { BxImmediate_Ib, &BX_CPU_C::OUT_IbAL },
  /* E7 /d */ { BxImmediate_Ib, &BX_CPU_C::OUT_IbEAX },
  /* E8 /d */ { BxImmediate_BrOff32, &BX_CPU_C::CALL_Jd },
  /* E9 /d */ { BxImmediate_BrOff32, &BX_CPU_C::JMP_Jd },
  /* EA /d */ { BxImmediate_IvIw, &BX_CPU_C::JMP_Ap },
  /* EB /d */ { BxImmediate_BrOff8, &BX_CPU_C::JMP_Jd },
  /* EC /d */ { 0, &BX_CPU_C::IN_ALDX },
  /* ED /d */ { 0, &BX_CPU_C::IN_EAXDX },
  /* EE /d */ { 0, &BX_CPU_C::OUT_DXAL },
  /* EF /d */ { 0, &BX_CPU_C::OUT_DXEAX },
  /* F0 /d */ { BxPrefix, &BX_CPU_C::BxError }, // LOCK:
  /* F1 /d */ { 0, &BX_CPU_C::INT1 },
  /* F2 /d */ { BxPrefix, &BX_CPU_C::BxError }, // REPNE/REPNZ
  /* F3 /d */ { BxPrefix, &BX_CPU_C::BxError }, // REP,REPE/REPZ
  /* F4 /d */ { 0, &BX_CPU_C::HLT },
  /* F5 /d */ { 0, &BX_CPU_C::CMC },
  /* F6 /d */ { BxGroup3, NULL, BxOpcodeInfoG3Eb },
  /* F7 /d */ { BxGroup3, NULL, BxOpcodeInfoG3Ed },
  /* F8 /d */ { 0, &BX_CPU_C::CLC },
  /* F9 /d */ { 0, &BX_CPU_C::STC },
  /* FA /d */ { 0, &BX_CPU_C::CLI },
  /* FB /d */ { 0, &BX_CPU_C::STI },
  /* FC /d */ { 0, &BX_CPU_C::CLD },
  /* FD /d */ { 0, &BX_CPU_C::STD },
  /* FE /d */ { BxSplitMod11b, NULL, BxOpcodeInfoModG4 },
  /* FF /d */ { BxGroup5, NULL, BxOpcodeInfoG5d },

  /* 0F 00 /d */ { BxGroup6, NULL, BxOpcodeInfoG6 },
  /* 0F 01 /d */ { BxSplitMod11b, NULL, BxOpcodeInfoModG7 },
  /* 0F 02 /d */ { 0, &BX_CPU_C::LAR_GvEw },
  /* 0F 03 /d */ { 0, &BX_CPU_C::LSL_GvEw },
  /* 0F 04 /d */ { 0, &BX_CPU_C::BxError },
#if BX_SUPPORT_X86_64
  /* 0F 05 /d */ { 0, &BX_CPU_C::SYSCALL },
#elif BX_CPU_LEVEL == 2
  /* 0F 05 /d */ { 0, &BX_CPU_C::LOADALL },
#else
  /* 0F 05 /d */ { 0, &BX_CPU_C::BxError },
#endif
  /* 0F 06 /d */ { 0, &BX_CPU_C::CLTS },
#if BX_SUPPORT_X86_64
  /* 0F 07 /d */ { 0, &BX_CPU_C::SYSRET },
#else
  /* 0F 07 /d */ { 0, &BX_CPU_C::BxError },
#endif
  /* 0F 08 /d */ { 0, &BX_CPU_C::INVD },
  /* 0F 09 /d */ { 0, &BX_CPU_C::WBINVD },
  /* 0F 0A /d */ { 0, &BX_CPU_C::BxError },
  /* 0F 0B /d */ { 0, &BX_CPU_C::UndefinedOpcode }, // UD2 opcode
  /* 0F 0C /d */ { 0, &BX_CPU_C::BxError },
#if BX_SUPPORT_X86_64 || BX_SUPPORT_3DNOW
  /* 0F 0D /d */ { 0, &BX_CPU_C::NOP },     // 3DNow! PREFETCH on AMD, NOP on Intel
#else
  /* 0F 0D /d */ { 0, &BX_CPU_C::BxError },
#endif
#if BX_SUPPORT_3DNOW
  /* 0F 0E /d */ { 0, &BX_CPU_C::EMMS },            // 3DNow! FEMMS
  /* 0F 0F /d */ { BxImmediate_Ib, NULL, Bx3DNowOpcodeInfo },
#else
  /* 0F 0E /d */ { 0, &BX_CPU_C::BxError },
  /* 0F 0F /d */ { 0, &BX_CPU_C::BxError },
#endif
  /* 0F 10 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f10 },
  /* 0F 11 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f11 },
  /* 0F 12 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f12 },
  /* 0F 13 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f13 },
  /* 0F 14 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f14 },
  /* 0F 15 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f15 },
  /* 0F 16 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f16 },
  /* 0F 17 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f17 },
  /* 0F 18 /d */ { 0, &BX_CPU_C::PREFETCH },  // opcode group G16, PREFETCH hints
  /* 0F 19 /d */ { 0, &BX_CPU_C::BxError },
  /* 0F 1A /d */ { 0, &BX_CPU_C::BxError },
  /* 0F 1B /d */ { 0, &BX_CPU_C::BxError },
  /* 0F 1C /d */ { 0, &BX_CPU_C::BxError },
  /* 0F 1D /d */ { 0, &BX_CPU_C::BxError },
  /* 0F 1E /d */ { 0, &BX_CPU_C::BxError },
#if BX_CPU_LEVEL < 6
  /* 0F 1F /d */ { 0, &BX_CPU_C::BxError },
#else
  /* 0F 1F /d */ { 0, &BX_CPU_C::NOP },      // multi-byte NOP
#endif
  /* 0F 20 /d */ { 0, &BX_CPU_C::MOV_RdCd },
  /* 0F 21 /d */ { 0, &BX_CPU_C::MOV_RdDd },
  /* 0F 22 /d */ { 0, &BX_CPU_C::MOV_CdRd },
  /* 0F 23 /d */ { 0, &BX_CPU_C::MOV_DdRd },
  /* 0F 24 /d */ { 0, &BX_CPU_C::MOV_RdTd },
  /* 0F 25 /d */ { 0, &BX_CPU_C::BxError },
  /* 0F 26 /d */ { 0, &BX_CPU_C::MOV_TdRd },
  /* 0F 27 /d */ { 0, &BX_CPU_C::BxError },
  /* 0F 28 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f28 },
  /* 0F 29 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f29 },
  /* 0F 2A /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f2a },
  /* 0F 2B /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f2b },
  /* 0F 2C /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f2c },
  /* 0F 2D /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f2d },
  /* 0F 2E /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f2e },
  /* 0F 2F /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f2f },
  /* 0F 30 /d */ { 0, &BX_CPU_C::WRMSR },
  /* 0F 31 /d */ { 0, &BX_CPU_C::RDTSC },
  /* 0F 32 /d */ { 0, &BX_CPU_C::RDMSR },
  /* 0F 33 /d */ { 0, &BX_CPU_C::RDPMC },
  /* 0F 34 /d */ { 0, &BX_CPU_C::SYSENTER },
  /* 0F 35 /d */ { 0, &BX_CPU_C::SYSEXIT },
  /* 0F 36 /d */ { 0, &BX_CPU_C::BxError },
  /* 0F 37 /d */ { 0, &BX_CPU_C::BxError },
#if (BX_SUPPORT_SSE >= 4) || (BX_SUPPORT_SSE >= 3 && BX_SUPPORT_SSE_EXTENSION > 0)
  /* 0F 38 /d */ { Bx3ByteOpcode | Bx3ByteOpTable, NULL, BxOpcode3ByteTableA4 }, // 3-byte escape
#else
  /* 0F 38 /d */ { 0, &BX_CPU_C::BxError },
#endif
  /* 0F 39 /d */ { 0, &BX_CPU_C::BxError },
#if (BX_SUPPORT_SSE >= 4) || (BX_SUPPORT_SSE >= 3 && BX_SUPPORT_SSE_EXTENSION > 0)
  /* 0F 3A /d */ { Bx3ByteOpcode | Bx3ByteOpTable, NULL, BxOpcode3ByteTableA5 }, // 3-byte escape
#else
  /* 0F 3A /d */ { 0, &BX_CPU_C::BxError },
#endif
  /* 0F 3B /d */ { 0, &BX_CPU_C::BxError },
  /* 0F 3C /d */ { 0, &BX_CPU_C::BxError },
  /* 0F 3D /d */ { 0, &BX_CPU_C::BxError },
  /* 0F 3E /d */ { 0, &BX_CPU_C::BxError },
  /* 0F 3F /d */ { 0, &BX_CPU_C::BxError },
  /* 0F 40 /d */ { 0, &BX_CPU_C::CMOV_GdEd },
  /* 0F 41 /d */ { 0, &BX_CPU_C::CMOV_GdEd },
  /* 0F 42 /d */ { 0, &BX_CPU_C::CMOV_GdEd },
  /* 0F 43 /d */ { 0, &BX_CPU_C::CMOV_GdEd },
  /* 0F 44 /d */ { 0, &BX_CPU_C::CMOV_GdEd },
  /* 0F 45 /d */ { 0, &BX_CPU_C::CMOV_GdEd },
  /* 0F 46 /d */ { 0, &BX_CPU_C::CMOV_GdEd },
  /* 0F 47 /d */ { 0, &BX_CPU_C::CMOV_GdEd },
  /* 0F 48 /d */ { 0, &BX_CPU_C::CMOV_GdEd },
  /* 0F 49 /d */ { 0, &BX_CPU_C::CMOV_GdEd },
  /* 0F 4A /d */ { 0, &BX_CPU_C::CMOV_GdEd },
  /* 0F 4B /d */ { 0, &BX_CPU_C::CMOV_GdEd },
  /* 0F 4C /d */ { 0, &BX_CPU_C::CMOV_GdEd },
  /* 0F 4D /d */ { 0, &BX_CPU_C::CMOV_GdEd },
  /* 0F 4E /d */ { 0, &BX_CPU_C::CMOV_GdEd },
  /* 0F 4F /d */ { 0, &BX_CPU_C::CMOV_GdEd },
  /* 0F 50 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f50 },
  /* 0F 51 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f51 },
  /* 0F 52 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f52 },
  /* 0F 53 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f53 },
  /* 0F 54 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f54 },
  /* 0F 55 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f55 },
  /* 0F 56 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f56 },
  /* 0F 57 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f57 },
  /* 0F 58 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f58 },
  /* 0F 59 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f59 },
  /* 0F 5A /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f5a },
  /* 0F 5B /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f5b },
  /* 0F 5C /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f5c },
  /* 0F 5D /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f5d },
  /* 0F 5E /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f5e },
  /* 0F 5F /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f5f },
  /* 0F 60 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f60 },
  /* 0F 61 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f61 }, 
  /* 0F 62 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f62 }, 
  /* 0F 63 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f63 }, 
  /* 0F 64 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f64 }, 
  /* 0F 65 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f65 }, 
  /* 0F 66 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f66 }, 
  /* 0F 67 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f67 }, 
  /* 0F 68 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f68 }, 
  /* 0F 69 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f69 }, 
  /* 0F 6A /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f6a }, 
  /* 0F 6B /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f6b }, 
  /* 0F 6C /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f6c },
  /* 0F 6D /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f6d },
  /* 0F 6E /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f6e }, 
  /* 0F 6F /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f6f }, 
  /* 0F 70 /d */ { BxImmediate_Ib | BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f70 },
  /* 0F 71 /d */ { BxGroup12, NULL, BxOpcodeInfoG12 },
  /* 0F 72 /d */ { BxGroup13, NULL, BxOpcodeInfoG13 },
  /* 0F 73 /d */ { BxGroup14, NULL, BxOpcodeInfoG14 },
  /* 0F 74 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f74 }, 
  /* 0F 75 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f75 }, 
  /* 0F 76 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f76 }, 
  /* 0F 77 /d */ { 0, &BX_CPU_C::EMMS }, 
  /* 0F 78 /d */ { 0, &BX_CPU_C::BxError },
  /* 0F 79 /d */ { 0, &BX_CPU_C::BxError },
  /* 0F 7A /d */ { 0, &BX_CPU_C::BxError },
  /* 0F 7B /d */ { 0, &BX_CPU_C::BxError },
  /* 0F 7C /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f7c }, 
  /* 0F 7D /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f7d }, 
  /* 0F 7E /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f7e }, 
  /* 0F 7F /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0f7f }, 
  /* 0F 80 /d */ { BxImmediate_BrOff32, &BX_CPU_C::JO_Jd },
  /* 0F 81 /d */ { BxImmediate_BrOff32, &BX_CPU_C::JNO_Jd },
  /* 0F 82 /d */ { BxImmediate_BrOff32, &BX_CPU_C::JB_Jd },
  /* 0F 83 /d */ { BxImmediate_BrOff32, &BX_CPU_C::JNB_Jd },
  /* 0F 84 /d */ { BxImmediate_BrOff32, &BX_CPU_C::JZ_Jd },
  /* 0F 85 /d */ { BxImmediate_BrOff32, &BX_CPU_C::JNZ_Jd },
  /* 0F 86 /d */ { BxImmediate_BrOff32, &BX_CPU_C::JBE_Jd },
  /* 0F 87 /d */ { BxImmediate_BrOff32, &BX_CPU_C::JNBE_Jd },
  /* 0F 88 /d */ { BxImmediate_BrOff32, &BX_CPU_C::JS_Jd },
  /* 0F 89 /d */ { BxImmediate_BrOff32, &BX_CPU_C::JNS_Jd },
  /* 0F 8A /d */ { BxImmediate_BrOff32, &BX_CPU_C::JP_Jd },
  /* 0F 8B /d */ { BxImmediate_BrOff32, &BX_CPU_C::JNP_Jd },
  /* 0F 8C /d */ { BxImmediate_BrOff32, &BX_CPU_C::JL_Jd },
  /* 0F 8D /d */ { BxImmediate_BrOff32, &BX_CPU_C::JNL_Jd },
  /* 0F 8E /d */ { BxImmediate_BrOff32, &BX_CPU_C::JLE_Jd },
  /* 0F 8F /d */ { BxImmediate_BrOff32, &BX_CPU_C::JNLE_Jd },
  /* 0F 90 /d */ { 0, &BX_CPU_C::SETO_Eb },
  /* 0F 91 /d */ { 0, &BX_CPU_C::SETNO_Eb },
  /* 0F 92 /d */ { 0, &BX_CPU_C::SETB_Eb },
  /* 0F 93 /d */ { 0, &BX_CPU_C::SETNB_Eb },
  /* 0F 94 /d */ { 0, &BX_CPU_C::SETZ_Eb },
  /* 0F 95 /d */ { 0, &BX_CPU_C::SETNZ_Eb },
  /* 0F 96 /d */ { 0, &BX_CPU_C::SETBE_Eb },
  /* 0F 97 /d */ { 0, &BX_CPU_C::SETNBE_Eb },
  /* 0F 98 /d */ { 0, &BX_CPU_C::SETS_Eb },
  /* 0F 99 /d */ { 0, &BX_CPU_C::SETNS_Eb },
  /* 0F 9A /d */ { 0, &BX_CPU_C::SETP_Eb },
  /* 0F 9B /d */ { 0, &BX_CPU_C::SETNP_Eb },
  /* 0F 9C /d */ { 0, &BX_CPU_C::SETL_Eb },
  /* 0F 9D /d */ { 0, &BX_CPU_C::SETNL_Eb },
  /* 0F 9E /d */ { 0, &BX_CPU_C::SETLE_Eb },
  /* 0F 9F /d */ { 0, &BX_CPU_C::SETNLE_Eb },
  /* 0F A0 /d */ { 0, &BX_CPU_C::PUSH32_FS },
  /* 0F A1 /d */ { 0, &BX_CPU_C::POP32_FS },
  /* 0F A2 /d */ { 0, &BX_CPU_C::CPUID },
  /* 0F A3 /d */ { 0, &BX_CPU_C::BT_EdGd },
  /* 0F A4 /d */ { BxImmediate_Ib, &BX_CPU_C::SHLD_EdGd },
  /* 0F A5 /d */ { 0,              &BX_CPU_C::SHLD_EdGd },
  /* 0F A6 /d */ { 0, &BX_CPU_C::CMPXCHG_XBTS },
  /* 0F A7 /d */ { 0, &BX_CPU_C::CMPXCHG_IBTS },
  /* 0F A8 /d */ { 0, &BX_CPU_C::PUSH32_GS },
  /* 0F A9 /d */ { 0, &BX_CPU_C::POP32_GS },
  /* 0F AA /d */ { 0, &BX_CPU_C::RSM },
  /* 0F AB /d */ { BxLockable, &BX_CPU_C::BTS_EdGd },
  /* 0F AC /d */ { BxImmediate_Ib, &BX_CPU_C::SHRD_EdGd },
  /* 0F AD /d */ { 0,              &BX_CPU_C::SHRD_EdGd },
  /* 0F AE /d */ { BxGroup15, NULL, BxOpcodeInfoG15 },
  /* 0F AF /d */ { 0, &BX_CPU_C::IMUL_GdEd },
  /* 0F B0 /d */ { BxLockable, &BX_CPU_C::CMPXCHG_EbGb },
  /* 0F B1 /d */ { BxLockable, &BX_CPU_C::CMPXCHG_EdGd },
  /* 0F B2 /d */ { 0, &BX_CPU_C::LSS_GdMp },
  /* 0F B3 /d */ { BxLockable, &BX_CPU_C::BTR_EdGd },
  /* 0F B4 /d */ { 0, &BX_CPU_C::LFS_GdMp },
  /* 0F B5 /d */ { 0, &BX_CPU_C::LGS_GdMp },
  /* 0F B6 /d */ { 0, &BX_CPU_C::MOVZX_GdEb },
  /* 0F B7 /d */ { 0, &BX_CPU_C::MOVZX_GdEw },
  /* 0F B8 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fb8d },
  /* 0F B9 /d */ { 0, &BX_CPU_C::UndefinedOpcode }, // UD2 opcode
  /* 0F BA /d */ { BxGroup8, NULL, BxOpcodeInfoG8EdIb },
  /* 0F BB /d */ { BxLockable, &BX_CPU_C::BTC_EdGd },
  /* 0F BC /d */ { 0, &BX_CPU_C::BSF_GdEd },
  /* 0F BD /d */ { 0, &BX_CPU_C::BSR_GdEd },
  /* 0F BE /d */ { 0, &BX_CPU_C::MOVSX_GdEb },
  /* 0F BF /d */ { 0, &BX_CPU_C::MOVSX_GdEw },
  /* 0F C0 /d */ { BxLockable, &BX_CPU_C::XADD_EbGb },
  /* 0F C1 /d */ { BxLockable, &BX_CPU_C::XADD_EdGd },
  /* 0F C2 /d */ { BxImmediate_Ib | BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fc2 },
  /* 0F C3 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fc3 },
  /* 0F C4 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fc4 },
  /* 0F C5 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fc5 },
  /* 0F C6 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fc6 },
  /* 0F C7 /d */ { BxGroup9, NULL, BxOpcodeInfoG9 },
  /* 0F C8 /d */ { 0, &BX_CPU_C::BSWAP_ERX },
  /* 0F C9 /d */ { 0, &BX_CPU_C::BSWAP_ERX },
  /* 0F CA /d */ { 0, &BX_CPU_C::BSWAP_ERX },
  /* 0F CB /d */ { 0, &BX_CPU_C::BSWAP_ERX },
  /* 0F CC /d */ { 0, &BX_CPU_C::BSWAP_ERX },
  /* 0F CD /d */ { 0, &BX_CPU_C::BSWAP_ERX },
  /* 0F CE /d */ { 0, &BX_CPU_C::BSWAP_ERX },
  /* 0F CF /d */ { 0, &BX_CPU_C::BSWAP_ERX },
  /* 0F D0 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fd0 },
  /* 0F D1 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fd1 },
  /* 0F D2 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fd2 },
  /* 0F D3 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fd3 },
  /* 0F D4 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fd4 },
  /* 0F D5 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fd5 }, 
  /* 0F D6 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fd6 },
  /* 0F D7 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fd7 },
  /* 0F D8 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fd8 },
  /* 0F D9 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fd9 },
  /* 0F DA /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fda },
  /* 0F DB /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fdb },
  /* 0F DC /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fdc },
  /* 0F DD /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fdd },
  /* 0F DE /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fde },
  /* 0F DF /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fdf },
  /* 0F E0 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fe0 },
  /* 0F E1 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fe1 }, 
  /* 0F E2 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fe2 }, 
  /* 0F E3 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fe3 },
  /* 0F E4 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fe4 },
  /* 0F E5 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fe5 },
  /* 0F E6 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fe6 },
  /* 0F E7 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fe7 },
  /* 0F E8 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fe8 },
  /* 0F E9 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fe9 },
  /* 0F EA /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fea },
  /* 0F EB /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0feb },
  /* 0F EC /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fec },
  /* 0F ED /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fed },
  /* 0F EE /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fee },
  /* 0F EF /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0fef },
  /* 0F F0 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0ff0 }, 
  /* 0F F1 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0ff1 }, 
  /* 0F F2 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0ff2 }, 
  /* 0F F3 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0ff3 }, 
  /* 0F F4 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0ff4 }, 
  /* 0F F5 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0ff5 }, 
  /* 0F F6 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0ff6 }, 
  /* 0F F7 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0ff7 }, 
  /* 0F F8 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0ff8 }, 
  /* 0F F9 /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0ff9 }, 
  /* 0F FA /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0ffa }, 
  /* 0F FB /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0ffb }, 
  /* 0F FC /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0ffc }, 
  /* 0F FD /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0ffd }, 
  /* 0F FE /d */ { BxPrefixSSE, NULL, BxOpcodeGroupSSE_0ffe }, 
  /* 0F FF /d */ { 0, &BX_CPU_C::BxError }
};

  unsigned
BX_CPU_C::fetchDecode32(Bit8u *iptr, bxInstruction_c *instruction, unsigned remain)
{
  // remain must be at least 1

  bx_bool is_32, lock=0;
  unsigned b1, b2, ilen=0, attr, os_32;
  unsigned imm_mode, offset;
  unsigned rm = 0, mod=0, nnn=0;

#define SSE_PREFIX_NONE 0
#define SSE_PREFIX_66   1
#define SSE_PREFIX_F2   2
#define SSE_PREFIX_F3   3
  unsigned sse_prefix = SSE_PREFIX_NONE;

  os_32 = is_32 =
    BX_CPU_THIS_PTR sregs[BX_SEG_REG_CS].cache.u.segment.d_b;

  instruction->ResolveModrm = BxResolveDummy;
  instruction->initMetaInfo(/*os32*/ is_32,  /*as32*/ is_32,
                            /*os64*/     0,  /*as64*/     0);

  offset = os_32 << 9; // * 512

fetch_b1:
  b1 = *iptr++;
  ilen++;
  attr = BxOpcodeInfo[b1+offset].Attr;

  if (attr & BxPrefix) {
    BX_INSTR_PREFIX(BX_CPU_ID, b1);
    switch (b1) {
      case 0x66: // OpSize
        os_32 = !is_32;
        offset = os_32 << 9;
        if(!sse_prefix) sse_prefix = SSE_PREFIX_66;
        instruction->setOs32B(os_32);
        if (ilen < remain) {
          goto fetch_b1;
        }
        return(0);
      case 0x67: // AddrSize
        instruction->setAs32B(!is_32);
        if (ilen < remain) {
          goto fetch_b1;
        }
        return(0);
      case 0xf2: // REPNE/REPNZ
        sse_prefix = SSE_PREFIX_F2;
        instruction->setRepUsed(b1 & 3);
        if (ilen < remain) {
          goto fetch_b1;
        }
        return(0);
      case 0xf3: // REP/REPE/REPZ
        sse_prefix = SSE_PREFIX_F3;
        instruction->setRepUsed(b1 & 3);
        if (ilen < remain) {
          goto fetch_b1;
        }
        return(0);
      case 0x2e: // CS:
        instruction->setSeg(BX_SEG_REG_CS);
        if (ilen < remain) {
          goto fetch_b1;
        }
        return(0);
      case 0x26: // ES:
        instruction->setSeg(BX_SEG_REG_ES);
        if (ilen < remain) {
          goto fetch_b1;
        }
        return(0);
      case 0x36: // SS:
        instruction->setSeg(BX_SEG_REG_SS);
        if (ilen < remain) {
          goto fetch_b1;
        }
        return(0);
      case 0x3e: // DS:
        instruction->setSeg(BX_SEG_REG_DS);
        if (ilen < remain) {
          goto fetch_b1;
        }
        return(0);
      case 0x64: // FS:
        instruction->setSeg(BX_SEG_REG_FS);
        if (ilen < remain) {
          goto fetch_b1;
        }
        return(0);
      case 0x65: // GS:
        instruction->setSeg(BX_SEG_REG_GS);
        if (ilen < remain) {
          goto fetch_b1;
        }
        return(0);
      case 0xf0: // LOCK:
        lock = 1;
        if (ilen < remain) {
          goto fetch_b1;
        }
        return(0);
      default:
        BX_PANIC(("fetch_decode: prefix default = 0x%02x", b1));
        return(0);
    }
  }

  // handle 2-byte escape
  if (b1 == 0x0f) {
    if (ilen < remain) {
      ilen++;
      b1 = 0x100 | *iptr++;
    }
    else
      return(0);
  }

  attr = BxOpcodeInfo[b1+offset].Attr;

  if (BxOpcodeHasModrm32[b1]) {

#if (BX_SUPPORT_SSE >= 4) || (BX_SUPPORT_SSE >= 3 && BX_SUPPORT_SSE_EXTENSION > 0)
    unsigned b3 = 0;
    // handle 3-byte escape
    if (attr & Bx3ByteOpcode) {
      if (ilen < remain) {
        ilen++;
        b3 = *iptr++;
      }
      else
        return(0);
  }
#endif

    // opcode requires modrm byte
    if (ilen < remain) {
      ilen++;
      b2 = *iptr++;
    }
    else
      return(0);

    // Parse mod-nnn-rm and related bytes
    mod = b2 & 0xc0; // leave unshifted
    nnn = (b2 >> 3) & 0x07;
    rm  = b2 & 0x07;

    // MOVs with CRx and DRx always use register ops and ignore the mod field.
    if ((b1 & ~3) == 0x120)
      mod = 0xc0;

    instruction->modRMForm.modRMData1 = rm;
    instruction->modRMForm.modRMData2 = mod;
    instruction->modRMForm.modRMData3 = 0;
    instruction->modRMForm.modRMData4 = nnn;

    if (mod == 0xc0) { // mod == 11b
      instruction->assertModC0();
      goto modrm_done;
    }

    if (instruction->as32L()) {
      // 32-bit addressing modes; note that mod==11b handled above
      if (rm != 4) { // no s-i-b byte
        if (mod == 0x00) { // mod == 00b
          instruction->ResolveModrm = Resolve32Mod0[rm];
          if (BX_NULL_SEG_REG(instruction->seg()))
            instruction->setSeg(BX_SEG_REG_DS);
          if (rm == 5) {
            if ((ilen+3) < remain) {
              instruction->modRMForm.displ32u = FetchDWORD(iptr);
              iptr += 4;
              ilen += 4;
              goto modrm_done;
            }
            else return(0);
          }
          // mod==00b, rm!=4, rm!=5
          goto modrm_done;
        }
        if (mod == 0x40) { // mod == 01b
          instruction->ResolveModrm = BxResolve32Mod1or2Rm;
          if (BX_NULL_SEG_REG(instruction->seg()))
            instruction->setSeg(BX_CPU_THIS_PTR sreg_mod01or10_rm32[rm]);
get_8bit_displ:
          if (ilen < remain) {
            // 8 sign extended to 32
            instruction->modRMForm.displ32u = (Bit8s) *iptr++;
            ilen++;
            goto modrm_done;
          }
          else return(0);
        }
        // (mod == 0x80) mod == 10b
        instruction->ResolveModrm = BxResolve32Mod1or2Rm;
        if (BX_NULL_SEG_REG(instruction->seg()))
          instruction->setSeg(BX_CPU_THIS_PTR sreg_mod01or10_rm32[rm]);
get_32bit_displ:
        if ((ilen+3) < remain) {
          instruction->modRMForm.displ32u = FetchDWORD(iptr);
          iptr += 4;
          ilen += 4;
          goto modrm_done;
        }
        else return(0);
      }
      else { // mod!=11b, rm==4, s-i-b byte follows
        unsigned sib, base, index, scale;
        if (ilen < remain) {
          sib = *iptr++;
          ilen++;
        }
        else {
          return(0);
        }
        base  = sib & 0x07; sib >>= 3;
        index = sib & 0x07; sib >>= 3;
        scale = sib;
        instruction->modRMForm.modRMData3 |= (base);
        instruction->modRMForm.modRMData2 |= (index);
        instruction->modRMForm.modRMData2 |= (scale<<4);
        if (mod == 0x00) { // mod==00b, rm==4
          if (index == 4)
            instruction->ResolveModrm = Resolve32Mod0Base[base];
          else
            instruction->ResolveModrm = Resolve32Mod0BaseIndex[base];
          if (BX_NULL_SEG_REG(instruction->seg()))
            instruction->setSeg(BX_CPU_THIS_PTR sreg_mod0_base32[base]);
          if (base == 0x05)
            goto get_32bit_displ;
          // mod==00b, rm==4, base!=5
          goto modrm_done;
        }
        if (mod == 0x40) { // mod==01b, rm==4
          if (index == 4)
            instruction->ResolveModrm = BxResolve32Mod1or2Base;
          else
            instruction->ResolveModrm = BxResolve32Mod1or2BaseIndex;
          if (BX_NULL_SEG_REG(instruction->seg()))
            instruction->setSeg(BX_CPU_THIS_PTR sreg_mod1or2_base32[base]);
          goto get_8bit_displ;
        }
        // (mod == 0x80),  mod==10b, rm==4
        if (index == 4)
          instruction->ResolveModrm = BxResolve32Mod1or2Base;
        else
          instruction->ResolveModrm = BxResolve32Mod1or2BaseIndex;
        if (BX_NULL_SEG_REG(instruction->seg()))
          instruction->setSeg(BX_CPU_THIS_PTR sreg_mod1or2_base32[base]);
        goto get_32bit_displ;
      }
    }
    else {
      // 16-bit addressing modes, mod==11b handled above
      if (mod == 0x40) { // mod == 01b
        instruction->ResolveModrm = Resolve16Mod1or2[rm];
        if (BX_NULL_SEG_REG(instruction->seg()))
          instruction->setSeg(BX_CPU_THIS_PTR sreg_mod01or10_rm16[rm]);
        if (ilen < remain) {
          // 8 sign extended to 16
          instruction->modRMForm.displ16u = (Bit8s) *iptr++;
          ilen++;
          goto modrm_done;
        }
        else return(0);
      }
      if (mod == 0x80) { // mod == 10b
        instruction->ResolveModrm = Resolve16Mod1or2[rm];
        if (BX_NULL_SEG_REG(instruction->seg()))
          instruction->setSeg(BX_CPU_THIS_PTR sreg_mod01or10_rm16[rm]);
        if ((ilen+1) < remain) {
          instruction->modRMForm.displ16u = FetchWORD(iptr);
          iptr += 2;
          ilen += 2;
          goto modrm_done;
        }
        else return(0);
      }
      // mod must be 00b at this point
      instruction->ResolveModrm = Resolve16Mod0[rm];
      if (BX_NULL_SEG_REG(instruction->seg()))
        instruction->setSeg(BX_CPU_THIS_PTR sreg_mod00_rm16[rm]);
      if (rm == 0x06) {
        if ((ilen+1) < remain) {
          instruction->modRMForm.displ16u = FetchWORD(iptr);
          iptr += 2;
          ilen += 2;
          goto modrm_done;
        }
        else return(0);
      }
      // mod=00b rm!=6
    }

modrm_done:

    // Resolve ExecutePtr and additional opcode Attr
    const BxOpcodeInfo_t *OpcodeInfoPtr = &(BxOpcodeInfo[b1+offset]);
    while(attr & BxGroupX) 
    {
       Bit32u Group = attr & BxGroupX;
       attr &= ~BxGroupX;
      
       switch(Group) {
         case BxGroupN:
             OpcodeInfoPtr = &(OpcodeInfoPtr->AnotherArray[nnn]);
             break;
         case BxRMGroup:
             OpcodeInfoPtr = &(OpcodeInfoPtr->AnotherArray[rm]);
             break;
#if (BX_SUPPORT_SSE >= 4) || (BX_SUPPORT_SSE >= 3 && BX_SUPPORT_SSE_EXTENSION > 0)
         case Bx3ByteOpTable:
             OpcodeInfoPtr = &(OpcodeInfoPtr->AnotherArray[b3 >> 4]);
             break;
         case Bx3ByteOpIndex:
             OpcodeInfoPtr = &(OpcodeInfoPtr->AnotherArray[b3 & 15]);
             break;
#endif
         case BxPrefixSSE:
             /* For SSE opcodes, look into another 4 entries table 
                      with the opcode prefixes (NONE, 0x66, 0xF2, 0xF3) */
             BX_ASSERT(sse_prefix < 4);
             OpcodeInfoPtr = &(OpcodeInfoPtr->AnotherArray[sse_prefix]);
             break;
         case BxSplitMod11b:
             /* For high frequency opcodes, two variants of the instruction are
              * implemented; one for the mod=11b case (Reg-Reg), and one for
              * the other cases (Reg-Mem).  If this is one of those cases,
              * we need to dereference to get to the execute pointer.
              */
             OpcodeInfoPtr = &(OpcodeInfoPtr->AnotherArray[mod==0xc0]);
             break;
#if BX_SUPPORT_FPU
         case BxFPGroup:
             if (mod != 0xc0)  // mod != 11b
                OpcodeInfoPtr = &(OpcodeInfoPtr->AnotherArray[nnn]);
             else {
                int index = (b1-0xD8)*64 + (0x3f & b2);
                OpcodeInfoPtr = &(BxOpcodeInfo_FloatingPoint[index]);
             }
             break;
#endif
         default:
             BX_PANIC(("fetchdecode: Unknown opcode group"));
       }

       /* get additional attributes from group table */
       attr |= OpcodeInfoPtr->Attr;
    }

    instruction->execute = OpcodeInfoPtr->ExecutePtr;
  }
  else {
    // Opcode does not require a MODRM byte.
    // Note that a 2-byte opcode (0F XX) will jump to before
    // the if() above after fetching the 2nd byte, so this path is
    // taken in all cases if a modrm byte is NOT required.
    instruction->execute = BxOpcodeInfo[b1+offset].ExecutePtr;
    instruction->IxForm.opcodeReg = b1 & 7;
  }

  if (lock) { // lock prefix invalid opcode
    // lock prefix not allowed or destination operand is not memory
    if ((mod == 0xc0) || !(attr & BxLockable)) {
      BX_INFO(("LOCK prefix unallowed (op1=0x%x, attr=0x%x, mod=0x%x, nnn=%u)", b1, attr, mod, nnn));
      UndefinedOpcode(instruction);
    }
  }

  imm_mode = attr & BxImmediate;
  if (imm_mode) {
    switch (imm_mode) {
      case BxImmediate_Ib:
        if (ilen < remain) {
          instruction->modRMForm.Ib = *iptr;
          ilen++;
        }
        else {
          return(0);
        }
        break;
      case BxImmediate_Ib_SE: // Sign extend to OS size
        if (ilen < remain) {
          Bit8s temp8s = *iptr;
          if (instruction->os32L())
            instruction->modRMForm.Id = (Bit32s) temp8s;
          else
            instruction->modRMForm.Iw = (Bit16s) temp8s;
          ilen++;
        }
        else {
          return(0);
        }
        break;
      case BxImmediate_Iv: // same as BxImmediate_BrOff32
      case BxImmediate_IvIw: // CALL_Ap
        if (instruction->os32L()) {
          if ((ilen+3) < remain) {
            instruction->modRMForm.Id = FetchDWORD(iptr);
            iptr += 4;
            ilen += 4;
          }
          else return(0);
        }
        else {
          if ((ilen+1) < remain) {
            instruction->modRMForm.Iw = FetchWORD(iptr);
            iptr += 2;
            ilen += 2;
          }
          else return(0);
        }
        if (imm_mode != BxImmediate_IvIw)
          break;
        // Get Iw for BxImmediate_IvIw
        if ((ilen+1) < remain) {
          instruction->IxIxForm.Iw2 = FetchWORD(iptr);
          ilen += 2;
        }
        else {
          return(0);
        }
        break;
      case BxImmediate_O:
        // For instructions which embed the address in the opcode.
        if (instruction->as32L()) {
          // fetch 32bit address into Id
          if ((ilen+3) < remain) {
            instruction->modRMForm.Id = FetchDWORD(iptr);
            ilen += 4;
          }
          else return(0);
        }
        else {
          // fetch 16bit address into Id
          if ((ilen+1) < remain) {
            instruction->modRMForm.Id = (Bit32u) FetchWORD(iptr);
            ilen += 2;
          }
          else return(0);
        }
        break;
      case BxImmediate_Iw:
      case BxImmediate_IwIb:
        if ((ilen+1) < remain) {
          instruction->modRMForm.Iw = FetchWORD(iptr);
          iptr += 2;
          ilen += 2;
        }
        else {
          return(0);
        }
        if (imm_mode == BxImmediate_Iw) break;
        if (ilen < remain) {
          instruction->IxIxForm.Ib2 = *iptr;
          ilen++;
        }
        else {
          return(0);
        }
        break;
      case BxImmediate_BrOff8:
        if (ilen < remain) {
          Bit8s temp8s = *iptr;
          instruction->modRMForm.Id = temp8s;
          ilen++;
        }
        else {
          return(0);
        }
        break;
      case BxImmediate_BrOff16:
        if ((ilen+1) < remain) {
          instruction->modRMForm.Id = (Bit16s) FetchWORD(iptr);
          ilen += 2;
        }
        else {
          return(0);
        }
        break;
      case BxImmediate_BrOff32:
        if ((ilen+3) < remain) {
          instruction->modRMForm.Id = (Bit32s) FetchDWORD(iptr);
          ilen += 4;
        }
        else {
          return(0);
        }
        break;
      default:
        BX_INFO(("b1 was %x", b1));
        BX_PANIC(("fetchdecode: imm_mode = %u", imm_mode));
    }
  }

#if BX_SUPPORT_3DNOW
  if(b1 == 0x10f)
     instruction->execute = Bx3DNowOpcodeInfo[instruction->modRMForm.Ib].ExecutePtr;
#endif

  if (BX_NULL_SEG_REG(instruction->seg()))
     instruction->setSeg(BX_SEG_REG_DS);

  instruction->setB1(b1);
  instruction->setILen(ilen);
  return(1);
}

void BX_CPU_C::BxError(bxInstruction_c *i)
{
  BX_INFO(("BxError: instruction with opcode=0x%x", i->b1()));
  BX_INFO(("mod was %x, nnn was %u, rm was %u", i->mod(), i->nnn(), i->rm()));
  BX_INFO(("WARNING: Encountered an unknown instruction (signalling illegal instruction)"));

  BX_CPU_THIS_PTR UndefinedOpcode(i);
}
