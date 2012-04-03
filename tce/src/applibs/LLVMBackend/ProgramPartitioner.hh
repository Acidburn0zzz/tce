/*
    Copyright (c) 2012 Tampere University of Technology.

    This file is part of TTA-Based Codesign Environment (TCE).

    Permission is hereby granted, free of charge, to any person obtaining a
    copy of this software and associated documentation files (the "Software"),
    to deal in the Software without restriction, including without limitation
    the rights to use, copy, modify, merge, publish, distribute, sublicense,
    and/or sell copies of the Software, and to permit persons to whom the
    Software is furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in
    all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
    THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
    DEALINGS IN THE SOFTWARE.
 */
/**
 * @file ProgramPartitioner.hh
 *
 * Declaration of ProgramPartitioner class. A MachineFunctionPass that chooses
 * more restricted register classes for program's variables. The register classes
 * are chosen based on the connectivity and in the case of vector load sources
 * in a clustered regular machine, according to the vector lane index. In case
 * of clustered machines the chosen register classes map the register files in
 * each cluster node, therefore producing a node partitioning for the program.
 *
 * @author Pekka Jääskeläinen 2012
 */

#ifndef TCE_PROGRAM_PARTITIONER_HH
#define TCE_PROGRAM_PARTITIONER_HH

#include "llvm/CodeGen/Passes.h"
#include "llvm/Constant.h"
#include "llvm/Constants.h"
#include "llvm/Instructions.h"
#include "llvm/Function.h"
#include "llvm/Pass.h"
#include "llvm/Type.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/Support/CFG.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Target/TargetInstrInfo.h"
#include "llvm/ADT/DepthFirstIterator.h"

#include "llvm/Analysis/CallGraph.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/Module.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/ADT/StringMap.h"


struct ProgramPartitioner : public llvm::MachineFunctionPass {
    static char ID;
    ProgramPartitioner() : MachineFunctionPass(ID) {}

    virtual bool doInitialization(llvm::Module &M);
    virtual bool runOnMachineFunction(llvm::MachineFunction &MF);
    virtual bool doFinalization(llvm::Module &M);

    virtual const char *getPassName() const {
        return "TCE: program variables to register file partitioner";
    }

public:
};

#endif