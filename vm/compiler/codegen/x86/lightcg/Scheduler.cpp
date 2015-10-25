/*
 * Copyright (C) 2010-2013 Intel Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*! \file Scheduler.cpp
    \brief This file implements the Atom Instruction Scheduler.
    \details Scheduling algorithm implemented is basic block scheduling.
*/

#include "CompilationErrorLCG.h"
#include "CompilationUnit.h"
#include "Lower.h"
#include "interp/InterpDefs.h"
#include "Scheduler.h"
#include "Utility.h"

//! \def DISABLE_ATOM_SCHEDULING_STATISTICS
//! \brief Disables printing of scheduling statistics.
//! \details Defining this macro disables printing of scheduling statistics pre
//! and post scheduling. Undefine macro when statistics are needed.
#define DISABLE_ATOM_SCHEDULING_STATISTICS

//! \def DISABLE_DEBUG_ATOM_SCHEDULER
//! \brief Disables debug printing for atom scheduler.
//! \details Defining macro DISABLE_DEBUG_ATOM_SCHEDULER disables debug printing.
//! Undefine macro when debugging scheduler implementation.
#define DISABLE_DEBUG_ATOM_SCHEDULER

//! \def DISABLE_DEPENDENGY_GRAPH_DEBUG
//! \brief Disables printing of dependency graph
//! \details Undefine this macro when wanting to debug dependency graph.
//! The dot files for each basic block will be dumped to folder /data/local/tmp
//! and the name for each file will be depengraph_<pid>_<stream_start_of_BB>.dot
#define DISABLE_DEPENDENGY_GRAPH_DEBUG

#ifndef DISABLE_DEPENDENGY_GRAPH_DEBUG
#include <fstream>
#include <sstream>
#include <string>
#include <set>
#endif

//! \enum IssuePort
//! \brief Defines possible combinations of port-binding information for use
//! with information about each x86 mnemonic.
enum IssuePort {
    //! \brief invalid port, used for table only when some
    //! operands are not supported for the mnemonic
    INVALID_PORT = -1,
    //! \brief the mnemonic can only be issued on port 0
    PORT0 = 0,
    //! \brief the mnemonic can only be issued on port 1
    PORT1 = 1,
    //! \brief the mnemonic can be issued on either port
    EITHER_PORT,
    //! \brief both ports are used for the mnemonic
    BOTH_PORTS
};

#ifndef DISABLE_DEPENDENGY_GRAPH_DEBUG
//! \brief Transforms from IssuePort enum to a string representation.
inline const char * getIssuePort(IssuePort port) {
    switch (port) {
    case INVALID_PORT:
        return "invalid";
    case PORT0:
        return "0";
    case PORT1:
        return "1";
    case EITHER_PORT:
        return "either";
    case BOTH_PORTS:
        return "both";
    }
    return "Invalid";
}
#endif

//! \class MachineModelEntry
//! \brief Information needed to define the machine model for each x86 mnemonic.
struct MachineModelEntry {
    //! \brief which port the instruction can execute on
    IssuePort issuePortType;
    //! \brief execute to execute time for one instruction
    int executeToExecuteLatency;
};

//! \def INVP
//! \brief This is an abbreviation of INVALID_PORT and is used for readability
//! reasons.
#define INVP INVALID_PORT

//! \def INVN
//! \brief This is an abbreviation of invalid node latency and is used for
//! readability reasons.
#define INVN 0

//! \def REG_NOT_USED
//! \brief This is an abbreviation for register not used and is used for
//! readability reasons whenever a Scheduler method needs register type
//! to update some data structure but a register number does not make
//! sense in the context.
#define REG_NOT_USED -1

//! \brief This table lists the parameters for each Mnemonic in the Atom Machine Model.
//! \details This table includes port and latency information for each mnemonic for each possible
//! configuration of operands. 6 entries of MachineModelEntry are reserved for each Mnemonic:
//! - If a Mnemonic has zero operand, only the first entry is valid
//! - If a Mnemonic has a single operand, the first 3 entries are valid, for operand type
//! imm, reg and mem respectively
//! - If a Mnemonic has two operands, the last 5 entries are valid, for operand types
//! imm_to_reg, imm_to_mem, reg_to_reg, mem_to_reg and reg_to_mem
//! - If a Mnemonic has three operands imm_reg_reg, we use the same slot as the reg_to_reg
//! field for two operands.
//!
//! This table matches content from Intel 64 and IA-32 Architectures Optimization
//! Reference Manual (April 2012), Section 13.4
//!
//! This table contains SSE4 instructions not supported on Saltwell. For example, PEXTRD is not
//! supported. This table is commonly used for all Atom even though some don't have same ISA support.
//! In case of PEXTRD for this table we just used PEXTRW data (because it is supported on Saltwell).
//! At some point, it makes sense separating different processors into different models with separate tables.
//!
//! \warning This table is not complete and if new mnemonics are used that do not have an
//! entry, then the schedule selection will not be optimal.
MachineModelEntry atomMachineModel[Mnemonic_Count*6] = {
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //NULL, Null

    {PORT1,1},{PORT1,1},{BOTH_PORTS,2},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //JMP

    {INVP,INVN},{EITHER_PORT,1},{PORT0,1},{EITHER_PORT,1},{PORT0,1},{PORT0,1}, //MOV

    {PORT1,1},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //Jcc_O
    {PORT1,1},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //Jcc_NO
    {PORT1,1},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //Jcc_B
    {PORT1,1},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //Jcc_NB
    {PORT1,1},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //Jcc_Z
    {PORT1,1},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //Jcc_NZ
    {PORT1,1},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //Jcc_BE
    {PORT1,1},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //Jcc_NBE
    {PORT1,1},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //Jcc_S
    {PORT1,1},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //Jcc_NS
    {PORT1,1},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //Jcc_P
    {PORT1,1},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //Jcc_NP
    {PORT1,1},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //Jcc_L
    {PORT1,1},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //Jcc_NL
    {PORT1,1},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //Jcc_LE
    {PORT1,1},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //Jcc_NLE

    {BOTH_PORTS,1},{BOTH_PORTS,1},{EITHER_PORT,2},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //CALL

    {INVP,INVN},{EITHER_PORT,1},{PORT0,1},{EITHER_PORT,1},{PORT0,1},{PORT0,1}, //ADC
    {INVP,INVN},{EITHER_PORT,1},{PORT0,1},{EITHER_PORT,1},{PORT0,1},{PORT0,1}, //ADD
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{PORT1,5},{BOTH_PORTS,5},{INVP,INVN}, //ADDSD
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{PORT1,5},{BOTH_PORTS,5},{INVP,INVN}, //ADDSS
    {INVP,INVN},{EITHER_PORT,1},{PORT0,1},{EITHER_PORT,1},{PORT0,1},{PORT0,1}, //AND
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //BSF
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //BSR
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //CMC
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{PORT0,1},{INVP,INVN},{INVP,INVN}, //CWD, CDQ

    {INVP,INVN},{INVP,INVN},{INVP,INVN},{EITHER_PORT,1},{PORT0,1},{INVP,INVN}, //CMOV_O
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{EITHER_PORT,1},{PORT0,1},{INVP,INVN}, //CMOV_NO
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{EITHER_PORT,1},{PORT0,1},{INVP,INVN}, //CMOV_B,NAE,C
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{EITHER_PORT,1},{PORT0,1},{INVP,INVN}, //CMOV_NB,AE,NC
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{EITHER_PORT,1},{PORT0,1},{INVP,INVN}, //CMOV_Z,E
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{EITHER_PORT,1},{PORT0,1},{INVP,INVN}, //CMOV_NZ,NE
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{EITHER_PORT,1},{PORT0,1},{INVP,INVN}, //CMOV_BE,NA
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{EITHER_PORT,1},{PORT0,1},{INVP,INVN}, //CMOV_NBE,A
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{EITHER_PORT,1},{PORT0,1},{INVP,INVN}, //CMOV_S
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{EITHER_PORT,1},{PORT0,1},{INVP,INVN}, //CMOV_NS
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{EITHER_PORT,1},{PORT0,1},{INVP,INVN}, //CMOV_P,PE
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{EITHER_PORT,1},{PORT0,1},{INVP,INVN}, //CMOV_NP,PO
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{EITHER_PORT,1},{PORT0,1},{INVP,INVN}, //CMOV_L,NGE
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{EITHER_PORT,1},{PORT0,1},{INVP,INVN}, //CMOV_NL,GE
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{EITHER_PORT,1},{PORT0,1},{INVP,INVN}, //CMOV_LE,NG
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{EITHER_PORT,1},{PORT0,1},{INVP,INVN}, //CMOV_NLE,G

    {INVP,INVN},{EITHER_PORT,1},{PORT0,1},{EITHER_PORT,1},{PORT0,1},{PORT0,1}, //CMP
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{PORT0,5},{PORT0,5}, //CMPXCHG Note: info missed in section 13.4
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{PORT0,5},{PORT0,5}, //CMPXCHG8B Note: info missed in section 13.4
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //CMPSB
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //CMPSW
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //CMPSD

    {INVP,INVN},{INVP,INVN},{INVP,INVN},{BOTH_PORTS,7},{BOTH_PORTS,8},{INVP,INVN}, //CVTSD2SS
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{BOTH_PORTS,8},{BOTH_PORTS,9},{INVP,INVN}, //CVTSD2SI
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{BOTH_PORTS,8},{BOTH_PORTS,9},{INVP,INVN}, //CVTTSD2SI
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{BOTH_PORTS,7},{BOTH_PORTS,8},{INVP,INVN}, //CVTSS2SD
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{BOTH_PORTS,9},{BOTH_PORTS,10},{INVP,INVN}, //CVTSS2SI
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{BOTH_PORTS,9},{BOTH_PORTS,10},{INVP,INVN}, //CVTTSS2SI
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{BOTH_PORTS,7},{BOTH_PORTS,8},{INVP,INVN}, //CVTSI2SD
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{BOTH_PORTS,6},{BOTH_PORTS,7},{INVP,INVN}, //CVTSI2SS

    {INVP,INVN},{BOTH_PORTS,9},{INVP,INVN},{BOTH_PORTS,9},{BOTH_PORTS,10},{INVP,INVN}, //COMISD
    {INVP,INVN},{BOTH_PORTS,9},{INVP,INVN},{BOTH_PORTS,9},{BOTH_PORTS,10},{INVP,INVN}, //COMISS
    {INVP,INVN},{EITHER_PORT,1},{PORT0,1},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //DEC
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{BOTH_PORTS,62},{BOTH_PORTS,62},{INVP,INVN}, //DIVSD
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{BOTH_PORTS,34},{BOTH_PORTS,34},{INVP,INVN}, //DIVSS

    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //ENTER
    {INVP,INVN},{INVP,INVN},{BOTH_PORTS,5},{INVP,INVN},{BOTH_PORTS,5},{INVP,INVN}, //FLDCW
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{PORT1,5},{INVP,INVN}, //FADDP
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //FLDZ
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{PORT1,5},{INVP,INVN}, //FADD
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{PORT1,5},{INVP,INVN}, //FSUBP
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{PORT1,5},{INVP,INVN}, //FSUB
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //FISUB
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{PORT0,5},{INVP,INVN}, //FMUL
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{PORT0,5},{INVP,INVN}, //FMULP
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{PORT0,65},{INVP,INVN}, //FDIVP
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{PORT0,65},{INVP,INVN}, //FDIV
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{PORT1,1},{INVP,INVN},{INVP,INVN}, //FUCOM
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{PORT1,1},{INVP,INVN},{INVP,INVN}, //FUCOMI
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{PORT1,1},{INVP,INVN},{INVP,INVN}, //FUCOMP
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{PORT1,1},{INVP,INVN},{INVP,INVN}, //FUCOMIP
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //FUCOMPP
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //FRNDINT
    {INVP,INVN},{INVP,INVN},{BOTH_PORTS,5},{INVP,INVN},{INVP,INVN},{BOTH_PORTS,5}, //FNSTCW
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //FSTSW
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //FNSTSW
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{BOTH_PORTS,5},{INVP,INVN}, //FILD
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{PORT0,1},{INVP,INVN}, //FLD
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //FLDLG2
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //FLDLN2
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //FLD1

    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //FCLEX
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //FCHS
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //FNCLEX
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{EITHER_PORT,6}, //FIST
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{EITHER_PORT,6}, //FISTP
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //FISTTP
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //FPREM
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //FPREM1
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{EITHER_PORT,1}, //FST fp_mem
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{EITHER_PORT,1}, //FSTP
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{PORT0,65},{INVP,INVN}, //FSQRT
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{EITHER_PORT,1},{INVP,INVN}, //FABS
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //FSIN
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //FCOS
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //FPTAN
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //FYL2X
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //FYL2XP1
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //F2XM1
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //FPATAN
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //FXCH
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //FSCALE

    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{PORT0,5},{PORT0,5}, //XCHG Note: info missed in section 14.4
    // There is no way to differentiate operand sizes in this table, so just assume 32-bit
    {INVP,INVN},{BOTH_PORTS,57},{BOTH_PORTS,57},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //DIV
    {INVP,INVN},{BOTH_PORTS,57},{BOTH_PORTS,57},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //IDIV
    {INVP,INVN},{BOTH_PORTS,6},{BOTH_PORTS,7},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //MUL
    // This table does not support IMUL with single reg or mem operand
    {INVP,INVN},{PORT0,5},{PORT0,5},{PORT0,5},{PORT0,5},{INVP,INVN}, //IMUL
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //INC
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //INT3

    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{PORT1,1},{INVP,INVN}, //LEA
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //LEAVE
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //LOOP
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //LOOPE
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //LOOPNE
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //LAHF

    {INVP,INVN},{INVP,INVN},{INVP,INVN},{PORT0,1},{PORT0,1},{PORT0,1}, //MOVD
    {INVP,INVN},{PORT0,1},{PORT0,1},{PORT0,1},{PORT0,1},{PORT0,1}, //MOVQ
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //MOVS8
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //MOVS16
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //MOVS32
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //MOVS64
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{PORT0,1},{PORT0,1}, //MOVAPD
    {INVP,INVN},{PORT0,1},{INVP,INVN},{EITHER_PORT,1},{PORT0,1},{PORT0,1}, //MOVSD
    {INVP,INVN},{PORT0,1},{INVP,INVN},{EITHER_PORT,1},{PORT0,1},{PORT0,1}, //MOVSS
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{PORT0,1},{PORT0,1},{INVP,INVN}, //MOVSX
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{PORT0,1},{PORT0,1},{INVP,INVN}, //MOVZX

    {INVP,INVN},{INVP,INVN},{INVP,INVN},{PORT0,5},{PORT0,5},{INVP,INVN}, //MULSD
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{PORT0,4},{PORT0,4},{INVP,INVN}, //MULSS
    {INVP,INVN},{EITHER_PORT,1},{PORT0,10},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //NEG
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //NOP
    {INVP,INVN},{EITHER_PORT,1},{PORT0,10},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //NOT
    {INVP,INVN},{EITHER_PORT,1},{PORT0,1},{EITHER_PORT,1},{PORT0,1},{PORT0,1}, //OR
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //PREFETCH

    {INVP,INVN},{INVP,INVN},{INVP,INVN},{EITHER_PORT,2},{EITHER_PORT,3},{INVP,INVN}, //PADDQ
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{EITHER_PORT,1},{PORT0,1},{INVP,INVN}, //PAND
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{EITHER_PORT,1},{PORT0,1},{INVP,INVN}, //POR
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{EITHER_PORT,2},{EITHER_PORT,3},{INVP,INVN}, //PSUBQ
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{EITHER_PORT,1},{PORT0,1},{INVP,INVN}, //PANDN
    {INVP,INVN},{EITHER_PORT,1},{INVP,INVN},{EITHER_PORT,2},{EITHER_PORT,3},{INVP,INVN}, //PSLLQ
    {INVP,INVN},{EITHER_PORT,1},{INVP,INVN},{EITHER_PORT,2},{EITHER_PORT,3},{INVP,INVN}, //PSRLQ
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{EITHER_PORT,1},{PORT0,1},{INVP,INVN}, //PXOR
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //POP
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //POPFD
    {INVP,INVN},{BOTH_PORTS,1},{BOTH_PORTS,2},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //PUSH
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //PUSHFD
    {BOTH_PORTS,1},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //RET

    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //SET_O
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //SET_NO
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //SET_B
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //SET_NB
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //SET_Z
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //SET_NZ
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //SET_BE
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //SET_NBE
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //SET_S
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //SET_NS
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //SET_P
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //SET_NP
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //SET_L
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //SET_NL
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //SET_LE
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //SET_NLE

    {INVP,INVN},{PORT0,1},{PORT0,1},{PORT0,1},{INVP,INVN},{INVP,INVN}, //SAL,SHL
    {INVP,INVN},{PORT0,1},{PORT0,1},{PORT0,1},{INVP,INVN},{INVP,INVN}, //SAR
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //ROR
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //RCR
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //ROL
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //RCL
    {INVP,INVN},{PORT0,1},{PORT0,1},{PORT0,1},{INVP,INVN},{INVP,INVN}, //SHR
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{BOTH_PORTS,4},{BOTH_PORTS,4},{BOTH_PORTS,2}, //SHRD
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{BOTH_PORTS,4},{BOTH_PORTS,4},{BOTH_PORTS,2}, //SHLD
    {INVP,INVN},{EITHER_PORT,1},{PORT0,1},{EITHER_PORT,1},{PORT0,1},{PORT0,1}, //SBB
    {INVP,INVN},{EITHER_PORT,1},{PORT0,1},{EITHER_PORT,1},{PORT0,1},{PORT0,1}, //SUB
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{PORT1,5},{BOTH_PORTS,5},{INVP,INVN}, //SUBSD
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{PORT1,5},{BOTH_PORTS,5},{INVP,INVN}, //SUBSS

    {INVP,INVN},{EITHER_PORT,1},{PORT0,1},{EITHER_PORT,1},{INVP,INVN},{PORT0,1}, //TEST
    {INVP,INVN},{BOTH_PORTS,9},{INVP,INVN},{BOTH_PORTS,9},{BOTH_PORTS,10},{INVP,INVN}, //UCOMISD
    {INVP,INVN},{BOTH_PORTS,9},{INVP,INVN},{BOTH_PORTS,9},{BOTH_PORTS,10},{INVP,INVN}, //UCOMISS
    {INVP,INVN},{EITHER_PORT,1},{PORT0,1},{EITHER_PORT,1},{PORT0,1},{PORT0,1}, //XOR
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{EITHER_PORT,1},{PORT0,1},{INVP,INVN}, //XORPD
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //XORPS
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //CVTDQ2PD
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //CVTTPD2DQ
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //CVTDQ2PS
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //CVTTPS2DQ
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //STD
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //CLD
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //SCAS
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //STOS
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //WAIT

    {INVP,INVN},{INVP,INVN},{INVP,INVN},{EITHER_PORT,1},{PORT0,1},{INVP,INVN}, //PADDB
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{EITHER_PORT,1},{PORT0,1},{INVP,INVN}, //PADDW
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{EITHER_PORT,1},{PORT0,1},{INVP,INVN}, //PADDD
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{EITHER_PORT,1},{PORT0,1},{INVP,INVN}, //PSUBB
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{EITHER_PORT,1},{PORT0,1},{INVP,INVN}, //PSUBW
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{EITHER_PORT,1},{PORT0,1},{INVP,INVN}, //PSUBD
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{PORT0,5},{PORT0,4},{INVP,INVN}, //PMULLW
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{PORT0,5},{PORT0,4},{INVP,INVN}, //PMULLD - SSE4.1 instruction
    {INVP,INVN},{PORT0,1},{INVP,INVN},{BOTH_PORTS,2},{BOTH_PORTS,3},{INVP,INVN}, //PSLLW
    {INVP,INVN},{PORT0,1},{INVP,INVN},{BOTH_PORTS,2},{BOTH_PORTS,3},{INVP,INVN}, //PSLLD
    {INVP,INVN},{PORT0,1},{INVP,INVN},{BOTH_PORTS,2},{BOTH_PORTS,3},{INVP,INVN}, //PSRAW
    {INVP,INVN},{PORT0,1},{INVP,INVN},{BOTH_PORTS,2},{BOTH_PORTS,3},{INVP,INVN}, //PSRAD
    {INVP,INVN},{PORT0,1},{INVP,INVN},{BOTH_PORTS,2},{BOTH_PORTS,3},{INVP,INVN}, //PSRLW
    {INVP,INVN},{PORT0,1},{INVP,INVN},{BOTH_PORTS,2},{BOTH_PORTS,3},{INVP,INVN}, //PSRLD
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{PORT0,3},{INVP,INVN},{INVP,INVN}, //PMOVSXBW - SSE4.1 instruction
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{PORT0,1},{INVP,INVN},{INVP,INVN}, //PSHUFB - SSE3 instruction
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{PORT0,1},{INVP,INVN},{INVP,INVN}, //PSHUFD - 3 operand instruction
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{PORT0,1},{INVP,INVN},{INVP,INVN}, //PSHUFLW - 3 operand instruction
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{PORT0,1},{INVP,INVN},{INVP,INVN}, //PSHUFHW - 3 operand instruction
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{BOTH_PORTS,7},{BOTH_PORTS,8},{INVP,INVN}, //PHADDSW - SSE3 instruction
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{BOTH_PORTS,7},{BOTH_PORTS,8},{INVP,INVN}, //PHADDW - SSE3 instruction
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{BOTH_PORTS,3},{BOTH_PORTS,4},{INVP,INVN}, //PHADDD - SSE3 instruction
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{BOTH_PORTS,7},{BOTH_PORTS,8},{INVP,INVN}, //PHSUBSW - SSE3 instruction
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{BOTH_PORTS,7},{BOTH_PORTS,8},{INVP,INVN}, //PHSUBW - SSE3 instruction
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{BOTH_PORTS,3},{BOTH_PORTS,4},{INVP,INVN}, //PHSUBD - SSE3 instruction
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{BOTH_PORTS,4},{INVP,INVN},{INVP,INVN}, //PEXTRB - SSE4.1 3 operand instruction
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{BOTH_PORTS,4},{INVP,INVN},{INVP,INVN}, //PEXTRW - 3 operand instruction
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{BOTH_PORTS,4},{INVP,INVN},{INVP,INVN}, //PEXTRD - SSE4.1 3 operand instruction
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{EITHER_PORT,1},{PORT0,1},{INVP,INVN}, //MOVDQA

    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //SHUFPS
    {INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN},{INVP,INVN}, //MOVAPS
};

