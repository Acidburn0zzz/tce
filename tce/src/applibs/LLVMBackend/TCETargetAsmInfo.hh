//=====-- TCETargetAsmInfo.h - TCE asm properties -------------*- C++ -*--====//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source 
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the TCETargetAsmInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef TCETARGETASMINFO_H
#define TCETARGETASMINFO_H

#include <llvm/Target/TargetAsmInfo.h>

namespace llvm {
  class Target;
  class StringRef;
  
  class TCETargetAsmInfo : public TargetAsmInfo {
  public:    
    explicit TCETargetAsmInfo(const Target &T, const StringRef &TT);
  };

} // namespace llvm

#endif