//! \brief Get issue port for mnemonic with no operands
inline IssuePort getAtomMnemonicPort(Mnemonic m) {
    if (m >= Mnemonic_Count) return INVALID_PORT;
    return atomMachineModel[m*6].issuePortType;
}
//! \brief Get issue port for mnemonic with one immediate operand
inline IssuePort getAtomMnemonicPort_imm(Mnemonic m) {
    if (m >= Mnemonic_Count) return INVALID_PORT;
    return atomMachineModel[m*6].issuePortType;
}
//! \brief Get issue port for mnemonic with one register operand
inline IssuePort getAtomMnemonicPort_reg(Mnemonic m) {
    if (m >= Mnemonic_Count) return INVALID_PORT;
    return atomMachineModel[m*6+1].issuePortType;
}
//! \brief Get issue port for mnemonic with one memory operand
inline IssuePort getAtomMnemonicPort_mem(Mnemonic m) {
    if (m >= Mnemonic_Count) return INVALID_PORT;
    return atomMachineModel[m*6+2].issuePortType;
}
//! \brief Get issue port for mnemonic with two operands: immediate to register
inline IssuePort getAtomMnemonicPort_imm_to_reg(Mnemonic m) {
    if (m >= Mnemonic_Count) return INVALID_PORT;
    return atomMachineModel[m*6+1].issuePortType;
}
//! \brief Get issue port for mnemonic with two operands: immediate to memory
inline IssuePort getAtomMnemonicPort_imm_to_mem(Mnemonic m) {
    if (m >= Mnemonic_Count) return INVALID_PORT;
    return atomMachineModel[m*6+2].issuePortType;
}
//! \brief Get issue port for mnemonic with two operands: register to register
inline IssuePort getAtomMnemonicPort_reg_to_reg(Mnemonic m) {
    if (m >= Mnemonic_Count) return INVALID_PORT;
    return atomMachineModel[m*6+3].issuePortType;
}
//! \brief Get issue port for mnemonic with two operands: memory to register
inline IssuePort getAtomMnemonicPort_mem_to_reg(Mnemonic m) {
    if (m >= Mnemonic_Count) return INVALID_PORT;
    return atomMachineModel[m*6+4].issuePortType;
}
//! \brief Get issue port for mnemonic with two operands: register to memory
inline IssuePort getAtomMnemonicPort_reg_to_mem(Mnemonic m) {
    if (m >= Mnemonic_Count) return INVALID_PORT;
    return atomMachineModel[m*6+5].issuePortType;
}

//! \brief Get execute to execute latency for mnemonic with no operands
inline int getAtomMnemonicLatency(Mnemonic m) {
    if (m >= Mnemonic_Count) return INVN;
    return atomMachineModel[m*6].executeToExecuteLatency;
}
//! \brief Get execute to execute latency for mnemonic with one immediate operand
inline int getAtomMnemonicLatency_imm(Mnemonic m) {
    if (m >= Mnemonic_Count) return INVN;
    return atomMachineModel[m*6].executeToExecuteLatency;
}
//! \brief Get execute to execute latency for mnemonic with one register operand
inline int getAtomMnemonicLatency_reg(Mnemonic m) {
    if (m >= Mnemonic_Count) return INVN;
    return atomMachineModel[m*6+1].executeToExecuteLatency;
}
//! \brief Get execute to execute latency for mnemonic with one memory operand
inline int getAtomMnemonicLatency_mem(Mnemonic m) {
    if (m >= Mnemonic_Count) return INVN;
    return atomMachineModel[m*6+2].executeToExecuteLatency;
}
//! \brief Get execute to execute latency for mnemonic with two operands: immediate to register
inline int getAtomMnemonicLatency_imm_to_reg(Mnemonic m) {
    if (m >= Mnemonic_Count) return INVN;
    return atomMachineModel[m*6+1].executeToExecuteLatency;
}
//! \brief Get execute to execute latency for mnemonic with two operands: immediate to memory
inline int getAtomMnemonicLatency_imm_to_mem(Mnemonic m) {
    if (m >= Mnemonic_Count) return INVN;
    return atomMachineModel[m*6+2].executeToExecuteLatency;
}
//! \brief Get execute to execute latency for mnemonic with two operands: register to register
inline int getAtomMnemonicLatency_reg_to_reg(Mnemonic m) {
    if (m >= Mnemonic_Count) return INVN;
    return atomMachineModel[m*6+3].executeToExecuteLatency;
}
//! \brief Get execute to execute latency for mnemonic with two operands: memory to register
inline int getAtomMnemonicLatency_mem_to_reg(Mnemonic m) {
    if (m >= Mnemonic_Count) return INVN;
    return atomMachineModel[m*6+4].executeToExecuteLatency;
}
//! \brief Get execute to execute latency for mnemonic with two operands: register to memory
inline int getAtomMnemonicLatency_reg_to_mem(Mnemonic m) {
    if (m >= Mnemonic_Count) return INVN;
    return atomMachineModel[m*6+5].executeToExecuteLatency;
}

#ifndef DISABLE_DEBUG_ATOM_SCHEDULER
//! \brief Transforms from LowOpndDefUse enum to string representation of the usedef
//! \see LowOpndDefUse
inline const char * getUseDefType(LowOpndDefUse defuse) {
    switch (defuse) {
    case LowOpndDefUse_Def:
        return "Def";
    case LowOpndDefUse_Use:
        return "Use";
    case LowOpndDefUse_UseDef:
        return "UseDef";
    }
    return "-";
}
#endif

#ifndef DISABLE_DEBUG_ATOM_SCHEDULER
//! \brief Transforms from UseDefEntryType enum to a string representation.
//! \see UseDefEntryType
inline const char * getUseDefEntryType(UseDefEntryType type) {
    switch (type) {
    case UseDefType_Ctrl:
        return "Ctrl";
    case UseDefType_Float:
        return "Float";
    case UseDefType_MemVR:
        return "MemVR";
    case UseDefType_MemSpill:
        return "MemSpill";
    case UseDefType_MemUnknown:
        return "MemUnknown";
    case UseDefType_Reg:
        return "Reg";
    }
    return "-";
}
#endif

//! \brief Returns true if mnemonic is a variant of MOV including XCHG.
inline bool isMoveMnemonic(Mnemonic m) {
    return (m == Mnemonic_MOV || m == Mnemonic_MOVQ || m == Mnemonic_MOVSD || m == Mnemonic_MOVSS || m == Mnemonic_MOVZX
            || m == Mnemonic_MOVSX || m == Mnemonic_MOVAPD || m == Mnemonic_MOVDQA || m == Mnemonic_MOVD || m == Mnemonic_XCHG);
}

//! \brief Returns true if mnemonic is used for comparisons.
//! \details Returns false for FPU comparison mnemonics.
inline bool isCompareMnemonic(Mnemonic m) {
    return m == Mnemonic_CMP || m == Mnemonic_COMISD || m == Mnemonic_COMISS
            || m == Mnemonic_TEST;
}

//! \brief Returns true if mnemonic is SSE conversion routine
inline bool isConvertMnemonic(Mnemonic m) {
    return m == Mnemonic_CVTSD2SS || m == Mnemonic_CVTSD2SI
            || m == Mnemonic_CVTTSD2SI || m == Mnemonic_CVTSS2SD
            || m == Mnemonic_CVTSS2SI || m == Mnemonic_CVTTSS2SI
            || m == Mnemonic_CVTSI2SD || m == Mnemonic_CVTSI2SS;
}

//! \brief Returns true if the mnemonic is a XMM shuffle operation
//! \param m The Mnemonic to check
//! \return whether it is a shuffle operation
inline bool isShuffleMnemonic (Mnemonic m)
{
    return m == Mnemonic_PSHUFD || m == Mnemonic_PSHUFHW || m == Mnemonic_PSHUFLW || m == Mnemonic_PSHUFB;
}

//! \brief Returns true if mnemonic uses and then defines the FLAGS register
inline bool usesAndDefinesFlags(Mnemonic m) {
    return m == Mnemonic_ADC || m == Mnemonic_SBB;
}

//! \brief Returns true if mnemonic is CMPXCHG, which use and define EAX register
inline bool isCmpxchgMnemonic(Mnemonic m) {
    return m == Mnemonic_CMPXCHG;
}

//! \brief Returns true if ALU mnemonic has a variant that has implicit
//! register usage.
//! \details Returns true for div, idiv, mul, imul, and cdq. However, note
//! that implicit register usage is dependent on variant being used. For example,
//! only idiv with single reg operand has implicit register usage.
inline bool isAluOpWithImplicitRegisterUsage(Mnemonic m) {
    return m == Mnemonic_DIV || m == Mnemonic_IDIV
            || m == Mnemonic_IMUL || m == Mnemonic_MUL
            || m == Mnemonic_CDQ;
}

//! \brief Detects whether the mnemonic is a native basic block delimiter.
//! \details Unconditional jumps, conditional jumps, calls, and returns
//! always end a native basic block.
inline bool Scheduler::isBasicBlockDelimiter(Mnemonic m) {
    return (m == Mnemonic_JMP || m == Mnemonic_CALL
            || (m >= Mnemonic_Jcc && m <= Mnemonic_JG) || m == Mnemonic_RET);
}

//! \details Defines a mapping between the reason for edge latencies between
//! instructions and the actual latency value.
//! \see LatencyBetweenNativeInstructions
static int mapLatencyReasonToValue[] = {
    // Latency_None
    0,
    // Latency_Agen_stall
    3,
    //Latency_Load_blocked_by_store
    0,
    //Latency_Memory_Load
    0,
};

//! \brief Atom scheduler destructor
Scheduler::~Scheduler(void) {
    // Clear all scheduler data structures
    this->reset();
}

//! \brief Resets data structures used by Scheduler
void Scheduler::reset(void) {
    queuedLIREntries.clear();
    scheduledLIREntries.clear();

    for (std::vector<UseDefUserEntry>::iterator it = userEntries.begin();
            it != userEntries.end(); ++it) {
        it->useSlotsList.clear();
    }
    userEntries.clear();

    for (std::map<LowOp*, Dependencies>::iterator it =
            dependencyAssociation.begin(); it != dependencyAssociation.end();
            it++) {
        Dependencies &d = it->second;
        d.predecessorDependencies.clear();
        d.successorDependencies.clear();
    }
    dependencyAssociation.clear();

    // Safe to clear
    producerEntries.clear();
    ctrlEntries.clear();
}

//! \brief Returns true if Scheduler has no LIRs in its
//! queue for scheduling.
//! \return true when Scheduler queue is empty
bool Scheduler::isQueueEmpty() const {
    return queuedLIREntries.empty();
}

//! \brief Given an access to a resource (Control, register, VR, unknown memory
//! access), this updates dependency graph, usedef information, and control flags.
//! \details Algorithm description for dependency update:
//! - for Use or UseDef:
//!   -# insert RAW dependency from producer for this op
//! - for Def or UseDef:
//!   -# insert WAR dependency from earlier user for this op
//!   -# insert WAW dependency from earlier producer for this op
//! - Internal data structure updates for record keeping:
//!   -# for Def or UseDef: update producerEntries
//!   -# for Def: clear corresponding use slots for entry in userEntries
//!   -# for UseDef: clear corresponding use slots for entry in userEntries
//!   -# for Use: update userEntries
//!
//! \param type resource that causes dependency
//! \param regNum is a number corresponding to a physical register or a Dalvik
//! virtual register. When physical, this is of enum type PhysicalReg.
//! \param defuse definition, usage, or both
//! \param causeOfLatency Weight to use on the edge.
//! \param op LIR for which to update dependencies
void Scheduler::updateDependencyGraph(UseDefEntryType type, int regNum,
        LowOpndDefUse defuse, LatencyBetweenNativeInstructions causeOfLatency,
        LowOp* op) {
#ifndef DISABLE_DEBUG_ATOM_SCHEDULER
    const char * string_defuse = getUseDefType(defuse);
    const char * string_type = getUseDefEntryType(type);
    ALOGD("---updateDependencyGraph for resource <%s %d> "
            "at slot %d with %s---\n", string_type,
            regNum, op->slotId, string_defuse);
#endif

    unsigned int k;
    unsigned int index_for_user = userEntries.size();
    unsigned int index_for_producer = producerEntries.size();

    // Look for the producer of this resource. If none is found, then
    // index_for_producer will remain length of producerEntries list.
    if (type != UseDefType_Ctrl) {
        for (k = 0; k < producerEntries.size(); ++k) {
            if (producerEntries[k].type == type
                    && producerEntries[k].regNum == regNum) {
                index_for_producer = k;
                break;
            }
        }
    }

    // Look for the users of this resource. If none are found, then
    // index_for_user will remain length of userEntries list.
    for (k = 0; k < userEntries.size(); ++k) {
        if (userEntries[k].type == type && userEntries[k].regNum == regNum) {
            index_for_user = k;
            break;
        }
    }
#ifndef DISABLE_DEBUG_ATOM_SCHEDULER
    ALOGD("index_for_producer %d %d index_for_user %d %d\n",
            index_for_producer, producerEntries.size(),
            index_for_user, userEntries.size());
#endif

    if (defuse == LowOpndDefUse_Use || defuse == LowOpndDefUse_UseDef) {
        // If use or usedef, then there is a RAW dependency from producer
        // of the resource.
        if (type != UseDefType_Ctrl
                && index_for_producer != producerEntries.size()) {
            assert(producerEntries[index_for_producer].producerSlot != op->slotId);
#ifndef DISABLE_DEBUG_ATOM_SCHEDULER
            ALOGD("RAW dependency from %d to %d due to resource <%s %d>\n",
                    producerEntries[index_for_producer].producerSlot, op->slotId, string_type, regNum);
#endif
            DependencyInformation ds;
            ds.dataHazard = Dependency_RAW;
            ds.lowopSlotId = producerEntries[index_for_producer].producerSlot;
            ds.causeOfEdgeLatency = causeOfLatency;
            ds.edgeLatency = mapLatencyReasonToValue[causeOfLatency];
            // If producer is a memory load, then also add memory latency
            if (isMoveMnemonic(queuedLIREntries[ds.lowopSlotId]->opCode) &&
                    queuedLIREntries[ds.lowopSlotId]->opndSrc.type == LowOpndType_Mem) {
                // If memory load latency is greater than current latency,
                // replace it with the memory load
                if (mapLatencyReasonToValue[Latency_Memory_Load] > ds.edgeLatency) {
                    ds.causeOfEdgeLatency = Latency_Memory_Load;
                    ds.edgeLatency += mapLatencyReasonToValue[Latency_Memory_Load];
                }
            }
            dependencyAssociation[op].predecessorDependencies.push_back(ds);
        }
        // For Ctrl dependencies, when there is a user of a resource
        // it depends on the last producer. However, the last producer
        // depends on all previous producers. This is done as an
        // optimization because flag writers don't need to depend on
        // each other unless there is a flag reader.
        if (type == UseDefType_Ctrl && ctrlEntries.size() > 0) {
            // insert RAW from the last producer
            assert(ctrlEntries.back() != op->slotId);
#ifndef DISABLE_DEBUG_ATOM_SCHEDULER
            ALOGD("insert RAW from %d to %d due to Ctrl\n",
                    ctrlEntries.back(), op->slotId);
#endif
            DependencyInformation ds;
            ds.dataHazard = Dependency_RAW;
            ds.lowopSlotId = ctrlEntries.back();
            ds.causeOfEdgeLatency = causeOfLatency;
            ds.edgeLatency = mapLatencyReasonToValue[causeOfLatency];
            dependencyAssociation[op].predecessorDependencies.push_back(ds);
            // insert WAW from earlier producers to the last producer
            LowOp* opLast = (queuedLIREntries[ctrlEntries.back()]);
            for (k = 0; k < ctrlEntries.size() - 1; k++) {
                assert(ctrlEntries[k] != opLast->slotId);
#ifndef DISABLE_DEBUG_ATOM_SCHEDULER
                ALOGD("insert WAW from %d to %d due to Ctrl\n", ctrlEntries[k], ctrlEntries.back());
#endif
                DependencyInformation ds;
                ds.dataHazard = Dependency_WAW;
                ds.lowopSlotId = ctrlEntries[k];
                ds.causeOfEdgeLatency = causeOfLatency;
                ds.edgeLatency = mapLatencyReasonToValue[causeOfLatency];
                dependencyAssociation[opLast].predecessorDependencies.push_back(ds);
            }
        }

        // If this is the first use of this resource, then an
        // entry should be created in the userEntries list
        if (index_for_user == userEntries.size()) {
            UseDefUserEntry entry;
            entry.type = type;
            entry.regNum = regNum;
            userEntries.push_back(entry);
        } else if (type == UseDefType_Ctrl) {
            userEntries[index_for_user].useSlotsList.clear();
        }
        // Add current op as user of resource
        userEntries[index_for_user].useSlotsList.push_back(op->slotId);
#ifndef DISABLE_DEBUG_ATOM_SCHEDULER
        ALOGD("op with slot %d uses resource <%s %d>\n", op->slotId, string_type, regNum);
#endif

        if (type == UseDefType_Ctrl)
            ctrlEntries.clear();
    }

    if (defuse == LowOpndDefUse_Def || defuse == LowOpndDefUse_UseDef) {
        // If def or usedef, then there is a WAR dependency from earlier users
        // of this resource and a WAW dependency due to earlier producer
        if (index_for_user != userEntries.size()) {
            // Go through every user of resource and update current op with a WAR
            // from each user.
            for (k = 0; k < userEntries[index_for_user].useSlotsList.size();
                    k++) {
                if (userEntries[index_for_user].useSlotsList[k] == op->slotId)
                    continue; // No need to create dependency on self
#ifndef DISABLE_DEBUG_ATOM_SCHEDULER
                ALOGD("WAR dependency from %d to %d due to resource <%s %d>\n",
                        userEntries[index_for_user].useSlotsList[k], op->slotId, string_type, regNum);
#endif
                DependencyInformation ds;
                ds.dataHazard = Dependency_WAR;
                ds.lowopSlotId = userEntries[index_for_user].useSlotsList[k];
                ds.causeOfEdgeLatency = causeOfLatency;
                ds.edgeLatency = mapLatencyReasonToValue[causeOfLatency];
                dependencyAssociation[op].predecessorDependencies.push_back(ds);
            }
        }
        if (type != UseDefType_Ctrl
                && index_for_producer != producerEntries.size()) {
            // There is WAW dependency from earlier producer to current producer
            // For Ctrl resource, WAW is not relevant until there is a reader
            assert(producerEntries[index_for_producer].producerSlot != op->slotId);
#ifndef DISABLE_DEBUG_ATOM_SCHEDULER
            ALOGD("WAW dependency from %d to %d due to resource <%s %d>\n",
                    producerEntries[index_for_producer].producerSlot, op->slotId, string_type, regNum);
#endif
            DependencyInformation ds;
            ds.dataHazard = Dependency_WAW;
            ds.lowopSlotId = producerEntries[index_for_producer].producerSlot;
            ds.causeOfEdgeLatency = causeOfLatency;
            ds.edgeLatency = mapLatencyReasonToValue[causeOfLatency];
            dependencyAssociation[op].predecessorDependencies.push_back(ds);
        }

        if (type != UseDefType_Ctrl
                && index_for_producer == producerEntries.size()) {
            // If we get here it means that this the first known producer
            // of this resource and therefore we should keep track of this
            UseDefProducerEntry entry;
            entry.type = type;
            entry.regNum = regNum;
            producerEntries.push_back(entry);
        }
#ifndef DISABLE_DEBUG_ATOM_SCHEDULER
        ALOGD("op with slot %d produces/defines resource <%s %d>\n",
                op->slotId, string_type, regNum);
#endif
        if (type != UseDefType_Ctrl)
            // Add current op as producer of resource
            producerEntries[index_for_producer].producerSlot = op->slotId;
        else {
            // Save the current op as one of the producers of this resource
            ctrlEntries.push_back(op->slotId);
        }

        // Since this a new producer of the resource, we can now forget
        // all past users. This behavior is also correct if current op is
        // user and then producer because when handling usedef, use is
        // handled first.
        if (type != UseDefType_Ctrl && index_for_user != userEntries.size()) {
            userEntries[index_for_user].useSlotsList.clear();
        }
    }
}

//! \brief Given an volatile load update dependancy graph to reject later loads being
//! re-oredered with our load
//! \details We defines volatile load as producer of memory resourses. As a result
//! all future loads will have RAW dependancy from our load and re-oreder is disallowed.
//! \param op LIR for which to update dependencies
void Scheduler::updateUseDefInformation_volatile_load(LowOp * op)
{
    int regNum = REG_NOT_USED;
    UseDefEntryType type = UseDefType_MemUnknown;

    unsigned int k;
    unsigned int index_for_user = userEntries.size();
    unsigned int index_for_producer = producerEntries.size();

    // Look for the producer of this resource. If none is found, then
    // index_for_producer will remain length of producerEntries list.
    for (k = 0; k < producerEntries.size(); ++k) {
        if (producerEntries[k].type == type
                && producerEntries[k].regNum == regNum) {
            index_for_producer = k;
            break;
        }
    }

    // Look for the users of this resource. If none are found, then
    // index_for_user will remain length of userEntries list.
    for (k = 0; k < userEntries.size(); ++k) {
        if (userEntries[k].type == type && userEntries[k].regNum == regNum) {
            index_for_user = k;
            break;
        }
    }

#ifndef DISABLE_DEBUG_ATOM_SCHEDULER
    const char * string_type = getUseDefEntryType(type);
    ALOGD("volatile load with slot %d produces/defines resource <%s %d>\n",
            op->slotId, string_type, regNum);
#endif

    if (index_for_producer == producerEntries.size()) {
        // If we get here it means that this the first known producer
        // of this resource and therefore we should keep track of this
        UseDefProducerEntry entry;
        entry.type = type;
        entry.regNum = regNum;
        producerEntries.push_back(entry);
    }

    // Add current op as producer of resource
    producerEntries[index_for_producer].producerSlot = op->slotId;

    // Since this a new producer of the resource, we can now forget
    // all past users.
    if (index_for_user != userEntries.size()) {
        userEntries[index_for_user].useSlotsList.clear();
    }
}

//! \brief Given an access to a memory location this updates dependency graph,
//! usedef information, and control flags.
//! \details This method uses updateDependencyGraph internally to update
//! dependency graph, but knows the type of memory resource that is being used.
//! \param mOpnd reference to the structure for memory operand
//! \param defuse definition, usage, or both
//! \param op LIR for which to update dependencies
void Scheduler::updateDependencyGraphForMem(LowOpndMem & mOpnd, LowOpndDefUse defuse,
        LowOp* op) {
    MemoryAccessType mType = mOpnd.mType;
    int index = mOpnd.index;
    bool is64 = false;

    // We know that if we have an access to the memory holding constants
    // then it is okay to reorder instructions accessing that since it
    // is not updated at runtime. Thus, we don't add any dependencies for
    // this operand.
    if (mType == MemoryAccess_Constants)
    {
        // All accesses to the constants section should be done with
        // null base register
        assert(mOpnd.m_base.regNum == PhysicalReg_Null);

        return;
    }

    // Update dependency on registers used
    updateDependencyGraph(UseDefType_Reg, mOpnd.m_base.regNum,
            LowOpndDefUse_Use, Latency_Agen_stall, op);
    if (mOpnd.hasScale)
        updateDependencyGraph(UseDefType_Reg, mOpnd.m_index.regNum,
                LowOpndDefUse_Use, Latency_Agen_stall, op);

    // In order to be safe, if one of the operands has size 64, assume it is size 64
    if (op->numOperands >= 1 && op->opndDest.size == OpndSize_64)
        is64 = true;
    if (op->numOperands >= 2 && op->opndSrc.size == OpndSize_64)
        is64 = true;

    // At this point make a decision on whether or not to do memory disambiguation.
    // If it is not VR or SPILL access, then it may not be safe to disambiguate.
    if (mType == MemoryAccess_VR) {
        // All VR accesses should be made via Java frame pointer
        assert(mOpnd.m_base.regNum == PhysicalReg_FP);

        updateDependencyGraph(UseDefType_MemVR, index, defuse, Latency_None, op);
        if (is64)
            updateDependencyGraph(UseDefType_MemVR, index + 1, defuse, Latency_None, op);
    } else if (mType == MemoryAccess_SPILL) {
        // All spill accesses should be made via offset from EBP
        assert(mOpnd.m_base.regNum == PhysicalReg_EBP);

        updateDependencyGraph(UseDefType_MemSpill, index, defuse, Latency_None, op);
        if (is64) {
            updateDependencyGraph(UseDefType_MemSpill, index + 4, defuse, Latency_None, op);
        }
    } else // No disambiguation
        updateDependencyGraph(UseDefType_MemUnknown, REG_NOT_USED, defuse,
                Latency_None, op);
}

//! \brief Updates dependency information for PUSH which uses then defines %esp
//! and also updates native stack.
void inline Scheduler::handlePushDependencyUpdate(LowOp* op) {
    if (op->opCode == Mnemonic_PUSH) {
        updateDependencyGraph(UseDefType_Reg, PhysicalReg_ESP,
                LowOpndDefUse_UseDef, Latency_Agen_stall, op);
        updateDependencyGraph(UseDefType_MemUnknown, REG_NOT_USED,
                LowOpndDefUse_Def, Latency_None, op);
    }
}

//! \brief Updates dependency information for operations on floating point stack.
//! \details This should be called for all x87 instructions. This will ensure
//! that they are never reordered.
void inline Scheduler::handleFloatDependencyUpdate(LowOp* op) {
    // UseDef dependency is used so that x87 instructions won't be reordered
    // Whenever reordering support is added, this function should be replaced
    // and new resources defined like FPU flags, control word, status word, etc.
    updateDependencyGraph(UseDefType_Float, REG_NOT_USED, LowOpndDefUse_UseDef,
            Latency_None, op);
}

//! \brief Sets up dependencies on resources that must be live out.
//! \details Last write to a resource should be ensured to be live
//! out.
void Scheduler::setupLiveOutDependencies() {
    // Handle live out control flags. Namely, make sure that last flag
    // writer depends on all previous flag writers.
    if (ctrlEntries.size() != 0) {
        // If the ctrlEntries list is empty, it means we have no flag producers
        // that we need to update. This is caused if there really were not flag
        // producers or a flag reader has already cleared this list which means
        // the flag read already will be the one live out.
        LowOp* lastFlagWriter = (queuedLIREntries[ctrlEntries.back()]);

        // Don't include last flag writer in the iteration
        for (std::vector<unsigned int>::const_iterator iter = ctrlEntries.begin ();
                                                       (iter + 1) != ctrlEntries.end ();
                                                       iter++) {
            // Add a WAW dependency to the last flag writer from all other
            // flag writers
            DependencyInformation ds;
            ds.dataHazard = Dependency_WAW;
            ds.lowopSlotId = *iter;
            ds.causeOfEdgeLatency = Latency_None;
            ds.edgeLatency = mapLatencyReasonToValue[Latency_None];
            dependencyAssociation[lastFlagWriter].predecessorDependencies.push_back(
                    ds);
        }
    }

    //! @todo Take care of live out dependencies for all types of resources
    //! including physical registers.
}

//! \brief Updates dependency graph with the implicit dependencies on eax
//! and edx for imul, mul, div, idiv, and cdq
//! \warning Assumes that operand size is 32 bits
void inline Scheduler::handleImplicitDependenciesEaxEdx(LowOp* op) {
    if (isAluOpWithImplicitRegisterUsage(op->opCode)) {
        // mul and imul with a reg operand implicitly usedef eax and def edx
        // div and idiv with a reg operand implicitly usedef eax and usedef edx
        // cdq implicitly usedef eax and def edx
        if (op->opCode == Mnemonic_MUL || op->opCode == Mnemonic_IMUL
                || op->opCode == Mnemonic_CDQ) {
            updateDependencyGraph(UseDefType_Reg, PhysicalReg_EAX,
                    LowOpndDefUse_UseDef, Latency_None, op);
            updateDependencyGraph(UseDefType_Reg, PhysicalReg_EDX,
                    LowOpndDefUse_Def, Latency_None, op);
        } else if (op->opCode == Mnemonic_IDIV || op->opCode == Mnemonic_DIV) {
            updateDependencyGraph(UseDefType_Reg, PhysicalReg_EAX,
                    LowOpndDefUse_UseDef, Latency_None, op);
            updateDependencyGraph(UseDefType_Reg, PhysicalReg_EDX,
                    LowOpndDefUse_UseDef, Latency_None, op);
        }
    }
}

//! \brief Updates dependency information for LowOps with zero operands.
//! \param op has mnemonic RET
void Scheduler::updateUseDefInformation(LowOp * op) {
    assert(op->opCode == Mnemonic_RET);
    op->instructionLatency = getAtomMnemonicLatency(op->opCode);
    op->portType = getAtomMnemonicPort(op->opCode);
    assert(op->instructionLatency != INVN);
    assert(op->portType != INVP);

    updateDependencyGraph(UseDefType_Ctrl, REG_NOT_USED, LowOpndDefUse_Def,
            Latency_None, op);
    signalEndOfNativeBasicBlock(); // RET ends native basic block
}

//! \brief Updates dependency information for LowOps with a single immediate
//! operand.
//! \param op has mnemonic JMP, Jcc, or CALL
void Scheduler::updateUseDefInformation_imm(LowOp * op) {
    assert((op->opCode >= Mnemonic_Jcc && op->opCode <= Mnemonic_JG)
            || op->opCode == Mnemonic_JMP || op->opCode == Mnemonic_CALL);
    op->instructionLatency = getAtomMnemonicLatency_imm(op->opCode);
    op->portType = getAtomMnemonicPort_imm(op->opCode);
    assert(op->instructionLatency != INVN);
    assert(op->portType != INVP);

    if (op->opCode == Mnemonic_CALL || op->opCode == Mnemonic_JMP)
        updateDependencyGraph(UseDefType_Ctrl, REG_NOT_USED, LowOpndDefUse_Def,
                Latency_None, op);
    else
        updateDependencyGraph(UseDefType_Ctrl, REG_NOT_USED, LowOpndDefUse_Use,
                Latency_None, op);
    if (isBasicBlockDelimiter(op->opCode))
        signalEndOfNativeBasicBlock();
}

//! \brief Updates dependency information for LowOps with a single register operand.
//! \param op has mnemonic JMP, CALL, PUSH or it is an ALU instruction
void Scheduler::updateUseDefInformation_reg(LowOpReg * op) {
    assert(op->opCode == Mnemonic_JMP || op->opCode == Mnemonic_CALL
            || op->opCode == Mnemonic_PUSH || op->opCode2 == ATOM_NORMAL_ALU);
    op->instructionLatency = getAtomMnemonicLatency_reg(op->opCode);
    op->portType = getAtomMnemonicPort_reg(op->opCode);
    assert(op->instructionLatency != INVN);
    assert(op->portType != INVP);

    if (op->opCode == Mnemonic_CALL || op->opCode == Mnemonic_JMP
            || op->opCode == Mnemonic_PUSH
            || isAluOpWithImplicitRegisterUsage(op->opCode))
        op->opndSrc.defuse = LowOpndDefUse_Use;
    else // ALU ops with a single operand and no implicit operands use and then define
        op->opndSrc.defuse = LowOpndDefUse_UseDef;
    updateDependencyGraph(UseDefType_Reg, op->regOpnd.regNum,
            op->opndSrc.defuse, Latency_None, op);

    // PUSH will not update control flag
    if (op->opCode != Mnemonic_PUSH)
        updateDependencyGraph(UseDefType_Ctrl, REG_NOT_USED, LowOpndDefUse_Def,
                Latency_None, op);

    handleImplicitDependenciesEaxEdx(op);
    handlePushDependencyUpdate(op);

    if (isBasicBlockDelimiter(op->opCode))
        signalEndOfNativeBasicBlock();
}

//! \brief Updates dependency information for for LowOps with a single
//! memory operand.
//! \param op has mnemonic CALL, FLDCW, FNSTCW, PUSH, JMP or it is an ALU
//! instruction
void Scheduler::updateUseDefInformation_mem(LowOpMem * op) {
    assert(op->opCode == Mnemonic_CALL || op->opCode == Mnemonic_JMP
            || op->opCode == Mnemonic_FLDCW || op->opCode == Mnemonic_FNSTCW
            || op->opCode == Mnemonic_PUSH  || op->opCode2 == ATOM_NORMAL_ALU);
    op->instructionLatency = getAtomMnemonicLatency_mem(op->opCode);
    op->portType = getAtomMnemonicPort_mem(op->opCode);
    assert(op->instructionLatency != INVN);
    assert(op->portType != INVP);

    if (op->opCode == Mnemonic_CALL || op->opCode == Mnemonic_JMP
            || op->opCode == Mnemonic_FLDCW
            || op->opCode == Mnemonic_PUSH
            || isAluOpWithImplicitRegisterUsage(op->opCode))
        op->opndSrc.defuse = LowOpndDefUse_Use;
    else if (op->opCode == Mnemonic_FNSTCW)
        op->opndSrc.defuse = LowOpndDefUse_Def;
    else // ALU ops with a single operand and no implicit operands use and then define
        op->opndSrc.defuse = LowOpndDefUse_UseDef;
    updateDependencyGraphForMem(op->memOpnd, op->opndSrc.defuse, op);

    // PUSH will not update control flag
    if (op->opCode != Mnemonic_PUSH && op->opCode != Mnemonic_FLDCW
            && op->opCode != Mnemonic_FNSTCW)
        updateDependencyGraph(UseDefType_Ctrl, REG_NOT_USED, LowOpndDefUse_Def,
                Latency_None, op);

    handleImplicitDependenciesEaxEdx(op);
    handlePushDependencyUpdate(op);

    if (op->opCode == Mnemonic_FLDCW || op->opCode == Mnemonic_FNSTCW)
        handleFloatDependencyUpdate(op);
    if (isBasicBlockDelimiter(op->opCode))
        signalEndOfNativeBasicBlock();
}

//! \brief Updates dependency information for LowOps with two operands:
//! immediate to register
//! \param op must be a MOV variant, a comparison (CMP, TEST, COMISS, COMISD),
//! or an ALU instruction
void Scheduler::updateUseDefInformation_imm_to_reg(LowOpImmReg * op) {
    assert(isMoveMnemonic(op->opCode) || isCompareMnemonic(op->opCode)
            || op->opCode2 == ATOM_NORMAL_ALU);
    bool isMove = isMoveMnemonic(op->opCode);
    op->instructionLatency = getAtomMnemonicLatency_imm_to_reg(op->opCode);
    op->portType = getAtomMnemonicPort_imm_to_reg(op->opCode);
    assert(op->instructionLatency != INVN);
    assert(op->portType != INVP);

    if (usesAndDefinesFlags(op->opCode))
        updateDependencyGraph(UseDefType_Ctrl, REG_NOT_USED, LowOpndDefUse_Use,
                Latency_None, op);
    if (!isMove)
        updateDependencyGraph(UseDefType_Ctrl, REG_NOT_USED, LowOpndDefUse_Def,
                Latency_None, op);

    if (isMove)
        op->opndDest.defuse = LowOpndDefUse_Def;
    else if (isCompareMnemonic(op->opCode))
        op->opndDest.defuse = LowOpndDefUse_Use;
    else // ALU ops
        op->opndDest.defuse = LowOpndDefUse_UseDef;
    updateDependencyGraph(UseDefType_Reg, op->regDest.regNum,
            op->opndDest.defuse, Latency_None, op);
}

//! \brief Updates dependency information for LowOps with two operands:
//! immediate and memory.
//! \param op must be a MOV variant, a comparison (CMP, TEST, COMISS, COMISD),
//! or an ALU instruction
void Scheduler::updateUseDefInformation_imm_to_mem(LowOpImmMem * op) {
    assert(isMoveMnemonic(op->opCode) || isCompareMnemonic(op->opCode)
            || op->opCode2 == ATOM_NORMAL_ALU);
    bool isMove = isMoveMnemonic(op->opCode);
    op->instructionLatency = getAtomMnemonicLatency_imm_to_mem(op->opCode);
    op->portType = getAtomMnemonicPort_imm_to_mem(op->opCode);
    assert(op->instructionLatency != INVN);
    assert(op->portType != INVP);

    if (usesAndDefinesFlags(op->opCode))
        updateDependencyGraph(UseDefType_Ctrl, REG_NOT_USED, LowOpndDefUse_Use,
                Latency_None, op);
    if (!isMove)
        updateDependencyGraph(UseDefType_Ctrl, REG_NOT_USED, LowOpndDefUse_Def,
                Latency_None, op);

    if (isMove)
        op->opndDest.defuse = LowOpndDefUse_Def;
    else if (isCompareMnemonic(op->opCode))
        op->opndDest.defuse = LowOpndDefUse_Use;
    else // ALU ops
        op->opndDest.defuse = LowOpndDefUse_UseDef;
    updateDependencyGraphForMem(op->memDest, op->opndDest.defuse, op);
}

//! \brief Updates dependency information for LowOps with two operands:
//! register to register.
//! \param op must be a MOV variant, a comparison (CMP, TEST, COMISS, COMISD),
//! an ALU instruction including SSE variants SD and SS, SSE conversion,
//! or must have mnemonic FUCOMI, FUCOMIP, CMOVcc, or CDQ.
void Scheduler::updateUseDefInformation_reg_to_reg(LowOpRegReg * op) {
    assert(isMoveMnemonic(op->opCode) || isCompareMnemonic(op->opCode)
            || isConvertMnemonic(op->opCode) ||op->opCode2 == ATOM_NORMAL_ALU
            || op->opCode == Mnemonic_FUCOMI || op->opCode == Mnemonic_FUCOMIP
            || op->opCode == Mnemonic_CDQ ||
            (op->opCode >= Mnemonic_CMOVcc && op->opCode < Mnemonic_CMP));
    bool isMove = isMoveMnemonic(op->opCode);
    bool isConvert = isConvertMnemonic(op->opCode);
    op->instructionLatency = getAtomMnemonicLatency_reg_to_reg(op->opCode);
    op->portType = getAtomMnemonicPort_reg_to_reg(op->opCode);
    assert(op->instructionLatency != INVN);
    assert(op->portType != INVP);

    if ((op->opCode >= Mnemonic_CMOVcc && op->opCode < Mnemonic_CMP)
            || usesAndDefinesFlags(op->opCode))
        updateDependencyGraph(UseDefType_Ctrl, REG_NOT_USED, LowOpndDefUse_Use,
                Latency_None, op);
    else if (!isMove && !isConvert && op->opCode != Mnemonic_CDQ)
        // Note that FUCOMI and FUCOMIP update EFLAGS register (ZF, CF, PF),
        // so we have to update Ctrl dependency for them.
        updateDependencyGraph(UseDefType_Ctrl, REG_NOT_USED, LowOpndDefUse_Def,
                Latency_None, op);

    if (op->opCode == Mnemonic_CDQ) {
        // CDQ has no explicit operands but for encoding reasons it is treated like
        // it does and therefore comes to Scheduler via this interface function.
        // We can handle it here.
        assert(op->opndSrc.size == OpndSize_32
                && op->opndDest.size == OpndSize_32);
        handleImplicitDependenciesEaxEdx(op);
        return;
    }

    op->opndSrc.defuse = LowOpndDefUse_Use;
    updateDependencyGraph(UseDefType_Reg, op->regSrc.regNum, op->opndSrc.defuse,
            Latency_None, op);

    if (isMove == true || isConvert == true || isShuffleMnemonic (op->opCode) == true
            || (op->opCode >= Mnemonic_CMOVcc && op->opCode < Mnemonic_CMP)
            || op->opCode == Mnemonic_PEXTRD || op->opCode == Mnemonic_PEXTRW)
    {
        op->opndDest.defuse = LowOpndDefUse_Def;
    }
    else if (isCompareMnemonic(op->opCode))
    {
        op->opndDest.defuse = LowOpndDefUse_Use;
    }
    else
    {
        op->opndDest.defuse = LowOpndDefUse_UseDef;
    }
    updateDependencyGraph(UseDefType_Reg, op->regDest.regNum,
            op->opndDest.defuse, Latency_None, op);

    if (op->opCode == Mnemonic_FUCOMI || op->opCode == Mnemonic_FUCOMIP)
        handleFloatDependencyUpdate(op);
}

//! \brief Updates dependency information for LowOps with two operands:
//! memory to register.
//! \param op must be a MOV variant, a comparison (CMP, COMISS, COMISD),
//! an ALU instruction including SSE variants SD and SS, SSE conversion,
//! or must have mnemonic LEA
void Scheduler::updateUseDefInformation_mem_to_reg(LowOpMemReg * op) {
    assert(isMoveMnemonic(op->opCode) || isCompareMnemonic(op->opCode)
            || isConvertMnemonic(op->opCode) || op->opCode2 == ATOM_NORMAL_ALU
            || op->opCode == Mnemonic_LEA);
    bool isMove = isMoveMnemonic(op->opCode);
    bool isConvert = isConvertMnemonic(op->opCode);
    op->instructionLatency = getAtomMnemonicLatency_mem_to_reg(op->opCode);
    op->portType = getAtomMnemonicPort_mem_to_reg(op->opCode);
    assert(op->instructionLatency != INVN);
    assert(op->portType != INVP);

    if (usesAndDefinesFlags(op->opCode))
        updateDependencyGraph(UseDefType_Ctrl, REG_NOT_USED, LowOpndDefUse_Use,
                Latency_None, op);
    if (!isMove && !isConvert && op->opCode != Mnemonic_LEA)
        updateDependencyGraph(UseDefType_Ctrl, REG_NOT_USED, LowOpndDefUse_Def,
                Latency_None, op);

    // Read from memory
    // However, LEA does not load from memory, and instead it uses the register
    op->opndSrc.defuse = LowOpndDefUse_Use;
    if (op->opCode != Mnemonic_LEA)
        updateDependencyGraphForMem(op->memSrc, op->opndSrc.defuse, op);
    else {
        updateDependencyGraph(UseDefType_Reg, op->memSrc.m_base.regNum,
                op->opndSrc.defuse, Latency_Agen_stall, op);
        if(op->memSrc.hasScale)
            updateDependencyGraph(UseDefType_Reg, op->memSrc.m_index.regNum,
                    op->opndSrc.defuse, Latency_Agen_stall, op);
    }

    if (isMove || isConvert || op->opCode == Mnemonic_LEA)
        op->opndDest.defuse = LowOpndDefUse_Def;
    else if (isCompareMnemonic(op->opCode))
        op->opndDest.defuse = LowOpndDefUse_Use;
    else // ALU ops
        op->opndDest.defuse = LowOpndDefUse_UseDef;
    updateDependencyGraph(UseDefType_Reg, op->regDest.regNum,
            op->opndDest.defuse, Latency_None, op);
}

//! \brief Updates dependency information for LowOps with two operands:
//! register to memory.
//! \param op must be a MOV variant, a comparison (CMP), a cmpxchange (CMPXCHG), or an ALU instruction
void Scheduler::updateUseDefInformation_reg_to_mem(LowOpRegMem * op) {
    assert(isMoveMnemonic(op->opCode) || isCompareMnemonic(op->opCode)
            || op->opCode2 == ATOM_NORMAL_ALU || isCmpxchgMnemonic(op->opCode));
    bool isMove = isMoveMnemonic(op->opCode);
    bool isCmpxchg = isCmpxchgMnemonic(op->opCode);
    op->instructionLatency = getAtomMnemonicLatency_reg_to_mem(op->opCode);
    op->portType = getAtomMnemonicPort_reg_to_mem(op->opCode);
    assert(op->instructionLatency != INVN);
    assert(op->portType != INVP);

    if (usesAndDefinesFlags(op->opCode))
        updateDependencyGraph(UseDefType_Ctrl, REG_NOT_USED, LowOpndDefUse_Use,
                Latency_None, op);
    if (!isMove)
        updateDependencyGraph(UseDefType_Ctrl, REG_NOT_USED, LowOpndDefUse_Def,
                Latency_None, op);

    //CMPXCHG uses and defines EAX
    if (isCmpxchg == true) {
        updateDependencyGraph(UseDefType_Reg, PhysicalReg_EAX, LowOpndDefUse_UseDef,
            Latency_None, op);
    }

    op->opndSrc.defuse = (op->opCode == Mnemonic_XCHG) ? LowOpndDefUse_UseDef : LowOpndDefUse_Use;
    updateDependencyGraph(UseDefType_Reg, op->regSrc.regNum, op->opndSrc.defuse,
            Latency_None, op);

    if (isMove)
        op->opndDest.defuse = (op->opCode == Mnemonic_XCHG) ? LowOpndDefUse_UseDef : LowOpndDefUse_Def;
    else if (isCompareMnemonic(op->opCode))
        op->opndDest.defuse = LowOpndDefUse_Use;
    else // ALU ops
        op->opndDest.defuse = LowOpndDefUse_UseDef;
    updateDependencyGraphForMem(op->memDest, op->opndDest.defuse, op);
}

//! \brief Updates dependency information for LowOps with two operands:
//! floating point stack to memory.
//! \param op must have mnemonic FSTP, FST, FISTP, or FIST
void Scheduler::updateUseDefInformation_fp_to_mem(LowOpRegMem * op) {
    assert(op->opCode == Mnemonic_FSTP || op->opCode == Mnemonic_FST
            || op->opCode == Mnemonic_FISTP || op->opCode == Mnemonic_FIST);
    op->instructionLatency = getAtomMnemonicLatency_reg_to_mem(op->opCode);
    op->portType = getAtomMnemonicPort_reg_to_mem(op->opCode);
    assert(op->instructionLatency != INVN);
    assert(op->portType != INVP);

    handleFloatDependencyUpdate(op);

    op->opndSrc.defuse = LowOpndDefUse_Use;
    updateDependencyGraph(UseDefType_Reg, PhysicalReg_ST0, op->opndSrc.defuse,
            Latency_None, op);
    op->opndDest.defuse = LowOpndDefUse_Def;
    updateDependencyGraphForMem(op->memDest, op->opndDest.defuse, op);
}

//! \brief Updates dependency information for LowOps with two operands:
//! memory to floating point stack.
//! \param op must have mnemonic FLD or FILD, or must be an x87 ALU op
void Scheduler::updateUseDefInformation_mem_to_fp(LowOpMemReg * op) {
    assert(op->opCode == Mnemonic_FLD || op->opCode == Mnemonic_FILD
            || op->opCode2 == ATOM_NORMAL_ALU);
    op->instructionLatency = getAtomMnemonicLatency_mem_to_reg(op->opCode);
    op->portType = getAtomMnemonicPort_mem_to_reg(op->opCode);
    assert(op->instructionLatency != INVN);
    assert(op->portType != INVP);

    handleFloatDependencyUpdate(op);

    op->opndSrc.defuse = LowOpndDefUse_Use;
    updateDependencyGraphForMem(op->memSrc, op->opndSrc.defuse, op);
    if (op->opCode == Mnemonic_FLD || op->opCode == Mnemonic_FILD)
        op->opndDest.defuse = LowOpndDefUse_Def;
    else // x87 ALU ops
        op->opndDest.defuse = LowOpndDefUse_UseDef;
    updateDependencyGraph(UseDefType_Reg, PhysicalReg_ST0, op->opndDest.defuse,
            Latency_None, op);
}

//! \brief Generates IA native code for given LowOp
//! \details This method takes a LowOp and generates x86 instructions into the
//! code stream by making calls to the encoder.
//! \param op to be encoded and placed into code stream.
void Scheduler::generateAssembly(LowOp * op) {
    if(IS_ANY_JIT_ERROR_SET())
        return;
    if (op->numOperands == 0) {
        stream = encoder_return(stream);
    } else if (op->numOperands == 1) {
        if (op->opndSrc.type == LowOpndType_Label) {
            bool unknown;
            OpndSize size;
            int imm;
            if (op->opCode == Mnemonic_JMP)
                imm = getRelativeOffset(((LowOpLabel*) op)->labelOpnd.label,
                        ((LowOpLabel*) op)->labelOpnd.isLocal, JmpCall_uncond,
                        &unknown, &size);
            else if (op->opCode == Mnemonic_CALL)
                imm = getRelativeOffset(((LowOpLabel*) op)->labelOpnd.label,
                        ((LowOpLabel*) op)->labelOpnd.isLocal, JmpCall_call,
                        &unknown, &size);
            else
                imm = getRelativeOffset(((LowOpLabel*) op)->labelOpnd.label,
                        ((LowOpLabel*) op)->labelOpnd.isLocal, JmpCall_cond,
                        &unknown, &size);
            op->opndSrc.size = size;
            stream = encoder_imm(op->opCode, op->opndSrc.size, imm, stream);
        } else if (op->opndSrc.type == LowOpndType_BlockId) {
            LowOpBlock * blockOp = reinterpret_cast<LowOpBlock *>(op);

            // If the immediate needs aligned, then do a test dump to see
            // if the immediate will cross the 16-byte boundary. If we plan
            // on aligning immediate, we expect that its size will be 32-bit
            if (blockOp->blockIdOpnd.immediateNeedsAligned) {
                // Dump to stream but don't update stream pointer
                char * newStream = encoder_imm(blockOp->opCode, OpndSize_32, 0,
                        stream);

                // Immediates are assumed to be at end of instruction, so just check
                // that the updated stream pointer does not break up the 32-bit immediate
                unsigned int bytesCrossing =
                        reinterpret_cast<unsigned int>(newStream) % 16;
                bool needNops =
                        (bytesCrossing > OpndSize_Null
                                && bytesCrossing < OpndSize_32) ? true : false;

                if (needNops)
                    stream = encoder_nops(OpndSize_32 - bytesCrossing, stream);
            }

            bool unknown;
            OpndSize actualSize = OpndSize_Null;
            int imm;
            if (blockOp->opCode == Mnemonic_JMP)
                imm = getRelativeNCG(blockOp->blockIdOpnd.value,
                        JmpCall_uncond, &unknown, &actualSize);
            else
                imm = getRelativeNCG(blockOp->blockIdOpnd.value,
                        JmpCall_cond, &unknown, &actualSize);

            // When we need to align, we expect that the size of the immediate is
            // 32-bit so we make sure of that now.
            blockOp->opndSrc.size =
                    blockOp->blockIdOpnd.immediateNeedsAligned ?
                            OpndSize_32 : actualSize;

            stream = encoder_imm(blockOp->opCode, blockOp->opndSrc.size, imm, stream);
        } else if (op->opndSrc.type == LowOpndType_Imm) {
            stream = encoder_imm(op->opCode, op->opndSrc.size,
                    ((LowOpImm*) op)->immOpnd.value, stream);
        } else if (op->opndSrc.type == LowOpndType_Reg) {
            stream = encoder_reg(op->opCode, op->opndSrc.size,
                    ((LowOpReg*) op)->regOpnd.regNum,
                    ((LowOpReg*) op)->regOpnd.isPhysical,
                    ((LowOpReg*) op)->regOpnd.regType, stream);
        } else { // Corresponds to lower_mem
            stream = encoder_mem(op->opCode, op->opndSrc.size,
                    ((LowOpMem*) op)->memOpnd.m_disp.value,
                    ((LowOpMem*) op)->memOpnd.m_base.regNum,
                    ((LowOpMem*) op)->memOpnd.m_base.isPhysical, stream);
        }
    }
    //Check if we have 3 operands
    else if (op->numOperands == 3)
    {
        assert (op->opndSrc.type == LowOpndType_Reg && op->opndDest.type == LowOpndType_Reg);

        //We have an instruction with three operands: two reg and one immediate
        LowOpImmRegReg *immRegRegOp = reinterpret_cast <LowOpImmRegReg *> (op);

        //Now encode it
        stream = encoder_imm_reg_reg (immRegRegOp->opCode, immRegRegOp->imm.value, immRegRegOp->imm.immediateSize,
                immRegRegOp->regSrc.regNum, immRegRegOp->opndSrc.size, immRegRegOp->regDest.regNum,
                immRegRegOp->opndDest.size, stream);
    }
    // Number of operands is 2
    // Handles LowOps coming from  lower_imm_reg, lower_imm_mem,
    // lower_reg_mem, lower_mem_reg, lower_mem_scale_reg,
    // lower_reg_mem_scale, lower_reg_reg, lower_fp_mem, and lower_mem_fp
    else if (op->opndDest.type == LowOpndType_Reg
            && op->opndSrc.type == LowOpndType_Imm) {

        SwitchInfoScheduler* switchInfoScheduler;
        switchInfoScheduler = ((LowOpImmReg*)op)->switchInfoScheduler;
        if (switchInfoScheduler != NULL && switchInfoScheduler->switchInfo){
                int offset = switchInfoScheduler->offset;

                // update address for immediate location
                if (switchInfoScheduler->isFirst) {
                    switchInfoScheduler->switchInfo->immAddr = stream + offset;
                }
                else {
                    switchInfoScheduler->switchInfo->immAddr2 = stream + offset;
                }
        }
        stream = encoder_imm_reg_diff_sizes(op->opCode, op->opndSrc.size,
                ((LowOpImmReg*) op)->immSrc.value,
                op->opndDest.size,
                ((LowOpImmReg*) op)->regDest.regNum,
                ((LowOpImmReg*) op)->regDest.isPhysical,
                ((LowOpImmReg*) op)->regDest.regType, stream);
    } else if (op->opndDest.type == LowOpndType_Reg
            && op->opndSrc.type == LowOpndType_Chain) {
        // The immediates used for chaining must be aligned within a 16-byte
        // region so we need to ensure that now.

        // First, dump to code stream but do not update stream pointer
        char * newStream = encoder_imm_reg_diff_sizes(op->opCode,op->opndSrc.size,
                ((LowOpImmReg*) op)->immSrc.value, op->opndDest.size,
                ((LowOpImmReg*) op)->regDest.regNum,
                ((LowOpImmReg*) op)->regDest.isPhysical,
                ((LowOpImmReg*) op)->regDest.regType, stream);

        // Immediates are assumed to be at end of instruction, so just check
        // that the updated stream pointer does not break up the immediate
        unsigned int bytesCrossing =
                reinterpret_cast<unsigned int>(newStream) % 16;
        bool needNops =
                (bytesCrossing > OpndSize_Null
                        && bytesCrossing < op->opndDest.size) ? true : false;

        if (needNops)
            stream = encoder_nops(op->opndDest.size - bytesCrossing, stream);

        // Now we are ready to do the actual encoding
        insertChainingWorklist(((LowOpImmReg*) op)->immSrc.value, stream);
        stream = encoder_imm_reg_diff_sizes(op->opCode,op->opndSrc.size,
                ((LowOpImmReg*) op)->immSrc.value, op->opndDest.size,
                ((LowOpImmReg*) op)->regDest.regNum,
                ((LowOpImmReg*) op)->regDest.isPhysical,
                ((LowOpImmReg*) op)->regDest.regType, stream);
    } else if (op->opndDest.type == LowOpndType_Mem
            && op->opndSrc.type == LowOpndType_Imm) {
        SwitchInfoScheduler* switchInfoScheduler;
        switchInfoScheduler = ((LowOpImmMem*)op)->switchInfoScheduler;
        if (switchInfoScheduler != 0 && switchInfoScheduler->switchInfo){
                int offset = switchInfoScheduler->offset;

                // update address for immediate location
                switchInfoScheduler->switchInfo->immAddr = stream + offset;
        }

        stream = encoder_imm_mem_diff_sizes(op->opCode, op->opndSrc.size,
                ((LowOpImmMem*) op)->immSrc.value, op->opndDest.size,
                ((LowOpImmMem*) op)->memDest.m_disp.value,
                ((LowOpImmMem*) op)->memDest.m_base.regNum,
                ((LowOpImmMem*) op)->memDest.m_base.isPhysical, stream);
    } else if (op->opndDest.type == LowOpndType_Mem
            && op->opndSrc.type == LowOpndType_Chain) {
        // The immediates used for chaining must be aligned within a 16-byte
        // region so we need to ensure that now.

        // First, dump to code stream but do not update stream pointer
        char * newStream = encoder_imm_mem_diff_sizes(op->opCode, op->opndSrc.size,
                ((LowOpImmMem*) op)->immSrc.value, op->opndDest.size,
                ((LowOpImmMem*) op)->memDest.m_disp.value,
                ((LowOpImmMem*) op)->memDest.m_base.regNum,
                ((LowOpImmMem*) op)->memDest.m_base.isPhysical, stream);

        // Immediates are assumed to be at end of instruction, so just check
        // that the updated stream pointer does not break up the immediate
        unsigned int bytesCrossing =
                reinterpret_cast<unsigned int>(newStream) % 16;
        bool needNops =
                (bytesCrossing > OpndSize_Null
                        && bytesCrossing < op->opndDest.size) ? true : false;

        if (needNops)
            stream = encoder_nops(op->opndDest.size - bytesCrossing, stream);

        // Now we are ready to do the actual encoding
        insertChainingWorklist(((LowOpImmMem*) op)->immSrc.value, stream);
        stream = encoder_imm_mem_diff_sizes(op->opCode, op->opndSrc.size,
                ((LowOpImmMem*) op)->immSrc.value, op->opndDest.size,
                ((LowOpImmMem*) op)->memDest.m_disp.value,
                ((LowOpImmMem*) op)->memDest.m_base.regNum,
                ((LowOpImmMem*) op)->memDest.m_base.isPhysical, stream);
    } else if (op->opndDest.type == LowOpndType_Reg
            && op->opndSrc.type == LowOpndType_Reg) {
        if (op->opCode == Mnemonic_FUCOMIP || op->opCode == Mnemonic_FUCOMI) {
            stream = encoder_compare_fp_stack(op->opCode == Mnemonic_FUCOMIP,
                    ((LowOpRegReg*) op)->regSrc.regNum
                            - ((LowOpRegReg*) op)->regDest.regNum,
                    op->opndDest.size == OpndSize_64, stream);
        } else {
            stream = encoder_reg_reg_diff_sizes(op->opCode, op->opndSrc.size,
                    ((LowOpRegReg*) op)->regSrc.regNum,
                    ((LowOpRegReg*) op)->regSrc.isPhysical,
                    op->opndDest.size,
                    ((LowOpRegReg*) op)->regDest.regNum,
                    ((LowOpRegReg*) op)->regDest.isPhysical,
                    ((LowOpRegReg*) op)->regDest.regType, stream);
        }
    } else if (op->opndDest.type == LowOpndType_Reg
            && op->opndSrc.type == LowOpndType_Mem) {
        // Corresponds to lower_mem_reg, lower_mem_fp, or lower_mem_scale_reg
        LowOpMemReg * regmem_op = (LowOpMemReg*) op;

        // Constant initialization for 64 bit data requires saving stream address location
        struct ConstInfo* tmpPtr;
        tmpPtr = regmem_op->constLink;
        if (tmpPtr != NULL && tmpPtr->constAddr == NULL){
            // save address of instruction post scheduling
            tmpPtr->streamAddr = stream;
        }

        if (regmem_op->regDest.regType == LowOpndRegType_fs)
            stream = encoder_mem_fp(regmem_op->opCode, regmem_op->opndSrc.size,
                    regmem_op->memSrc.m_disp.value,
                    regmem_op->memSrc.m_base.regNum,
                    regmem_op->memSrc.m_base.isPhysical,
                    regmem_op->regDest.regNum - PhysicalReg_ST0, stream);
        else if (regmem_op->memSrc.hasScale)
            stream = encoder_mem_disp_scale_to_reg_diff_sizes(regmem_op->opCode,
                    regmem_op->opndSrc.size, regmem_op->memSrc.m_base.regNum,
                    regmem_op->memSrc.m_base.isPhysical,
                    regmem_op->memSrc.m_disp.value,
                    regmem_op->memSrc.m_index.regNum,
                    regmem_op->memSrc.m_index.isPhysical,
                    regmem_op->memSrc.m_scale.value, regmem_op->opndDest.size,
                    regmem_op->regDest.regNum, regmem_op->regDest.isPhysical,
                    regmem_op->regDest.regType, stream);
        else
            stream = encoder_mem_to_reg_diff_sizes(regmem_op->opCode,
                    regmem_op->opndSrc.size, regmem_op->memSrc.m_disp.value,
                    regmem_op->memSrc.m_base.regNum,
                    regmem_op->memSrc.m_base.isPhysical,
                    regmem_op->opndDest.size, regmem_op->regDest.regNum,
                    regmem_op->regDest.isPhysical, regmem_op->regDest.regType,
                    stream);
    } else if (op->opndDest.type == LowOpndType_Mem
            && op->opndSrc.type == LowOpndType_Reg) {
        // Corresponds to lower_reg_mem, lower_fp_mem, or lower_reg_mem_scale
        LowOpRegMem * memreg_op = (LowOpRegMem*) op;
        if (memreg_op->regSrc.regType == LowOpndRegType_fs)
            stream = encoder_fp_mem(memreg_op->opCode, memreg_op->opndDest.size,
                    memreg_op->regSrc.regNum - PhysicalReg_ST0,
                    memreg_op->memDest.m_disp.value,
                    memreg_op->memDest.m_base.regNum,
                    memreg_op->memDest.m_base.isPhysical, stream);
        else if (memreg_op->memDest.hasScale)
            stream = encoder_reg_mem_disp_scale(memreg_op->opCode,
                    memreg_op->opndDest.size, memreg_op->regSrc.regNum,
                    memreg_op->regSrc.isPhysical,
                    memreg_op->memDest.m_base.regNum,
                    memreg_op->memDest.m_base.isPhysical,
                    memreg_op->memDest.m_disp.value,
                    memreg_op->memDest.m_index.regNum,
                    memreg_op->memDest.m_index.isPhysical,
                    memreg_op->memDest.m_scale.value,
                    memreg_op->regSrc.regType, stream);
        else
            stream = encoder_reg_mem(op->opCode, op->opndDest.size,
                    memreg_op->regSrc.regNum, memreg_op->regSrc.isPhysical,
                    memreg_op->memDest.m_disp.value,
                    memreg_op->memDest.m_base.regNum,
                    memreg_op->memDest.m_base.isPhysical,
                    memreg_op->regSrc.regType, stream);
    }
    if (dvmCompilerWillCodeCacheOverflow((stream - streamStart) +
       CODE_CACHE_PADDING) == true) {
        ALOGI("JIT_INFO: Code cache full after Scheduler::generateAssembly (trace uses %uB)", (stream - streamStart));
        SET_JIT_ERROR(kJitErrorCodeCacheFull);
        dvmCompilerSetCodeAndDataCacheFull();
    }
}

//! \brief Figures out which LowOps are ready after an instruction at chosenIdx
//! is scheduled.
//! \details It also updates the readyTime of every LowOp waiting to be scheduled.
//! \param chosenIdx is the index of the chosen instruction for scheduling
//! \param scheduledOps is an input list of scheduled LowOps
//! \param readyOps is an output list of LowOps that are ready
void Scheduler::updateReadyOps(int chosenIdx, BitVector * scheduledOps,
        BitVector * readyOps) {
    // Go through each successor LIR that depends on selected LIR
    for (unsigned int k = 0; k < dependencyAssociation[queuedLIREntries[chosenIdx]].successorDependencies.size(); ++k) {
        int dst = dependencyAssociation[queuedLIREntries[chosenIdx]].successorDependencies[k].lowopSlotId;
        bool isReady = true;
        int readyTime = -1;
        // If all predecessors are scheduled, insert into ready queue
        for (unsigned int k2 = 0; k2 < dependencyAssociation[queuedLIREntries[dst]].predecessorDependencies.size(); ++k2) {
            int src = dependencyAssociation[queuedLIREntries[dst]].predecessorDependencies[k2].lowopSlotId;
            if (dvmIsBitSet(scheduledOps, src) == false) {
                // If one of parents hasn't been scheduled, then current instruction is not ready
                isReady = false;
                break;
            }

            // If our candidate is a RAW, then we must wait until parent finishes
            // executing. However, if we have a WAW, WAR, or RAR, then we can issue
            // next cycle.
            int readyDelay =
                    (dependencyAssociation[queuedLIREntries[dst]].predecessorDependencies[k2].dataHazard
                            == Dependency_RAW) ?
                            queuedLIREntries[src]->instructionLatency : 1;

            // Candidate ready time is the sum of the scheduled time of parent,
            // the latency of parent, and the weight of edge between parent
            // and self
            int candidateReadyTime = queuedLIREntries[src]->scheduledTime + readyDelay
                    + dependencyAssociation[queuedLIREntries[dst]].predecessorDependencies[k2].edgeLatency;

            if (readyTime < candidateReadyTime) {
                // This is ready after ALL predecessors have finished executing
                readyTime = candidateReadyTime;
            }
        }
        if (isReady) {
            dvmSetBit(readyOps, dst);
            queuedLIREntries[dst]->readyTime = readyTime;
#ifndef DISABLE_DEBUG_ATOM_SCHEDULER
            ALOGD("update readyTime of slot %d: %d\n", dst, readyTime);
#endif
        }
    }
}

//! This method constructs the inverse topological sort of the
//! dependency graph of current basic block (queuedLIREntries)
//! \param nodeId Index of LowOp in queuedLIREntries
//! \param visitedList List with same indexing as queuedLIREntries
//! that keeps track of LowOps that have already been visited
//! \param inverseTopologicalOrder A list that will eventually
//! hold the inverse topological order of the dependency graph.
//! Inverse order means that parent nodes come later in the list
//! compared to its children.
void Scheduler::visitNodeTopologicalSort(unsigned int nodeId, int * visitedList,
        NativeBasicBlock & inverseTopologicalOrder) {
    // If it has been visited already, there's no need to do anything
    if (visitedList[nodeId] == 0) {
        assert(queuedLIREntries[nodeId]->slotId == nodeId);
        // Mark as visited
        visitedList[nodeId]++;
        for (unsigned int child = 0;
                child < dependencyAssociation[queuedLIREntries[nodeId]].successorDependencies.size();
                ++child) {
            // visit children
            visitNodeTopologicalSort(
                    dependencyAssociation[queuedLIREntries[nodeId]].successorDependencies[child].lowopSlotId,
                    visitedList, inverseTopologicalOrder);
        }
        // Since all children have been visited, can now add node to list
        inverseTopologicalOrder.push_back(queuedLIREntries[nodeId]);
    }
}

//! \brief Finds longest path latency for every node in every tree in the
//! dependency graph.
//! \details This updates the longest path field of every LowOp in the current
//! native basic block.
//! \see queuedLIREntries
void Scheduler::findLongestPath() {
    NativeBasicBlock inverseTopologicalOrder;

    // Initialize visited list to 0 (false) for all nodes
    int visitedList[queuedLIREntries.size()];
    memset(visitedList, 0, queuedLIREntries.size() * sizeof(int));

    // Determine topological order.
    for (unsigned int node = 0; node < queuedLIREntries.size(); ++node) {
        visitNodeTopologicalSort(node, visitedList, inverseTopologicalOrder);
    }

    assert(queuedLIREntries.size() == inverseTopologicalOrder.size());

    // for each node in inverse topological order
    for(unsigned int vindex = 0; vindex < inverseTopologicalOrder.size(); ++vindex) {
        int bestLongestPath = 0;
        // Go through each child find the best longest path.
        // Since we are doing this in inverse topological order,
        // we know the longest path for all children has already
        // been updated.
        for(unsigned int windex = 0; windex < dependencyAssociation[inverseTopologicalOrder[vindex]].successorDependencies.size();
                ++windex) {
            int successorSlotId = dependencyAssociation[inverseTopologicalOrder[vindex]].successorDependencies[windex].lowopSlotId;
            int edgeLatency = dependencyAssociation[inverseTopologicalOrder[vindex]].successorDependencies[windex].edgeLatency;
            if (queuedLIREntries[successorSlotId]->longestPath > bestLongestPath) {
                bestLongestPath = queuedLIREntries[successorSlotId]->longestPath + edgeLatency;
            }
        }
        // Longest path to self is sum of best longest path to children plus
        // instruction latency of self
        inverseTopologicalOrder[vindex]->longestPath = inverseTopologicalOrder[vindex]->instructionLatency
                + bestLongestPath;
    }
}

//! \brief Reorders basic block to minimize block latency and make use of both
//! Atom issue ports.
//! \details The result of scheduling is stored in scheduledLIREntries. Additionally,
//! each LowOp individually stores its scheduled time (logical based on index ordering).
//!
//! Algorithm details:
//! - select one LIR from readyQueue with 2 criteria:
//!   -# smallest readyTime
//!   -# on critical path
//! - A pair of LowOps can be issued at the same time slot if they use different issue ports.
//! - A LowOp can be issued if
//!   -# all pending ops can commit ahead of this LowOp (restriction reflected in readyTime)
//!   -# it is ready
//! - At end, currentTime is advanced to readyTime of the selected LowOps
//! - If any LIR has jmp, jcc, call, or ret mnemonic, it must be scheduled last
//!
//! \see scheduledLIREntries
//! \post Scheduler::scheduledLIREntries is same size as Scheduler::queuedLIREntries
//! \post If last LIR in Scheduler::queuedLIREntries is a jump, call, or return, it must
//! also be the last LIR in Scheduler::scheduledLIREntries
void Scheduler::schedule() {
    // Declare data structures for scheduling
    unsigned int candidateArray[queuedLIREntries.size()]; // ready candidates for scheduling
    unsigned int num_candidates = 0 /*index for candidateArray*/, numScheduled = 0, lirID;
    int currentTime = 0;

    // LIRs ready for scheduling
    BitVector * readyOps = dvmCompilerAllocBitVector(queuedLIREntries.size(), false);
    dvmClearAllBits(readyOps);
    // LIRs that have been scheduled
    BitVector * scheduledOps = dvmCompilerAllocBitVector(queuedLIREntries.size(), false);
    dvmClearAllBits(scheduledOps);

    // Set up the live out dependencies
    setupLiveOutDependencies();

    // Predecessor dependencies have already been initialized in the dependency graph building.
    // Now, initialize successor dependencies to complete dependency graph.
    for (lirID = 0; lirID < queuedLIREntries.size(); ++lirID) {
        for (unsigned int k2 = 0; k2 < dependencyAssociation[queuedLIREntries[lirID]].predecessorDependencies.size(); ++k2) {
            int src = dependencyAssociation[queuedLIREntries[lirID]].predecessorDependencies[k2].lowopSlotId;
            DependencyInformation ds;
            ds.lowopSlotId = lirID;

            // Since edges are directional, no need to invert weight.
            ds.edgeLatency = dependencyAssociation[queuedLIREntries[lirID]].predecessorDependencies[k2].edgeLatency;
            dependencyAssociation[queuedLIREntries[src]].successorDependencies.push_back(ds);
        }
    }

    // Find longest path from each LIR to the leaves of the dependency trees
    findLongestPath();

    // When a LowOp is ready, it means all its predecessors are scheduled
    // and the readyTime of this LowOp has been set already.
    for (lirID = 0; lirID < queuedLIREntries.size(); lirID++) {
#ifndef DISABLE_DEBUG_ATOM_SCHEDULER
        ALOGD("-- slot %d: latency %d port type %d\n", lirID, queuedLIREntries[lirID]->instructionLatency,
                queuedLIREntries[lirID]->portType);
#endif
        if (dependencyAssociation[queuedLIREntries[lirID]].predecessorDependencies.size() == 0) {
            dvmSetBit(readyOps, lirID);
#ifndef DISABLE_DEBUG_ATOM_SCHEDULER
            ALOGD("slot %d is ready\n", lirID);
#endif
            queuedLIREntries[lirID]->readyTime = 0;
        }
    }

    // Schedule each of LIRs in the basic block
    while (numScheduled < queuedLIREntries.size()) {
        // Set chosen indices to BB size since no LIR will have
        // this id.
        unsigned int chosenIdx1 = queuedLIREntries.size();
        unsigned int chosenIdx2 = queuedLIREntries.size();

        // Reset number of picked candidates
        num_candidates = 0;

        // Select candidates that are ready (readyTime <= currentTime)
        for (lirID = 0; lirID < queuedLIREntries.size(); lirID++) {
            if (dvmIsBitSet(readyOps, lirID)
                    && queuedLIREntries[lirID]->readyTime <= currentTime
                    && (!isBasicBlockDelimiter(queuedLIREntries[lirID]->opCode)))
                candidateArray[num_candidates++] = lirID;
        }

        // If no candidate is ready to be issued, just pick the one with
        // the smallest readyTime
        if (num_candidates == 0) {

            // First, find the smallest ready time out of instructions that are
            // ready.
            for (lirID = 0; lirID < queuedLIREntries.size(); lirID++) {
                if (dvmIsBitSet(readyOps, lirID)
                        && (!isBasicBlockDelimiter(queuedLIREntries[lirID]->opCode))) {
                    if (chosenIdx1 == queuedLIREntries.size()
                            || queuedLIREntries[lirID]->readyTime
                                    < queuedLIREntries[chosenIdx1]->readyTime) {
                        chosenIdx1 = lirID;
                        // Update current time with smallest ready time
                        currentTime = queuedLIREntries[lirID]->readyTime;
                    }
                }
            }

            // Select any other candidates that also are ready at the same time.
            for (lirID = 0; lirID < queuedLIREntries.size(); lirID++) {
                if (dvmIsBitSet(readyOps, lirID)
                        && (!isBasicBlockDelimiter(queuedLIREntries[lirID]->opCode))
                        && queuedLIREntries[lirID]->readyTime <= currentTime) {
                    candidateArray[num_candidates++] = lirID;
                }
            }
        }

        // This is the last gate for picking a candidate.
        // By this point if still we don't have a candidate, it means that
        // only the sync point instruction remains.
        if (num_candidates == 0)
            candidateArray[num_candidates++] = queuedLIREntries.size() - 1;

        // Reinitialize chosenIdx1 since it was used earlier for
        // finding smallest ready time
        chosenIdx1 = queuedLIREntries.size();

        // Pick candidate that is on the critical path
        for (unsigned int i = 0; i < num_candidates; i++) {
            lirID = candidateArray[i];
            // Always try to pick a candidate. Once we've picked one,
            // then we can start looking for another with a longer
            // critical path
            if (chosenIdx1 == queuedLIREntries.size()
                    || queuedLIREntries[lirID]->longestPath
                            > queuedLIREntries[chosenIdx1]->longestPath) {
                chosenIdx1 = lirID;
            }
        }

        // By the time we get to this point, we better have picked
        // an instruction to schedule OR ELSE ...
        assert (chosenIdx1 < queuedLIREntries.size());

        // Pick 2 candidates if possible.
        // If current candidate must issue on both ports, we cannot pick another
        if (queuedLIREntries[chosenIdx1]->portType == BOTH_PORTS)
            num_candidates = 0;

        // The only way we will go through this logic is if chosen instruction
        // doesn't issue on both ports
        for (unsigned int i = 0; i < num_candidates; i++) {
            lirID = candidateArray[i];
            if (lirID == chosenIdx1)
                continue; // Should skip the one already chosen

            // Check for port conflict
            if (queuedLIREntries[lirID]->portType == BOTH_PORTS)
                continue; // Look for another one that doesn't issue on both ports
            if (queuedLIREntries[chosenIdx1]->portType == EITHER_PORT
                    || queuedLIREntries[lirID]->portType == EITHER_PORT
                    || (queuedLIREntries[chosenIdx1]->portType == PORT0
                            && queuedLIREntries[lirID]->portType == PORT1)
                    || (queuedLIREntries[chosenIdx1]->portType == PORT1
                            && queuedLIREntries[lirID]->portType == PORT0)) {
                // Looks like we found one that doesn't conflict on ports
                // However, still try to find one on critical path
                if (chosenIdx2 == queuedLIREntries.size()
                        || queuedLIREntries[lirID]->longestPath
                                > queuedLIREntries[chosenIdx2]->longestPath) {
                    chosenIdx2 = lirID;
                }
            }
        }
#ifndef DISABLE_DEBUG_ATOM_SCHEDULER
        ALOGD("pick ready instructions at slots %d %d\n", chosenIdx1, chosenIdx2);
#endif

        scheduledLIREntries.push_back(queuedLIREntries[chosenIdx1]);
        dvmSetBit(scheduledOps, chosenIdx1);
        dvmClearBit(readyOps, chosenIdx1);
        queuedLIREntries[chosenIdx1]->scheduledTime = currentTime;
        numScheduled++;

        if (chosenIdx2 < queuedLIREntries.size()) {
            scheduledLIREntries.push_back(queuedLIREntries[chosenIdx2]);
            dvmSetBit(scheduledOps, chosenIdx2);
            dvmClearBit(readyOps, chosenIdx2);
            queuedLIREntries[chosenIdx2]->scheduledTime = currentTime;
            numScheduled++;
        }

        // Since we have scheduled instructions in this cycle, we should
        // update the ready queue now to find new instructions whose
        // dependencies have been satisfied
        updateReadyOps(chosenIdx1, scheduledOps, readyOps);
        if (chosenIdx2 < queuedLIREntries.size())
            updateReadyOps(chosenIdx2, scheduledOps, readyOps);

        // Advance time to next cycle
        currentTime++;
    }

    // Make sure that original and scheduled basic blocks are same size
    if (scheduledLIREntries.size() != queuedLIREntries.size()) {
        ALOGI("JIT_INFO: (Atom Scheduler) Original basic block is not same \
                size as the scheduled basic block");
        SET_JIT_ERROR(kJitErrorInsScheduling);
        return;
    }

    // Make sure that basic block delimiter mnemonic is always last one in
    // scheduled basic block
    if (isBasicBlockDelimiter(queuedLIREntries.back()->opCode)
            && !isBasicBlockDelimiter(scheduledLIREntries.back()->opCode)) {
        ALOGI("JIT_INFO: (Atom Scheduler) Sync point should be the last \
                scheduled instruction.");
        SET_JIT_ERROR(kJitErrorInsScheduling);
        return;
    }
}

//! \brief Called to signal the scheduler that the native basic block it has
//! been building is finished.
//! \details This method should be called from other modules to signal that the
//! native basic block the Scheduler has been building is finished. This has
//! side effects because it starts the scheduling process using already created
//! dependency graphs and then updates the code stream with the scheduled
//! instructions.
//! \warning Jumps to immediate must signal end of native basic block for target.
//! If the target has a label, then this is not a problem. But if jumping to an
//! address without label, this method must be called before building dependency
//! graph for target basic block.
void Scheduler::signalEndOfNativeBasicBlock() {
    if(queuedLIREntries.empty())
            return; // No need to do any work

#ifndef DISABLE_DEPENDENGY_GRAPH_DEBUG
    std::ostringstream dependGraphFileName;
    char * streamStartBasicBlock = stream;
    dependGraphFileName << "depengraph_" << gDvm.threadList[0].systemTid << "_"
            << std::hex << (int)streamStartBasicBlock;
#endif

    printStatistics(true /*prescheduling*/);
    schedule();
    printStatistics(false /*prescheduling*/);

    for(unsigned int k = 0; k < scheduledLIREntries.size(); ++k) {
        generateAssembly(scheduledLIREntries[k]);
    }

#ifndef DISABLE_DEPENDENGY_GRAPH_DEBUG
    printDependencyGraph("/data/local/tmp/", dependGraphFileName.str(),
            streamStartBasicBlock, true, true, true, true, true);
#endif

    // Clear all scheduler data structures
    this->reset();
}

#ifndef DISABLE_DEBUG_ATOM_SCHEDULER
//! \brief Transforms LowOpndType enum to string
//! \see LowOpndType
inline const char * operandTypeToString(LowOpndType type) {
    switch (type) {
    case LowOpndType_Imm:
        return "Imm";
    case LowOpndType_Reg:
        return "Reg";
    case LowOpndType_Mem:
        return "Mem";
    case LowOpndType_Label:
        return "Label";
    case LowOpndType_BlockId:
        return "BlockId";
    case LowOpndType_Chain:
        return "Chain";
    }
    return "-";
}
#endif

//! \brief Returns a scaled distance between two basic blocks.
//! \details Computes the Hamming distance between two basic blocks and then scales
//! result by block size and turns it into percentage. block1 and block2 must
//! have same size.
//! \retval scaled Hamming distance
inline double Scheduler::basicBlockEditDistance(const NativeBasicBlock & block1,
        const NativeBasicBlock & block2) {
#ifndef DISABLE_ATOM_SCHEDULING_STATISTICS
    int distance = 0;
    assert(block1.size() == block2.size());
    for(unsigned int i = 0; i < block1.size(); ++i) {
        if(block1[i] != block2[i]) {
            distance += 1;
        }
    }
    return (distance * 100.0) / block1.size();
#else
    return 0.0;
#endif
}

//! \brief Prints Atom Instruction Scheduling statistics.
//! Details prints block size and basic block difference.
//! \todo Comparing basic block latencies pre and post scheduling is a useful
//! statistic.
//! \param prescheduling is used to indicate whether the statistics are requested
//! before the scheduling
void Scheduler::printStatistics(bool prescheduling) {
#ifndef DISABLE_ATOM_SCHEDULING_STATISTICS
    const char * message_tag =
            prescheduling ?
                    "Atom Sched Stats: Pre-schedule:" :
                    "Atom Sched Stats: Post-schedule:";
    NativeBasicBlock * lowOpList;
    if (prescheduling)
        lowOpList = &queuedLIREntries;
    else
        lowOpList = &scheduledLIREntries;

    ALOGD("%s The block size is %d\n", message_tag, lowOpList->size());
    if (!prescheduling) {
        ALOGD("%s Difference in basic blocks after scheduling is %5.2f%%\n",
                message_tag, basicBlockEditDistance(queuedLIREntries, scheduledLIREntries));
    }
#endif
}

//! \brief Prints dependency graph in dot format
//! \details Creates dot files in /data/local/tmp with every basic block
//! that has been scheduled.
//! \param directoryPath the path to the directory
//! \param dgfilename Name to use for dot file created
//! \param startStream The pointer to the start of code cache stream where
//! the basic block has been encoded
//! \param printScheduledTime Allow printing of scheduled time of each LIR
//! \param printIssuePort Allow printing of issue port of each LIR
//! \param printInstructionLatency Allow printing of latency of each LIR
//! \param printCriticalPath Allows printing of longest path latency for each
//! LIR.
//! \param printOriginalOrder Appends to front of instruction the original
//! instruction order before scheduling.
void Scheduler::printDependencyGraph(const char * directoryPath,
        const std::string &dgfilename, const char * startStream,
        bool printScheduledTime, bool printIssuePort,
        bool printInstructionLatency, bool printCriticalPath,
        bool printOriginalOrder) {
#ifndef DISABLE_DEPENDENGY_GRAPH_DEBUG
    std::ofstream depengraphfile;
    const unsigned int maxInstSize = 30;
    char decodedInst[maxInstSize];

    // Create dot file
    std::string completeFSPath = directoryPath;
    completeFSPath.append(dgfilename);
    completeFSPath.append(".dot");
    ALOGD("Dumping dependency graph to %s", completeFSPath.c_str());
    depengraphfile.open(completeFSPath.c_str(), std::ios::out);

    // A little error handling
    if (depengraphfile.fail()) {
        ALOGD("Encountered error when trying to open the file %s",
                completeFSPath.c_str());
        depengraphfile.close();
        return;
    }

    // Print header
    depengraphfile << "digraph BB" << dgfilename.c_str() << " {" << std::endl;
    depengraphfile << "forcelabels = true" << std::endl;

    // Print nodes
    for (unsigned int i = 0; i < scheduledLIREntries.size(); ++i) {
        startStream = decoder_disassemble_instr(const_cast<char *>(startStream),
                decodedInst, maxInstSize);
        // Add node with the x86 instruction as label
        depengraphfile << "LIR" << scheduledLIREntries[i]->slotId
                << " [shape=record, label=\"{";
        if (printOriginalOrder)
            depengraphfile << scheduledLIREntries[i]->slotId << ": ";
        depengraphfile << decodedInst;
        if (printScheduledTime) // Conditional print of time instruction was scheduled
            depengraphfile << " | ScheduledTime:"
                    << scheduledLIREntries[i]->scheduledTime;
        if (printIssuePort) // Conditional print of issue port of instruction
            // I promise the cast is safe
            depengraphfile << " | IssuePort:"
                    << getIssuePort(
                            static_cast<IssuePort>(scheduledLIREntries[i]->portType));
        if (printInstructionLatency) // Conditional print of instruction latency
            depengraphfile << " | Latency:"
                    << scheduledLIREntries[i]->instructionLatency;
        if (printCriticalPath) // Conditional print of critical path
            depengraphfile << " | LongestPath:"
                    << scheduledLIREntries[i]->longestPath;
        // Close label
        depengraphfile << "}\"";
        // Close node attributes
        depengraphfile << "]" << std::endl;
    }

    // Print edge between each node and its successors
    for (unsigned int i = 0; i < scheduledLIREntries.size(); ++i) {
        // It is possible that successorDependencies contains duplicates
        // So we set up a set here to avoid creating multiple edges
        std::set<int> successors;
        for (unsigned int j = 0;
                j < dependencyAssociation[scheduledLIREntries[i]].successorDependencies.size(); ++j) {
            int successorSlotId = dependencyAssociation[scheduledLIREntries[i]].successorDependencies[j].lowopSlotId;
            if(successors.find(successorSlotId) != successors.end())
                continue; // If we already generated edge for this successor, don't generate another
            successors.insert(successorSlotId);
            depengraphfile << "LIR" << scheduledLIREntries[i]->slotId << "->LIR"
                    << dependencyAssociation[scheduledLIREntries[i]].successorDependencies[j].lowopSlotId
                    << std::endl;
        }
    }
    depengraphfile << "}" << std::endl;
    depengraphfile.close();
#endif
}
