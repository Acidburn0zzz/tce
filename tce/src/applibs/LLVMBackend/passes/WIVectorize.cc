//===- WIVectorize.cpp - A Work Item Vectorizer -------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements a basic-block vectorization pass. The algorithm was
// inspired by that used by the Vienna MAP Vectorizor by Franchetti and Kral,
// et al. It works by looking for chains of pairable operations and then
// pairing them.
// Additional options are provided to vectorize only candidate from differnt
// work items according to metadata provided by 'pocl' frontend 
// (launchpad.net/pocl). 
// Additional option is also available to vectorize loads and stores only.
// Still work in progress by vladimir guzma [at] tut fi.
//
//===----------------------------------------------------------------------===//

#define WIV_NAME "wi-vectorize"
#define DEBUG_TYPE WIV_NAME
#include "llvm/Constants.h"
#include "llvm/DerivedTypes.h"
#include "llvm/Function.h"
#include "llvm/Instructions.h"
#include "llvm/IntrinsicInst.h"
#include "llvm/Intrinsics.h"
#include "llvm/LLVMContext.h"
#include "llvm/Pass.h"
#include "llvm/Type.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/AliasSetTracker.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/ValueHandle.h"
#include "llvm/Target/TargetData.h"
#include "llvm/Transforms/Vectorize.h"
#include "llvm/Metadata.h"
#include <algorithm>
#include <map>
#include <iostream>
using namespace llvm;

static cl::opt<unsigned>
ReqChainDepth("wi-vectorize-req-chain-depth", cl::init(2), cl::Hidden,
  cl::desc("The required chain depth for vectorization"));

static cl::opt<unsigned>
VectorWidth("wi-vectorize-vector-width", cl::init(8), cl::Hidden,
  cl::desc("The width of the machine vector in words."));

static cl::opt<bool>
NoMath("wi-vectorize-no-math", cl::init(false), cl::Hidden,
  cl::desc("Don't try to vectorize floating-point math intrinsics"));

static cl::opt<bool>
NoFMA("wi-vectorize-no-fma", cl::init(false), cl::Hidden,
  cl::desc("Don't try to vectorize the fused-multiply-add intrinsic"));

static cl::opt<bool>
NoMemOps("wi-vectorize-no-mem-ops", cl::init(false), cl::Hidden,
  cl::desc("Don't try to vectorize loads and stores"));

static cl::opt<bool>
AlignedOnly("wi-vectorize-aligned-only", cl::init(false), cl::Hidden,
  cl::desc("Only generate aligned loads and stores"));

static cl::opt<bool>
FastDep("wi-vectorize-fast-dep", cl::init(false), cl::Hidden,
  cl::desc("Use a fast instruction dependency analysis"));

static cl::opt<bool>
MemOpsOnly("wi-vectorize-mem-ops-only", cl::init(false), cl::Hidden,
  cl::desc("Try to vectorize loads and stores only"));

static cl::opt<bool>
AllAtOnce("wi-vectorize-all-at-once", cl::init(false), cl::Hidden,
  cl::desc("Try to vectorize whole candidate set at once."));

#ifndef NDEBUG
static cl::opt<bool>
DebugInstructionExamination("wi-vectorize-debug-instruction-examination",
  cl::init(false), cl::Hidden,
  cl::desc("When debugging is enabled, output information on the"
           " instruction-examination process"));
static cl::opt<bool>
DebugInstructionExaminationWi("wi-vectorize-debug-instruction-examination-wi",
    cl::init(false), cl::Hidden,
    cl::desc("When debugging is enabled, output information on the"
             " instruction-examination process related to work items"));           
static cl::opt<bool>
DebugCandidateSelection("wi-vectorize-debug-candidate-selection",
  cl::init(false), cl::Hidden,
  cl::desc("When debugging is enabled, output information on the"
           " candidate-selection process"));
static cl::opt<bool>
DebugPairSelection("wi-vectorize-debug-pair-selection",
  cl::init(false), cl::Hidden,
  cl::desc("When debugging is enabled, output information on the"
           " pair-selection process"));
static cl::opt<bool>
DebugCycleCheck("wi-vectorize-debug-cycle-check",
  cl::init(false), cl::Hidden,
  cl::desc("When debugging is enabled, output information on the"
           " cycle-checking process"));
#endif

STATISTIC(NumFusedOps, "Number of operations fused by wi-vectorize");

namespace llvm {
    FunctionPass* createWIVectorizePass();    
}
namespace {
  struct WIVectorize : public FunctionPass {
    static char ID; // Pass identification, replacement for typeid
    WIVectorize() : FunctionPass(ID) {}

    typedef std::pair<Value *, Value *> ValuePair;
    typedef std::pair<ValuePair, size_t> ValuePairWithDepth;
    typedef std::pair<ValuePair, ValuePair> VPPair; // A ValuePair pair
    typedef std::pair<std::multimap<Value *, Value *>::iterator,
              std::multimap<Value *, Value *>::iterator> VPIteratorPair;
    typedef std::pair<std::multimap<ValuePair, ValuePair>::iterator,
              std::multimap<ValuePair, ValuePair>::iterator>
                VPPIteratorPair;
    typedef std::vector<Value *> ValueVector;
    typedef DenseMap<Value*, ValueVector*> ValueVectorMap;
    typedef std::pair<ValueVector, ValueVector> VVPair; // A ValuePair pair
    /*typedef std::pair<std::multimap<Value *, Value *>::iterator,
              std::multimap<Value *, Value *>::iterator> VPIteratorPair;
    typedef std::pair<std::multimap<ValuePair, ValuePair>::iterator,
              std::multimap<ValuePair, ValuePair>::iterator>
                VPPIteratorPair;*/

    AliasAnalysis *AA;
    ScalarEvolution *SE;
    TargetData *TD;
    DenseMap<Value*, Value*> storedSources;
    std::multimap<Value*, Value*> flippedStoredSources;
    // FIXME: const correct?

    bool vectorizePairs(BasicBlock &BB);
    
    bool vectorizeVectors(BasicBlock &BB);

    bool vectorizePhiNodes(BasicBlock &BB);
    
    bool getCandidatePairs(BasicBlock &BB,
                       BasicBlock::iterator &Start,
                       std::multimap<Value *, Value *> &CandidatePairs,
                       std::vector<Value *> &PairableInsts);

    bool getCandidateVectors(BasicBlock &BB,
                       BasicBlock::iterator &Start,
                       ValueVectorMap &CandidatePairs,
                       std::vector<Value *> &PairableInsts);

    void computeConnectedPairs(std::multimap<Value *, Value *> &CandidatePairs,
                       std::vector<Value *> &PairableInsts,
                       std::multimap<ValuePair, ValuePair> &ConnectedPairs);

    void buildDepMap(BasicBlock &BB,
                       std::multimap<Value *, Value *> &CandidatePairs,
                       std::vector<Value *> &PairableInsts,
                       DenseSet<ValuePair> &PairableInstUsers);

    void choosePairs(std::multimap<Value *, Value *> &CandidatePairs,
                        std::vector<Value *> &PairableInsts,
                        std::multimap<ValuePair, ValuePair> &ConnectedPairs,
                        DenseSet<ValuePair> &PairableInstUsers,
                        DenseMap<Value *, Value *>& ChosenPairs);

    void fuseChosenPairs(BasicBlock &BB,
                     std::vector<Value *> &PairableInsts,
                     DenseMap<Value *, Value *>& ChosenPairs);
    
    void fuseChosenVectors(BasicBlock &BB,
                     std::vector<Value *> &PairableInsts,
                     ValueVectorMap& ChosenPairs);
    
    void dropUnused(BasicBlock &BB);
    
    bool isInstVectorizable(Instruction *I, bool &IsSimpleLoadStore);

    bool areInstsCompatible(Instruction *I, Instruction *J,
                       bool IsSimpleLoadStore);

    bool areInstsCompatibleFromDifferentWi(Instruction *I, Instruction *J);

    bool trackUsesOfI(DenseSet<Value *> &Users,
                      AliasSetTracker &WriteSet, Instruction *I,
                      Instruction *J, bool UpdateUsers = true,
                      std::multimap<Value *, Value *> *LoadMoveSet = 0);

    void computePairsConnectedTo(
                      std::multimap<Value *, Value *> &CandidatePairs,
                      std::vector<Value *> &PairableInsts,
                      std::multimap<ValuePair, ValuePair> &ConnectedPairs,
                      ValuePair P);

    bool pairsConflict(ValuePair P, ValuePair Q,
                 DenseSet<ValuePair> &PairableInstUsers,
                 std::multimap<ValuePair, ValuePair> *PairableInstUserMap = 0);

    bool pairWillFormCycle(ValuePair P,
                       std::multimap<ValuePair, ValuePair> &PairableInstUsers,
                       DenseSet<ValuePair> &CurrentPairs);

    void pruneTreeFor(
                      std::multimap<Value *, Value *> &CandidatePairs,
                      std::vector<Value *> &PairableInsts,
                      std::multimap<ValuePair, ValuePair> &ConnectedPairs,
                      DenseSet<ValuePair> &PairableInstUsers,
                      std::multimap<ValuePair, ValuePair> &PairableInstUserMap,
                      DenseMap<Value *, Value *> &ChosenPairs,
                      DenseMap<ValuePair, size_t> &Tree,
                      DenseSet<ValuePair> &PrunedTree, ValuePair J,
                      bool UseCycleCheck);

    void buildInitialTreeFor(
                      std::multimap<Value *, Value *> &CandidatePairs,
                      std::vector<Value *> &PairableInsts,
                      std::multimap<ValuePair, ValuePair> &ConnectedPairs,
                      DenseSet<ValuePair> &PairableInstUsers,
                      DenseMap<Value *, Value *> &ChosenPairs,
                      DenseMap<ValuePair, size_t> &Tree, ValuePair J);

    void findBestTreeFor(
                      std::multimap<Value *, Value *> &CandidatePairs,
                      std::vector<Value *> &PairableInsts,
                      std::multimap<ValuePair, ValuePair> &ConnectedPairs,
                      DenseSet<ValuePair> &PairableInstUsers,
                      std::multimap<ValuePair, ValuePair> &PairableInstUserMap,
                      DenseMap<Value *, Value *> &ChosenPairs,
                      DenseSet<ValuePair> &BestTree, size_t &BestMaxDepth,
                      size_t &BestEffSize, VPIteratorPair ChoiceRange,
                      bool UseCycleCheck);

    Value *getReplacementPointerInput(LLVMContext& Context, Instruction *I,
                     Instruction *J, unsigned o, bool &FlipMemInputs);

    Value *getReplacementPointerInput(LLVMContext& Context, Instruction *I,
                     ValueVector *vec, unsigned o);

    void fillNewShuffleMask(LLVMContext& Context, Instruction *J,
                     unsigned NumElem, unsigned MaskOffset, unsigned NumInElem,
                     unsigned IdxOffset, std::vector<Constant*> &Mask);

    Value *getReplacementShuffleMask(LLVMContext& Context, Instruction *I,
                     Instruction *J);

    Value *getReplacementInput(LLVMContext& Context, Instruction *I,
                     Instruction *J, unsigned o, bool FlipMemInputs);

    Value *getReplacementInput(LLVMContext& Context, Instruction *I,
                     ValueVector *vec, unsigned o);
    Value* CommonShuffleSource(Instruction *I, Instruction *J, unsigned o);
    void getReplacementInputsForPair(LLVMContext& Context, Instruction *I,
                     Instruction *J, SmallVector<Value *, 3> &ReplacedOperands,
                     bool &FlipMemInputs);

    void getReplacementInputsForVector(LLVMContext& Context, Instruction *I,
                     ValueVector* vec, SmallVector<Value *, 3> &ReplacedOperands);

    void replaceOutputsOfPair(LLVMContext& Context, Instruction *I,
                     Instruction *J, Instruction *K,
                     Instruction *&InsertionPt, Instruction *&K1,
                     Instruction *&K2, bool &FlipMemInputs);

    void replaceOutputsOfVector(LLVMContext& Context, Instruction *I,
                     ValueVector *vec, Instruction *K,
                     Instruction *&InsertionPt, ValueVector *newVec);

    void collectPairLoadMoveSet(BasicBlock &BB,
                     DenseMap<Value *, Value *> &ChosenPairs,
                     std::multimap<Value *, Value *> &LoadMoveSet,
                     Instruction *I);

    void collectLoadMoveSet(BasicBlock &BB,
                     std::vector<Value *> &PairableInsts,
                     DenseMap<Value *, Value *> &ChosenPairs,
                     std::multimap<Value *, Value *> &LoadMoveSet);

    void collectLoadMoveSet(BasicBlock &BB,
                     std::vector<Value *> &PairableInsts,
                     ValueVectorMap &ChosenVectors,
                     std::multimap<Value *, Value *> &LoadMoveSet);

    bool canMoveUsesOfIAfterJ(BasicBlock &BB,
                     std::multimap<Value *, Value *> &LoadMoveSet,
                     Instruction *I, Instruction *J);

    void moveUsesOfIAfterJ(BasicBlock &BB,
                     std::multimap<Value *, Value *> &LoadMoveSet,
                     Instruction *&InsertionPt,
                     Instruction *I, Instruction *J);
    
    bool doInitialization(Module& m) {
      return false;
    }
    bool doFinalization(Module& m) {
      return false;
    }
    virtual bool runOnFunction(Function &Func) {
      AA = &getAnalysis<AliasAnalysis>();
      SE = &getAnalysis<ScalarEvolution>();
      TD = getAnalysisIfAvailable<TargetData>();
      
      bool changed = false;      
      for (Function::iterator i = Func.begin();
         i != Func.end(); i++) {
	changed |=runOnBasicBlock(*i);
      }
      return changed;
    }
    
    virtual bool runOnBasicBlock(BasicBlock &BB) {

      bool changed = false;
      if (AllAtOnce) {
          return vectorizeVectors(BB);
      } else {
        // Iterate a sufficient number of times to merge types of size 1 bit,
        // then 2 bits, then 4, etc. up to half of the target vector width of the
        // target vector register.
        for (unsigned v = 2, n = 1; v <= VectorWidth;
            v *= 2, ++n) {
            DEBUG(dbgs() << "WIV: fusing loop #" << n << 
                " for " << BB.getName() << " in " <<
                BB.getParent()->getName() << "...\n");
            if (vectorizePairs(BB))
            changed = true;
            else
            break;
        }
      }
      if (changed)
        vectorizePhiNodes(BB);
      //dropUnused(BB);
      DEBUG(dbgs() << "WIV: done!\n");
      return changed;
    }

    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      FunctionPass::getAnalysisUsage(AU);
      AU.addRequired<AliasAnalysis>();
      AU.addRequired<ScalarEvolution>();
      AU.addPreserved<AliasAnalysis>();
      AU.addPreserved<ScalarEvolution>();
      AU.setPreservesCFG();
    }
    // This returns the vector type that holds a pair of the provided type.
    // If the provided type is already a vector, then its length is doubled.
    static inline VectorType *getVecTypeForVector(Type *ElemTy) {
      if (VectorType *VTy = dyn_cast<VectorType>(ElemTy)) {
        unsigned numElem = VTy->getNumElements();
        return VectorType::get(ElemTy->getScalarType(), numElem*VectorWidth);
      } else {
        return VectorType::get(ElemTy->getScalarType(), VectorWidth);
          
      }

      return VectorType::get(ElemTy, 2);
    }
    // This returns the vector type that holds a pair of the provided type.
    // If the provided type is already a vector, then its length is doubled.
    static inline VectorType *getVecTypeForPair(Type *ElemTy) {
      if (VectorType *VTy = dyn_cast<VectorType>(ElemTy)) {
        unsigned numElem = VTy->getNumElements();
        return VectorType::get(ElemTy->getScalarType(), numElem*2);
      }

      return VectorType::get(ElemTy, 2);
    }
    std::string getReplacementName(Instruction *I, bool IsInput, unsigned o,
                        unsigned n = 0) {
        if (!I->hasName())
        return "";

        return (I->getName() + (IsInput ? ".v.i" : ".v.r") + utostr(o) +
                (n > 0 ? "." + utostr(n) : "")).str();
    }

    // Returns the weight associated with the provided value. A chain of
    // candidate pairs has a length given by the sum of the weights of its
    // members (one weight per pair; the weight of each member of the pair
    // is assumed to be the same). This length is then compared to the
    // chain-length threshold to determine if a given chain is significant
    // enough to be vectorized. The length is also used in comparing
    // candidate chains where longer chains are considered to be better.
    // Note: when this function returns 0, the resulting instructions are
    // not actually fused.
    static inline size_t getDepthFactor(Value *V) {
      // InsertElement and ExtractElement have a depth factor of zero. This is
      // for two reasons: First, they cannot be usefully fused. Second, because
      // the pass generates a lot of these, they can confuse the simple metric
      // used to compare the trees in the next iteration. Thus, giving them a
      // weight of zero allows the pass to essentially ignore them in
      // subsequent iterations when looking for vectorization opportunities
      // while still tracking dependency chains that flow through those
      // instructions.
      if (isa<InsertElementInst>(V) || isa<ExtractElementInst>(V))
        return 0;

      // Give a load or store half of the required depth so that load/store
      // pairs will vectorize.
      if ((isa<LoadInst>(V) || isa<StoreInst>(V)))
        return ReqChainDepth;
        
      return 1;
    }

    // This determines the relative offset of two loads or stores, returning
    // true if the offset could be determined to be some constant value.
    // For example, if OffsetInElmts == 1, then J accesses the memory directly
    // after I; if OffsetInElmts == -1 then I accesses the memory
    // directly after J. This function assumes that both instructions
    // have the same type.
    bool getPairPtrInfo(Instruction *I, Instruction *J,
        Value *&IPtr, Value *&JPtr, unsigned &IAlignment, unsigned &JAlignment,
        int64_t &OffsetInElmts) {
      OffsetInElmts = 0;
      if (isa<LoadInst>(I)) {
        IPtr = cast<LoadInst>(I)->getPointerOperand();
        JPtr = cast<LoadInst>(J)->getPointerOperand();
        IAlignment = cast<LoadInst>(I)->getAlignment();
        JAlignment = cast<LoadInst>(J)->getAlignment();
      } else {
        IPtr = cast<StoreInst>(I)->getPointerOperand();
        JPtr = cast<StoreInst>(J)->getPointerOperand();
        IAlignment = cast<StoreInst>(I)->getAlignment();
        JAlignment = cast<StoreInst>(J)->getAlignment();
      }

      const SCEV *IPtrSCEV = SE->getSCEV(IPtr);
      const SCEV *JPtrSCEV = SE->getSCEV(JPtr);

      // If this is a trivial offset, then we'll get something like
      // 1*sizeof(type). With target data, which we need anyway, this will get
      // constant folded into a number.
      const SCEV *OffsetSCEV = SE->getMinusSCEV(JPtrSCEV, IPtrSCEV);
      if (const SCEVConstant *ConstOffSCEV =
            dyn_cast<SCEVConstant>(OffsetSCEV)) {
        ConstantInt *IntOff = ConstOffSCEV->getValue();
        int64_t Offset = IntOff->getSExtValue();

        Type *VTy = cast<PointerType>(IPtr->getType())->getElementType();
        int64_t VTyTSS = (int64_t) TD->getTypeStoreSize(VTy);

        assert(VTy == cast<PointerType>(JPtr->getType())->getElementType());

        OffsetInElmts = Offset/VTyTSS;
        return (abs64(Offset) % VTyTSS) == 0;
      }

      return false;
    }

    // Returns true if the provided CallInst represents an intrinsic that can
    // be vectorized.
    bool isVectorizableIntrinsic(CallInst* I) {
      Function *F = I->getCalledFunction();
      if (!F) return false;

      unsigned IID = F->getIntrinsicID();
      if (!IID) return false;

      switch(IID) {
      default:
        return false;
      case Intrinsic::sqrt:
      case Intrinsic::powi:
      case Intrinsic::sin:
      case Intrinsic::cos:
      case Intrinsic::log:
      case Intrinsic::log2:
      case Intrinsic::log10:
      case Intrinsic::exp:
      case Intrinsic::exp2:
      case Intrinsic::pow:
        return !NoMath;
      case Intrinsic::fma:
        return !NoFMA;
      }
    }

    // Returns true if J is the second element in some pair referenced by
    // some multimap pair iterator pair.
    template <typename V>
    bool isSecondInIteratorPair(V J, std::pair<
           typename std::multimap<V, V>::iterator,
           typename std::multimap<V, V>::iterator> PairRange) {
      for (typename std::multimap<V, V>::iterator K = PairRange.first;
           K != PairRange.second; ++K)
        if (K->second == J) return true;

      return false;
    }
  };
    // Replace phi nodes of individual valiables with vector they originated 
    // from.
    bool WIVectorize::vectorizePhiNodes(BasicBlock &BB) {
        BasicBlock::iterator Start = BB.begin();
        BasicBlock::iterator End = BB.getFirstInsertionPt();
        //BB.dump();
        ValueVectorMap valueMap;
        for (BasicBlock::iterator I = Start; I != End; ++I) {
            PHINode* node = dyn_cast<PHINode>(I);
            if (node) {
                ValueVector* candidateVector = new ValueVector;
                for (BasicBlock::iterator J = llvm::next(I);
                    J != End; ++J) {
                    PHINode* node2 = dyn_cast<PHINode>(J);
                    if (node2) {
                        bool match = true;
                        if (node->getNumIncomingValues() != 
                            node2->getNumIncomingValues())
                            continue;
                        
                        for (int i = 0; i < node->getNumIncomingValues(); i++) {
                            Value* v1 = node->getIncomingValue(i);
                            Value* v2 = node2->getIncomingValue(i);
                            if (node->getIncomingBlock(i) != 
                                node2->getIncomingBlock(i)) {
                                match = false;
                            }
                            // Stored sources contain original value from
                            // which one in phi node was extracted from
                            DenseMap<Value*, Value*>::iterator vi = 
                                storedSources.find(v1);
                            if (vi != storedSources.end()) {
                                DenseMap<Value*, Value*>::iterator ji =
                                    storedSources.find(v2);
                                if (ji != storedSources.end() &&
                                    (*vi).second == (*ji).second) {
                                } else {
                                    match = false;
                                }
                            } else {
                                // Incaming value can be also constant, they 
                                // have to match.                                
                                Constant* const1 = dyn_cast<Constant>(v1);
                                Constant* const2 = dyn_cast<Constant>(v2);
                                if (!(const1 && const2)) /* && 
                                    const1->getValue() == const2->getValue())) */{
                                    match = false;
                                }
                            }
                        }
                        if (match)
                            candidateVector->push_back(node2);
                    }
                }
                if (candidateVector->size() == VectorWidth -1) {
                    Value* newV = cast<Value>(node);
                    valueMap[newV] = candidateVector;
                }
            }
        }
        // Actually create new phi node
        for (DenseMap<Value*, ValueVector*>::iterator i =
            valueMap.begin(); i != valueMap.end(); i++) {
            ValueVector& v = *(*i).second;
            PHINode* orig = cast<PHINode>((*i).first);          
            Type *IType = orig->getType();
            Type *VType = getVecTypeForVector(IType);          
            PHINode* phi = PHINode::Create(VType, orig->getNumIncomingValues(),
                    getReplacementName(orig, false,0), orig);
            // Add incoming pairs to the phi node.
            for (int i = 0; i < orig->getNumIncomingValues(); i++) {
                Value* inc = orig->getIncomingValue(i);
                BasicBlock* BB = orig->getIncomingBlock(i);
                DenseMap<Value*, Value*>::iterator iter = 
                    storedSources.find(inc);
                if (iter != storedSources.end()) {
                    phi->addIncoming((*iter).second, BB);
                } else {
                    Constant* origConst = cast<Constant>(inc);
                    Constant* cons = ConstantVector::getSplat(                      
                        VectorWidth, origConst);
                    phi->addIncoming(cons, BB);
                }
            }
            // Extract scalar values from phi node to be used in the body 
            // of basic block. Replacing their uses cause instruction combiner
            // to find extractlement -> insertelement pairs and drop them
            // leaving direct use of vector.
            LLVMContext& Context = BB.getContext();
            BasicBlock::iterator toFill = BB.getFirstInsertionPt();
            Value *X = ConstantInt::get(Type::getInt32Ty(Context), 0);       
            Instruction* other = ExtractElementInst::Create(phi, X,
                                            getReplacementName(phi, false, 0));
            other->insertAfter(toFill);
            orig->replaceAllUsesWith(other);
            AA->replaceWithNewValue(orig, other);
            SE->forgetValue(orig);
            orig->eraseFromParent();
            Instruction* ins = other;
            for (int i = 0; i < v.size(); i++) {
                X = ConstantInt::get(Type::getInt32Ty(Context), i+1);            
                Instruction* other = ExtractElementInst::Create(phi, X,
                                            getReplacementName(phi, false, i+1));
                other->insertAfter(ins);
                Instruction* tmp = cast<Instruction>(v[i]);            
                tmp->replaceAllUsesWith(other);
                AA->replaceWithNewValue(tmp, other);  
                SE->forgetValue(tmp);
                tmp->eraseFromParent();
                ins = other;
            }          
            
        }
        //BB.dump();
        return true;      
    }
  // This function implements one vectorization iteration on the provided
  // basic block. It returns true if the block is changed.
  bool WIVectorize::vectorizePairs(BasicBlock &BB) {
    bool ShouldContinue;
    BasicBlock::iterator Start = BB.getFirstInsertionPt();

    std::vector<Value *> AllPairableInsts;
    DenseMap<Value *, Value *> AllChosenPairs;

    do {
      std::vector<Value *> PairableInsts;
      std::multimap<Value *, Value *> CandidatePairs;
      ShouldContinue = getCandidatePairs(BB, Start, CandidatePairs,
                                         PairableInsts);
      if (PairableInsts.empty()) continue;

      // Now we have a map of all of the pairable instructions and we need to
      // select the best possible pairing. A good pairing is one such that the
      // users of the pair are also paired. This defines a (directed) forest
      // over the pairs such that two pairs are connected iff the second pair
      // uses the first.

      // Note that it only matters that both members of the second pair use some
      // element of the first pair (to allow for splatting).

      std::multimap<ValuePair, ValuePair> ConnectedPairs;
      computeConnectedPairs(CandidatePairs, PairableInsts, ConnectedPairs);
      if (ConnectedPairs.empty() && !MemOpsOnly) continue;

      // Build the pairable-instruction dependency map
      DenseSet<ValuePair> PairableInstUsers;
      buildDepMap(BB, CandidatePairs, PairableInsts, PairableInstUsers);

      // There is now a graph of the connected pairs. For each variable, pick
      // the pairing with the largest tree meeting the depth requirement on at
      // least one branch. Then select all pairings that are part of that tree
      // and remove them from the list of available pairings and pairable
      // variables.

      DenseMap<Value *, Value *> ChosenPairs;
      choosePairs(CandidatePairs, PairableInsts, ConnectedPairs,
        PairableInstUsers, ChosenPairs);

      if (ChosenPairs.empty()) continue;
      AllPairableInsts.insert(AllPairableInsts.end(), PairableInsts.begin(),
                              PairableInsts.end());
      AllChosenPairs.insert(ChosenPairs.begin(), ChosenPairs.end());
    } while (ShouldContinue);

    if (AllChosenPairs.empty()) return false;
    NumFusedOps += AllChosenPairs.size();

    // A set of pairs has now been selected. It is now necessary to replace the
    // paired instructions with vector instructions. For this procedure each
    // operand must be replaced with a vector operand. This vector is formed
    // by using build_vector on the old operands. The replaced values are then
    // replaced with a vector_extract on the result.  Subsequent optimization
    // passes should coalesce the build/extract combinations.

    fuseChosenPairs(BB, AllPairableInsts, AllChosenPairs);
    return true;
  }
  // This function implements vectorization iteration on the provided
  // basic block. It returns true if the block is changed.
  bool WIVectorize::vectorizeVectors(BasicBlock &BB) {
    bool ShouldContinue;
    BasicBlock::iterator Start = BB.getFirstInsertionPt();

    std::vector<Value *> VectorizableInsts;
    ValueVectorMap CandidateVectors;
    ShouldContinue = getCandidateVectors(BB, Start, CandidateVectors,
                                         VectorizableInsts);
    if (VectorizableInsts.empty()) return false;

    if (CandidateVectors.empty()) return false;
    NumFusedOps += CandidateVectors.size();

    // A set of pairs has now been selected. It is now necessary to replace the
    // paired instructions with vector instructions. For this procedure each
    // operand must be replaced with a vector operand. This vector is formed
    // by using build_vector on the old operands. The replaced values are then
    // replaced with a vector_extract on the result.  Subsequent optimization
    // passes should coalesce the build/extract combinations.

    fuseChosenVectors(BB, VectorizableInsts, CandidateVectors);
    return true;
  }

  
  // This function returns true if the provided instruction is capable of being
  // fused into a vector instruction. This determination is based only on the
  // type and other attributes of the instruction.
  bool WIVectorize::isInstVectorizable(Instruction *I,
                                         bool &IsSimpleLoadStore) {
    IsSimpleLoadStore = false;

    if (CallInst *C = dyn_cast<CallInst>(I)) {
      if (!isVectorizableIntrinsic(C))
        return false;
    } else if (LoadInst *L = dyn_cast<LoadInst>(I)) {
      // Vectorize simple loads if possbile:
      IsSimpleLoadStore = L->isSimple();
      if (!IsSimpleLoadStore || NoMemOps)
        return false;
    } else if (StoreInst *S = dyn_cast<StoreInst>(I)) {
      // Vectorize simple stores if possbile:
      IsSimpleLoadStore = S->isSimple();
      if (!IsSimpleLoadStore || NoMemOps)
        return false;
    } else if (CastInst *C = dyn_cast<CastInst>(I)) {
      // We can vectorize casts, but not casts of pointer types, etc.

      Type *SrcTy = C->getSrcTy();
      if (!SrcTy->isSingleValueType() || SrcTy->isPointerTy())
        return false;

      Type *DestTy = C->getDestTy();
      if (!DestTy->isSingleValueType() || DestTy->isPointerTy())
        return false;
    } else if (!(I->isBinaryOp())) {/* || isa<ShuffleVectorInst>(I) ||
        isa<ExtractElementInst>(I) || isa<InsertElementInst>(I))) {*/
      return false;
    }

    // We can't vectorize memory operations without target data
    if (TD == 0 && IsSimpleLoadStore)
      return false;

    Type *T1, *T2;
    if (isa<StoreInst>(I)) {
      // For stores, it is the value type, not the pointer type that matters
      // because the value is what will come from a vector register.

      Value *IVal = cast<StoreInst>(I)->getValueOperand();
      T1 = IVal->getType();
    } else {
      T1 = I->getType();
    }

    if (I->isCast())
      T2 = cast<CastInst>(I)->getSrcTy();
    else
      T2 = T1;

    // Not every type can be vectorized...
    if (!(VectorType::isValidElementType(T1) || T1->isVectorTy()) ||
        !(VectorType::isValidElementType(T2) || T2->isVectorTy()))
      return false;

    if (T1->getPrimitiveSizeInBits() > (VectorWidth*32)/2 ||
        T2->getPrimitiveSizeInBits() > (VectorWidth*32)/2)
      return false;

    return true;
  }
    // This function returns true if the two provided instructions are compatible
    // (meaning that they can be fused into a vector instruction). This assumes
    // that I has already been determined to be vectorizable and that J is not
    // in the use tree of I.
    bool WIVectorize::areInstsCompatibleFromDifferentWi(Instruction *I, 
                                                        Instruction *J) {
	if (I->getMetadata("wi") == NULL || J->getMetadata("wi") == NULL) {
	  return false;
	}
        if (MemOpsOnly && 
            !((isa<LoadInst>(I) && isa<LoadInst>(J)) ||
              (isa<StoreInst>(I) && isa<StoreInst>(J)))) 
            return false;
	MDNode* mi = I->getMetadata("wi");
	MDNode* mj = J->getMetadata("wi");
	assert(mi->getNumOperands() == 6);
	assert(mj->getNumOperands() == 6);
	int differs = 0;
	for (unsigned int i = 2; i < mi->getNumOperands() -1; i++) {
	  ConstantInt *CI = dyn_cast<ConstantInt>(mi->getOperand(i));
	  ConstantInt *CJ = dyn_cast<ConstantInt>(mj->getOperand(i));
	  if (CI->getValue() != CJ->getValue()) {
	    differs ++;
	  }
	}
	if (differs == 0) {
	  // Same work item triplet
	  return false;
	}
	// Operand 5 is instruction line
	ConstantInt *CI = dyn_cast<ConstantInt>(mi->getOperand(5));
	ConstantInt *CJ = dyn_cast<ConstantInt>(mj->getOperand(5));
	if (CI->getValue() != CJ->getValue()) {
	  // different line in the original work item
	  // we do not want to vectorize operations that do not match
	  return false;
	}
        return true;
    }

  // This function returns true if the two provided instructions are compatible
  // (meaning that they can be fused into a vector instruction). This assumes
  // that I has already been determined to be vectorizable and that J is not
  // in the use tree of I.
  bool WIVectorize::areInstsCompatible(Instruction *I, Instruction *J,
                       bool IsSimpleLoadStore) {
    DEBUG( if (DebugInstructionExamination) dbgs() << "WIV: looking at " << *I <<
                     " <-> " << *J << "\n");

    // Loads and stores can be merged if they have different alignments,
    // but are otherwise the same.
    LoadInst *LI, *LJ;
    StoreInst *SI, *SJ;
    if ((LI = dyn_cast<LoadInst>(I)) && (LJ = dyn_cast<LoadInst>(J))) {
      if (I->getType() != J->getType()) {
        return false;
      }

      if (LI->getPointerOperand()->getType() !=
            LJ->getPointerOperand()->getType() ||
          LI->isVolatile() != LJ->isVolatile() ||
          LI->getOrdering() != LJ->getOrdering() ||
          LI->getSynchScope() != LJ->getSynchScope()) {
            return false; 
      }
    } else if ((SI = dyn_cast<StoreInst>(I)) && (SJ = dyn_cast<StoreInst>(J))) {
      if (SI->getValueOperand()->getType() !=
            SJ->getValueOperand()->getType() ||
          SI->getPointerOperand()->getType() !=
            SJ->getPointerOperand()->getType() ||
          SI->isVolatile() != SJ->isVolatile() ||
          SI->getOrdering() != SJ->getOrdering() ||
          SI->getSynchScope() != SJ->getSynchScope()) {
            return false;
      }
    } else if (!J->isSameOperationAs(I)) {
      return false;
    }
    // FIXME: handle addsub-type operations!

    if (IsSimpleLoadStore) {
      Value *IPtr, *JPtr;
      unsigned IAlignment, JAlignment;
      int64_t OffsetInElmts = 0;
      if (getPairPtrInfo(I, J, IPtr, JPtr, IAlignment, JAlignment,
            OffsetInElmts) && abs64(OffsetInElmts) == 1) {
            if (AlignedOnly) {
            Type *aType = isa<StoreInst>(I) ?
                cast<StoreInst>(I)->getValueOperand()->getType() : I->getType();
            // An aligned load or store is possible only if the instruction
            // with the lower offset has an alignment suitable for the
            // vector type.

            unsigned BottomAlignment = IAlignment;
            if (OffsetInElmts < 0) BottomAlignment = JAlignment;

            Type *VType = getVecTypeForPair(aType);
            unsigned VecAlignment = TD->getPrefTypeAlignment(VType);
            if (BottomAlignment < VecAlignment) {
                return false;
            }
            }
      } else {
        return false;
      }
    } else if (isa<ShuffleVectorInst>(I)) {
      // Only merge two shuffles if they're both constant
      return isa<Constant>(I->getOperand(2)) &&
             isa<Constant>(J->getOperand(2));
      // FIXME: We may want to vectorize non-constant shuffles also.
    }
    return true;
  }

  // Figure out whether or not J uses I and update the users and write-set
  // structures associated with I. Specifically, Users represents the set of
  // instructions that depend on I. WriteSet represents the set
  // of memory locations that are dependent on I. If UpdateUsers is true,
  // and J uses I, then Users is updated to contain J and WriteSet is updated
  // to contain any memory locations to which J writes. The function returns
  // true if J uses I. By default, alias analysis is used to determine
  // whether J reads from memory that overlaps with a location in WriteSet.
  // If LoadMoveSet is not null, then it is a previously-computed multimap
  // where the key is the memory-based user instruction and the value is
  // the instruction to be compared with I. So, if LoadMoveSet is provided,
  // then the alias analysis is not used. This is necessary because this
  // function is called during the process of moving instructions during
  // vectorization and the results of the alias analysis are not stable during
  // that process.
  bool WIVectorize::trackUsesOfI(DenseSet<Value *> &Users,
                       AliasSetTracker &WriteSet, Instruction *I,
                       Instruction *J, bool UpdateUsers,
                       std::multimap<Value *, Value *> *LoadMoveSet) {
    bool UsesI = false;

    // This instruction may already be marked as a user due, for example, to
    // being a member of a selected pair.
    if (Users.count(J))
      UsesI = true;

    if (!UsesI)
      for (User::op_iterator JU = J->op_begin(), JE = J->op_end();
           JU != JE; ++JU) {
        Value *V = *JU;
        if (I == V || Users.count(V)) {
          UsesI = true;
          break;
        }
      }
    if (!UsesI && J->mayReadFromMemory()) {
      if (LoadMoveSet) {
        VPIteratorPair JPairRange = LoadMoveSet->equal_range(J);
        UsesI = isSecondInIteratorPair<Value*>(I, JPairRange);
      } else {
        for (AliasSetTracker::iterator W = WriteSet.begin(),
             WE = WriteSet.end(); W != WE; ++W) {
          if (W->aliasesUnknownInst(J, *AA)) {
            UsesI = true;
            break;
          }
        }
      }
    }

    if (UsesI && UpdateUsers) {
      if (J->mayWriteToMemory()) WriteSet.add(J);
      Users.insert(J);
    }

    return UsesI;
  }
  
  bool WIVectorize::getCandidateVectors(BasicBlock &BB,
                       BasicBlock::iterator &Start,
                       ValueVectorMap &CandidateVectors,
                       std::vector<Value *> &PairableInsts) {
    BasicBlock::iterator E = BB.end();
    if (Start == E) return false;

    bool ShouldContinue = false, IAfterStart = false;
    
    for (BasicBlock::iterator I = Start++; I != E; ++I) {
      if (I == Start) IAfterStart = true;

      bool IsSimpleLoadStore;
      if (!isInstVectorizable(I, IsSimpleLoadStore)) {
          continue;
      }
      ValueVector* foundSoFar = new ValueVector;  
      // Look for an instruction with which to pair instruction *I...
      bool JAfterStart = IAfterStart;
      BasicBlock::iterator J = llvm::next(I);
      for (unsigned ss = 0; J != E ; ++J, ++ss) {
        if (J == Start) JAfterStart = true;

        // J does not use I, and comes before the first use of I, so it can be
        // merged with I if the instructions are compatible.
        if (!areInstsCompatibleFromDifferentWi(I,J)) continue;        
        //if (!areInstsCompatible(I, J, IsSimpleLoadStore)) continue;
        
        // J is a candidate for merging with I.
        if (!PairableInsts.size() ||
             PairableInsts[PairableInsts.size()-1] != I) {
          PairableInsts.push_back(I);
        }

        //CandidatePairs.insert(ValuePair(I, J));
        Value* v = cast<Value>(J);
        foundSoFar->push_back(v);

        // The next call to this function must start after the last instruction
        // selected during this invocation.
        if (JAfterStart) {
          Start = llvm::next(J);
          IAfterStart = JAfterStart = false;
        }
      }
      Value* v = cast<Value>(I);
      if (foundSoFar->size() == VectorWidth -1)
        CandidateVectors[v] = foundSoFar;      
    }

    DEBUG(dbgs() << "WIV: found " << PairableInsts.size()
           << " instructions with candidate pairs\n");

    return true;
  }
  // This function iterates over all instruction pairs in the provided
  // basic block and collects all candidate pairs for vectorization.
  bool WIVectorize::getCandidatePairs(BasicBlock &BB,
                       BasicBlock::iterator &Start,
                       std::multimap<Value *, Value *> &CandidatePairs,
                       std::vector<Value *> &PairableInsts) {
    BasicBlock::iterator E = BB.end();
    if (Start == E) return false;

    bool ShouldContinue = false, IAfterStart = false;
    for (BasicBlock::iterator I = Start++; I != E; ++I) {
      if (I == Start) IAfterStart = true;

      bool IsSimpleLoadStore;
      if (!isInstVectorizable(I, IsSimpleLoadStore)) continue;

      // Look for an instruction with which to pair instruction *I...
      DenseSet<Value *> Users;
      AliasSetTracker WriteSet(*AA);
      bool JAfterStart = IAfterStart;
      BasicBlock::iterator J = llvm::next(I);
      for (unsigned ss = 0; J != E ; ++J, ++ss) {
        if (J == Start) JAfterStart = true;

        // Determine if J uses I, if so, exit the loop.
        bool UsesI = trackUsesOfI(Users, WriteSet, I, J, !FastDep);
        if (FastDep) {
          // Note: For this heuristic to be effective, independent operations
          // must tend to be intermixed. This is likely to be true from some
          // kinds of grouped loop unrolling (but not the generic LLVM pass),
          // but otherwise may require some kind of reordering pass.

          // When using fast dependency analysis,
          // stop searching after first use:
          if (UsesI) break;
        } else {
          if (UsesI) continue;
        }

        // J does not use I, and comes before the first use of I, so it can be
        // merged with I if the instructions are compatible.
	if (!areInstsCompatibleFromDifferentWi(I,J)) continue;        
        if (!areInstsCompatible(I, J, IsSimpleLoadStore)) continue;
        
        // J is a candidate for merging with I.
        if (!PairableInsts.size() ||
             PairableInsts[PairableInsts.size()-1] != I) {
          PairableInsts.push_back(I);
        }

        CandidatePairs.insert(ValuePair(I, J));

        // The next call to this function must start after the last instruction
        // selected during this invocation.
        if (JAfterStart) {
          Start = llvm::next(J);
          IAfterStart = JAfterStart = false;
        }

        DEBUG(if (DebugCandidateSelection) dbgs() << "WIV: candidate pair "
                     << *I << " <-> " << *J << "\n");

      }

      if (ShouldContinue)
        break;
    }

    DEBUG(dbgs() << "WIV: found " << PairableInsts.size()
           << " instructions with candidate pairs\n");

    return ShouldContinue;
  }

  // Finds candidate pairs connected to the pair P = <PI, PJ>. This means that
  // it looks for pairs such that both members have an input which is an
  // output of PI or PJ.
  void WIVectorize::computePairsConnectedTo(
                      std::multimap<Value *, Value *> &CandidatePairs,
                      std::vector<Value *> &PairableInsts,
                      std::multimap<ValuePair, ValuePair> &ConnectedPairs,
                      ValuePair P) {
    // For each possible pairing for this variable, look at the uses of
    // the first value...
    for (Value::use_iterator I = P.first->use_begin(),
         E = P.first->use_end(); I != E; ++I) {
      VPIteratorPair IPairRange = CandidatePairs.equal_range(*I);

      // For each use of the first variable, look for uses of the second
      // variable...
      for (Value::use_iterator J = P.second->use_begin(),
           E2 = P.second->use_end(); J != E2; ++J) {
        VPIteratorPair JPairRange = CandidatePairs.equal_range(*J);

        // Look for <I, J>:
        if (isSecondInIteratorPair<Value*>(*J, IPairRange))
          ConnectedPairs.insert(VPPair(P, ValuePair(*I, *J)));

        // Look for <J, I>:
        if (isSecondInIteratorPair<Value*>(*I, JPairRange))
          ConnectedPairs.insert(VPPair(P, ValuePair(*J, *I)));
      }
      // Look for cases where just the first value in the pair is used by
      // both members of another pair (splatting).
      for (Value::use_iterator J = P.first->use_begin(); J != E; ++J) {
        if (isSecondInIteratorPair<Value*>(*J, IPairRange))
          ConnectedPairs.insert(VPPair(P, ValuePair(*I, *J)));
      }
    }
    // Look for cases where just the second value in the pair is used by
    // both members of another pair (splatting).
    for (Value::use_iterator I = P.second->use_begin(),
         E = P.second->use_end(); I != E; ++I) {
      VPIteratorPair IPairRange = CandidatePairs.equal_range(*I);

      for (Value::use_iterator J = P.second->use_begin(); J != E; ++J) {
        if (isSecondInIteratorPair<Value*>(*J, IPairRange))
          ConnectedPairs.insert(VPPair(P, ValuePair(*I, *J)));
      }
    }
  }

  // This function figures out which pairs are connected.  Two pairs are
  // connected if some output of the first pair forms an input to both members
  // of the second pair.
  void WIVectorize::computeConnectedPairs(
                      std::multimap<Value *, Value *> &CandidatePairs,
                      std::vector<Value *> &PairableInsts,
                      std::multimap<ValuePair, ValuePair> &ConnectedPairs) {

    for (std::vector<Value *>::iterator PI = PairableInsts.begin(),
         PE = PairableInsts.end(); PI != PE; ++PI) {
      VPIteratorPair choiceRange = CandidatePairs.equal_range(*PI);

      for (std::multimap<Value *, Value *>::iterator P = choiceRange.first;
           P != choiceRange.second; ++P)
        computePairsConnectedTo(CandidatePairs, PairableInsts,
                                ConnectedPairs, *P);
    }

    DEBUG(dbgs() << "WIV: found " << ConnectedPairs.size()
                 << " pair connections.\n");
  }

  // This function builds a set of use tuples such that <A, B> is in the set
  // if B is in the use tree of A. If B is in the use tree of A, then B
  // depends on the output of A.
  void WIVectorize::buildDepMap(
                      BasicBlock &BB,
                      std::multimap<Value *, Value *> &CandidatePairs,
                      std::vector<Value *> &PairableInsts,
                      DenseSet<ValuePair> &PairableInstUsers) {
    DenseSet<Value *> IsInPair;
    for (std::multimap<Value *, Value *>::iterator C = CandidatePairs.begin(),
         E = CandidatePairs.end(); C != E; ++C) {
      IsInPair.insert(C->first);
      IsInPair.insert(C->second);
    }

    // Iterate through the basic block, recording all Users of each
    // pairable instruction.

    BasicBlock::iterator E = BB.end();
    for (BasicBlock::iterator I = BB.getFirstInsertionPt(); I != E; ++I) {
      if (IsInPair.find(I) == IsInPair.end()) continue;

      DenseSet<Value *> Users;
      AliasSetTracker WriteSet(*AA);
      for (BasicBlock::iterator J = llvm::next(I); J != E; ++J)
        (void) trackUsesOfI(Users, WriteSet, I, J);

      for (DenseSet<Value *>::iterator U = Users.begin(), E = Users.end();
           U != E; ++U)
        PairableInstUsers.insert(ValuePair(I, *U));
    }
  }

  // Returns true if an input to pair P is an output of pair Q and also an
  // input of pair Q is an output of pair P. If this is the case, then these
  // two pairs cannot be simultaneously fused.
  bool WIVectorize::pairsConflict(ValuePair P, ValuePair Q,
                     DenseSet<ValuePair> &PairableInstUsers,
                     std::multimap<ValuePair, ValuePair> *PairableInstUserMap) {
    // Two pairs are in conflict if they are mutual Users of eachother.
    bool QUsesP = PairableInstUsers.count(ValuePair(P.first,  Q.first))  ||
                  PairableInstUsers.count(ValuePair(P.first,  Q.second)) ||
                  PairableInstUsers.count(ValuePair(P.second, Q.first))  ||
                  PairableInstUsers.count(ValuePair(P.second, Q.second));
    bool PUsesQ = PairableInstUsers.count(ValuePair(Q.first,  P.first))  ||
                  PairableInstUsers.count(ValuePair(Q.first,  P.second)) ||
                  PairableInstUsers.count(ValuePair(Q.second, P.first))  ||
                  PairableInstUsers.count(ValuePair(Q.second, P.second));
    if (PairableInstUserMap) {
      // FIXME: The expensive part of the cycle check is not so much the cycle
      // check itself but this edge insertion procedure. This needs some
      // profiling and probably a different data structure (same is true of
      // most uses of std::multimap).
      if (PUsesQ) {
        VPPIteratorPair QPairRange = PairableInstUserMap->equal_range(Q);
        if (!isSecondInIteratorPair(P, QPairRange))
          PairableInstUserMap->insert(VPPair(Q, P));
      }
      if (QUsesP) {
        VPPIteratorPair PPairRange = PairableInstUserMap->equal_range(P);
        if (!isSecondInIteratorPair(Q, PPairRange))
          PairableInstUserMap->insert(VPPair(P, Q));
      }
    }

    return (QUsesP && PUsesQ);
  }

  // This function walks the use graph of current pairs to see if, starting
  // from P, the walk returns to P.
  bool WIVectorize::pairWillFormCycle(ValuePair P,
                       std::multimap<ValuePair, ValuePair> &PairableInstUserMap,
                       DenseSet<ValuePair> &CurrentPairs) {
    DEBUG(if (DebugCycleCheck)
            dbgs() << "WIV: starting cycle check for : " << *P.first << " <-> "
                   << *P.second << "\n");
    // A lookup table of visisted pairs is kept because the PairableInstUserMap
    // contains non-direct associations.
    DenseSet<ValuePair> Visited;
    SmallVector<ValuePair, 32> Q;
    // General depth-first post-order traversal:
    Q.push_back(P);
    do {
      ValuePair QTop = Q.pop_back_val();
      Visited.insert(QTop);

      DEBUG(if (DebugCycleCheck)
              dbgs() << "WIV: cycle check visiting: " << *QTop.first << " <-> "
                     << *QTop.second << "\n");
      VPPIteratorPair QPairRange = PairableInstUserMap.equal_range(QTop);
      for (std::multimap<ValuePair, ValuePair>::iterator C = QPairRange.first;
           C != QPairRange.second; ++C) {
        if (C->second == P) {
          DEBUG(dbgs()
                 << "WIV: rejected to prevent non-trivial cycle formation: "
                 << *C->first.first << " <-> " << *C->first.second << "\n");
          return true;
        }

        if (CurrentPairs.count(C->second) && !Visited.count(C->second))
          Q.push_back(C->second);
      }
    } while (!Q.empty());

    return false;
  }

  // This function builds the initial tree of connected pairs with the
  // pair J at the root.
  void WIVectorize::buildInitialTreeFor(
                      std::multimap<Value *, Value *> &CandidatePairs,
                      std::vector<Value *> &PairableInsts,
                      std::multimap<ValuePair, ValuePair> &ConnectedPairs,
                      DenseSet<ValuePair> &PairableInstUsers,
                      DenseMap<Value *, Value *> &ChosenPairs,
                      DenseMap<ValuePair, size_t> &Tree, ValuePair J) {
    // Each of these pairs is viewed as the root node of a Tree. The Tree
    // is then walked (depth-first). As this happens, we keep track of
    // the pairs that compose the Tree and the maximum depth of the Tree.
    SmallVector<ValuePairWithDepth, 32> Q;
    // General depth-first post-order traversal:
    Q.push_back(ValuePairWithDepth(J, getDepthFactor(J.first)));
    do {
      ValuePairWithDepth QTop = Q.back();

      // Push each child onto the queue:
      bool MoreChildren = false;
      size_t MaxChildDepth = QTop.second;
      VPPIteratorPair qtRange = ConnectedPairs.equal_range(QTop.first);
      for (std::multimap<ValuePair, ValuePair>::iterator k = qtRange.first;
           k != qtRange.second; ++k) {
        // Make sure that this child pair is still a candidate:
        bool IsStillCand = false;
        VPIteratorPair checkRange =
          CandidatePairs.equal_range(k->second.first);
        for (std::multimap<Value *, Value *>::iterator m = checkRange.first;
             m != checkRange.second; ++m) {
          if (m->second == k->second.second) {
            IsStillCand = true;
            break;
          }
        }

        if (IsStillCand) {
          DenseMap<ValuePair, size_t>::iterator C = Tree.find(k->second);
          if (C == Tree.end()) {
            size_t d = getDepthFactor(k->second.first);
            Q.push_back(ValuePairWithDepth(k->second, QTop.second+d));
            MoreChildren = true;
          } else {
            MaxChildDepth = std::max(MaxChildDepth, C->second);
          }
        }
      }

      if (!MoreChildren) {
        // Record the current pair as part of the Tree:
        Tree.insert(ValuePairWithDepth(QTop.first, MaxChildDepth));
        Q.pop_back();
      }
    } while (!Q.empty());
  }

  // Given some initial tree, prune it by removing conflicting pairs (pairs
  // that cannot be simultaneously chosen for vectorization).
  void WIVectorize::pruneTreeFor(
                      std::multimap<Value *, Value *> &CandidatePairs,
                      std::vector<Value *> &PairableInsts,
                      std::multimap<ValuePair, ValuePair> &ConnectedPairs,
                      DenseSet<ValuePair> &PairableInstUsers,
                      std::multimap<ValuePair, ValuePair> &PairableInstUserMap,
                      DenseMap<Value *, Value *> &ChosenPairs,
                      DenseMap<ValuePair, size_t> &Tree,
                      DenseSet<ValuePair> &PrunedTree, ValuePair J,
                      bool UseCycleCheck) {
    SmallVector<ValuePairWithDepth, 32> Q;
    // General depth-first post-order traversal:
    Q.push_back(ValuePairWithDepth(J, getDepthFactor(J.first)));
    do {
      ValuePairWithDepth QTop = Q.pop_back_val();
      PrunedTree.insert(QTop.first);

      // Visit each child, pruning as necessary...
      DenseMap<ValuePair, size_t> BestChildren;
      VPPIteratorPair QTopRange = ConnectedPairs.equal_range(QTop.first);
      for (std::multimap<ValuePair, ValuePair>::iterator K = QTopRange.first;
           K != QTopRange.second; ++K) {
        DenseMap<ValuePair, size_t>::iterator C = Tree.find(K->second);
        if (C == Tree.end()) continue;

        // This child is in the Tree, now we need to make sure it is the
        // best of any conflicting children. There could be multiple
        // conflicting children, so first, determine if we're keeping
        // this child, then delete conflicting children as necessary.

        // It is also necessary to guard against pairing-induced
        // dependencies. Consider instructions a .. x .. y .. b
        // such that (a,b) are to be fused and (x,y) are to be fused
        // but a is an input to x and b is an output from y. This
        // means that y cannot be moved after b but x must be moved
        // after b for (a,b) to be fused. In other words, after
        // fusing (a,b) we have y .. a/b .. x where y is an input
        // to a/b and x is an output to a/b: x and y can no longer
        // be legally fused. To prevent this condition, we must
        // make sure that a child pair added to the Tree is not
        // both an input and output of an already-selected pair.

        // Pairing-induced dependencies can also form from more complicated
        // cycles. The pair vs. pair conflicts are easy to check, and so
        // that is done explicitly for "fast rejection", and because for
        // child vs. child conflicts, we may prefer to keep the current
        // pair in preference to the already-selected child.
        DenseSet<ValuePair> CurrentPairs;

        bool CanAdd = true;
        for (DenseMap<ValuePair, size_t>::iterator C2
              = BestChildren.begin(), E2 = BestChildren.end();
             C2 != E2; ++C2) {
          if (C2->first.first == C->first.first ||
              C2->first.first == C->first.second ||
              C2->first.second == C->first.first ||
              C2->first.second == C->first.second ||
              pairsConflict(C2->first, C->first, PairableInstUsers,
                            UseCycleCheck ? &PairableInstUserMap : 0)) {
            if (C2->second >= C->second) {
              CanAdd = false;
              break;
            }

            CurrentPairs.insert(C2->first);
          }
        }
        if (!CanAdd) continue;

        // Even worse, this child could conflict with another node already
        // selected for the Tree. If that is the case, ignore this child.
        for (DenseSet<ValuePair>::iterator T = PrunedTree.begin(),
             E2 = PrunedTree.end(); T != E2; ++T) {
          if (T->first == C->first.first ||
              T->first == C->first.second ||
              T->second == C->first.first ||
              T->second == C->first.second ||
              pairsConflict(*T, C->first, PairableInstUsers,
                            UseCycleCheck ? &PairableInstUserMap : 0)) {
            CanAdd = false;
            break;
          }

          CurrentPairs.insert(*T);
        }
        if (!CanAdd) continue;

        // And check the queue too...
        for (SmallVector<ValuePairWithDepth, 32>::iterator C2 = Q.begin(),
             E2 = Q.end(); C2 != E2; ++C2) {
          if (C2->first.first == C->first.first ||
              C2->first.first == C->first.second ||
              C2->first.second == C->first.first ||
              C2->first.second == C->first.second ||
              pairsConflict(C2->first, C->first, PairableInstUsers,
                            UseCycleCheck ? &PairableInstUserMap : 0)) {
            CanAdd = false;
            break;
          }

          CurrentPairs.insert(C2->first);
        }
        if (!CanAdd) continue;

        // Last but not least, check for a conflict with any of the
        // already-chosen pairs.
        for (DenseMap<Value *, Value *>::iterator C2 =
              ChosenPairs.begin(), E2 = ChosenPairs.end();
             C2 != E2; ++C2) {
          if (pairsConflict(*C2, C->first, PairableInstUsers,
                            UseCycleCheck ? &PairableInstUserMap : 0)) {
            CanAdd = false;
            break;
          }

          CurrentPairs.insert(*C2);
        }
        if (!CanAdd) continue;

        // To check for non-trivial cycles formed by the addition of the
        // current pair we've formed a list of all relevant pairs, now use a
        // graph walk to check for a cycle. We start from the current pair and
        // walk the use tree to see if we again reach the current pair. If we
        // do, then the current pair is rejected.

        // FIXME: It may be more efficient to use a topological-ordering
        // algorithm to improve the cycle check. This should be investigated.
        if (UseCycleCheck &&
            pairWillFormCycle(C->first, PairableInstUserMap, CurrentPairs))
          continue;

        // This child can be added, but we may have chosen it in preference
        // to an already-selected child. Check for this here, and if a
        // conflict is found, then remove the previously-selected child
        // before adding this one in its place.
        for (DenseMap<ValuePair, size_t>::iterator C2
              = BestChildren.begin(); C2 != BestChildren.end();) {
          if (C2->first.first == C->first.first ||
              C2->first.first == C->first.second ||
              C2->first.second == C->first.first ||
              C2->first.second == C->first.second ||
              pairsConflict(C2->first, C->first, PairableInstUsers))
            BestChildren.erase(C2++);
          else
            ++C2;
        }

        BestChildren.insert(ValuePairWithDepth(C->first, C->second));
      }

      for (DenseMap<ValuePair, size_t>::iterator C
            = BestChildren.begin(), E2 = BestChildren.end();
           C != E2; ++C) {
        size_t DepthF = getDepthFactor(C->first.first);
        Q.push_back(ValuePairWithDepth(C->first, QTop.second+DepthF));
      }
    } while (!Q.empty());
  }

  // This function finds the best tree of mututally-compatible connected
  // pairs, given the choice of root pairs as an iterator range.
  void WIVectorize::findBestTreeFor(
                      std::multimap<Value *, Value *> &CandidatePairs,
                      std::vector<Value *> &PairableInsts,
                      std::multimap<ValuePair, ValuePair> &ConnectedPairs,
                      DenseSet<ValuePair> &PairableInstUsers,
                      std::multimap<ValuePair, ValuePair> &PairableInstUserMap,
                      DenseMap<Value *, Value *> &ChosenPairs,
                      DenseSet<ValuePair> &BestTree, size_t &BestMaxDepth,
                      size_t &BestEffSize, VPIteratorPair ChoiceRange,
                      bool UseCycleCheck) {
    for (std::multimap<Value *, Value *>::iterator J = ChoiceRange.first;
         J != ChoiceRange.second; ++J) {

      // Before going any further, make sure that this pair does not
      // conflict with any already-selected pairs (see comment below
      // near the Tree pruning for more details).
      DenseSet<ValuePair> ChosenPairSet;
      bool DoesConflict = false;
      for (DenseMap<Value *, Value *>::iterator C = ChosenPairs.begin(),
           E = ChosenPairs.end(); C != E; ++C) {
        if (pairsConflict(*C, *J, PairableInstUsers,
                          UseCycleCheck ? &PairableInstUserMap : 0)) {
          DoesConflict = true;
          break;
        }

        ChosenPairSet.insert(*C);
      }
      if (DoesConflict) continue;

      if (UseCycleCheck &&
          pairWillFormCycle(*J, PairableInstUserMap, ChosenPairSet))
        continue;

      DenseMap<ValuePair, size_t> Tree;
      buildInitialTreeFor(CandidatePairs, PairableInsts, ConnectedPairs,
                          PairableInstUsers, ChosenPairs, Tree, *J);

      // Because we'll keep the child with the largest depth, the largest
      // depth is still the same in the unpruned Tree.
      size_t MaxDepth = Tree.lookup(*J);

      DEBUG(if (DebugPairSelection) dbgs() << "WIV: found Tree for pair {"
                   << *J->first << " <-> " << *J->second << "} of depth " <<
                   MaxDepth << " and size " << Tree.size() << "\n");

      // At this point the Tree has been constructed, but, may contain
      // contradictory children (meaning that different children of
      // some tree node may be attempting to fuse the same instruction).
      // So now we walk the tree again, in the case of a conflict,
      // keep only the child with the largest depth. To break a tie,
      // favor the first child.

      DenseSet<ValuePair> PrunedTree;
      pruneTreeFor(CandidatePairs, PairableInsts, ConnectedPairs,
                   PairableInstUsers, PairableInstUserMap, ChosenPairs, Tree,
                   PrunedTree, *J, UseCycleCheck);

      size_t EffSize = 0;
      for (DenseSet<ValuePair>::iterator S = PrunedTree.begin(),
           E = PrunedTree.end(); S != E; ++S)
        EffSize += getDepthFactor(S->first);

      DEBUG(if (DebugPairSelection)
             dbgs() << "WIV: found pruned Tree for pair {"
             << *J->first << " <-> " << *J->second << "} of depth " <<
             MaxDepth << " and size " << PrunedTree.size() <<
            " (effective size: " << EffSize << ")\n");
      if (MaxDepth >= ReqChainDepth && EffSize > BestEffSize) {
        BestMaxDepth = MaxDepth;
        BestEffSize = EffSize;
        BestTree = PrunedTree;
      }
    }
  }

  // Given the list of candidate pairs, this function selects those
  // that will be fused into vector instructions.
  void WIVectorize::choosePairs(
                      std::multimap<Value *, Value *> &CandidatePairs,
                      std::vector<Value *> &PairableInsts,
                      std::multimap<ValuePair, ValuePair> &ConnectedPairs,
                      DenseSet<ValuePair> &PairableInstUsers,
                      DenseMap<Value *, Value *>& ChosenPairs) {
    bool UseCycleCheck = true;
    std::multimap<ValuePair, ValuePair> PairableInstUserMap;
    for (std::vector<Value *>::iterator I = PairableInsts.begin(),
         E = PairableInsts.end(); I != E; ++I) {
      // The number of possible pairings for this variable:
      size_t NumChoices = CandidatePairs.count(*I);
      if (!NumChoices) continue;

      VPIteratorPair ChoiceRange = CandidatePairs.equal_range(*I);

      // The best pair to choose and its tree:
      size_t BestMaxDepth = 0, BestEffSize = 0;
      DenseSet<ValuePair> BestTree;
      findBestTreeFor(CandidatePairs, PairableInsts, ConnectedPairs,
                      PairableInstUsers, PairableInstUserMap, ChosenPairs,
                      BestTree, BestMaxDepth, BestEffSize, ChoiceRange,
                      UseCycleCheck);

      // A tree has been chosen (or not) at this point. If no tree was
      // chosen, then this instruction, I, cannot be paired (and is no longer
      // considered).

      DEBUG(if (BestTree.size() > 0)
              dbgs() << "WIV: selected pairs in the best tree for: "
                     << *cast<Instruction>(*I) << "\n");

      for (DenseSet<ValuePair>::iterator S = BestTree.begin(),
           SE2 = BestTree.end(); S != SE2; ++S) {
        // Insert the members of this tree into the list of chosen pairs.
        ChosenPairs.insert(ValuePair(S->first, S->second));
        DEBUG(dbgs() << "WIV: selected pair: " << *S->first << " <-> " <<
               *S->second << "\n");

        // Remove all candidate pairs that have values in the chosen tree.
        for (std::multimap<Value *, Value *>::iterator K =
               CandidatePairs.begin(); K != CandidatePairs.end();) {
          if (K->first == S->first || K->second == S->first ||
              K->second == S->second || K->first == S->second) {
            // Don't remove the actual pair chosen so that it can be used
            // in subsequent tree selections.
            if (!(K->first == S->first && K->second == S->second))
              CandidatePairs.erase(K++);
            else
              ++K;
          } else {
            ++K;
          }
        }
      }
    }

    DEBUG(dbgs() << "WIV: selected " << ChosenPairs.size() << " pairs.\n");
  }

  // Returns the value that is to be used as the pointer input to the vector
  // instruction that fuses I with J.
  Value *WIVectorize::getReplacementPointerInput(LLVMContext& Context,
                     Instruction *I, ValueVector *vec, unsigned o) {
    Value *IPtr, *JPtr;
    unsigned IAlignment, JAlignment;
    int64_t OffsetInElmts;
    (void) getPairPtrInfo(I, I, IPtr, JPtr, IAlignment, JAlignment,
                          OffsetInElmts);

    // The pointer value is taken to be the one with the lowest offset.
    Value *VPtr;
    if (OffsetInElmts > 0) {
      VPtr = IPtr;
    } else {
      VPtr = JPtr;
    }

    Type *ArgType = cast<PointerType>(IPtr->getType())->getElementType();
    Type *VArgType = getVecTypeForVector(ArgType);
    Type *VArgPtrType = PointerType::get(VArgType,
      cast<PointerType>(IPtr->getType())->getAddressSpace());
    BitCastInst* b =  new BitCastInst(IPtr, VArgPtrType, getReplacementName(I, true, o),
                        I);
    return b;
  }
  // Returns the value that is to be used as the pointer input to the vector
  // instruction that fuses I with J.
  Value *WIVectorize::getReplacementPointerInput(LLVMContext& Context,
                     Instruction *I, Instruction *J, unsigned o,
                     bool &FlipMemInputs) {
    Value *IPtr, *JPtr;
    unsigned IAlignment, JAlignment;
    int64_t OffsetInElmts;
    (void) getPairPtrInfo(I, J, IPtr, JPtr, IAlignment, JAlignment,
                          OffsetInElmts);

    // The pointer value is taken to be the one with the lowest offset.
    Value *VPtr;
    if (OffsetInElmts > 0) {
      VPtr = IPtr;
    } else {
      FlipMemInputs = true;
      VPtr = JPtr;
    }

    Type *ArgType = cast<PointerType>(IPtr->getType())->getElementType();
    Type *VArgType = getVecTypeForPair(ArgType);
    Type *VArgPtrType = PointerType::get(VArgType,
      cast<PointerType>(IPtr->getType())->getAddressSpace());
    BitCastInst* b =  new BitCastInst(VPtr, VArgPtrType, getReplacementName(I, true, o),
                        /* insert before */ FlipMemInputs ? J : I);
    if (I->getMetadata("wi") != NULL) {
      b->setMetadata("wi", I->getMetadata("wi"));
    }
    return b;
  }

  void WIVectorize::fillNewShuffleMask(LLVMContext& Context, Instruction *J,
                     unsigned NumElem, unsigned MaskOffset, unsigned NumInElem,
                     unsigned IdxOffset, std::vector<Constant*> &Mask) {
    for (unsigned v = 0; v < NumElem/2; ++v) {
      int m = cast<ShuffleVectorInst>(J)->getMaskValue(v);
      if (m < 0) {
        Mask[v+MaskOffset] = UndefValue::get(Type::getInt32Ty(Context));
      } else {
        unsigned mm = m + (int) IdxOffset;
        if (m >= (int) NumInElem)
          mm += (int) NumInElem;

        Mask[v+MaskOffset] =
          ConstantInt::get(Type::getInt32Ty(Context), mm);
      }
    }
  }

  // Returns the value that is to be used as the vector-shuffle mask to the
  // vector instruction that fuses I with J.
  Value *WIVectorize::getReplacementShuffleMask(LLVMContext& Context,
                     Instruction *I, Instruction *J) {
    // This is the shuffle mask. We need to append the second
    // mask to the first, and the numbers need to be adjusted.

    Type *ArgType = I->getType();
    Type *VArgType = getVecTypeForPair(ArgType);

    // Get the total number of elements in the fused vector type.
    // By definition, this must equal the number of elements in
    // the final mask.
    unsigned NumElem = cast<VectorType>(VArgType)->getNumElements();
    std::vector<Constant*> Mask(NumElem);

    Type *OpType = I->getOperand(0)->getType();
    unsigned NumInElem = cast<VectorType>(OpType)->getNumElements();

    // For the mask from the first pair...
    fillNewShuffleMask(Context, I, NumElem, 0, NumInElem, 0, Mask);

    // For the mask from the second pair...
    fillNewShuffleMask(Context, J, NumElem, NumElem/2, NumInElem, NumInElem,
                       Mask);

    return ConstantVector::get(Mask);
  }

  // Returns the value to be used as the specified operand of the vector
  // instruction that fuses I with J.
  Value *WIVectorize::getReplacementInput(LLVMContext& Context, Instruction *I,
                     ValueVector *vec, unsigned o) {
      // Compute the fused vector type for this operand
    Type *ArgType = I->getOperand(o)->getType();
    VectorType *VArgType = getVecTypeForVector(ArgType);
    Instruction *L = I;

    
    // If these two inputs are the output of another vector instruction,
    // then we should use that output directly. It might be necessary to
    // permute it first. [When pairings are fused recursively, you can
    // end up with cases where a large vector is decomposed into scalars
    // using extractelement instructions, then built into size-2
    // vectors using insertelement and the into larger vectors using
    // shuffles. InstCombine does not simplify all of these cases well,
    // and so we make sure that shuffles are generated here when possible.
    /*ExtractElementInst *Orig =
        dyn_cast<ExtractElementInst>(L);
    if (Orig)
        return Orig->getOperand(1);*/
    
    ExtractElementInst *LEE
      = dyn_cast<ExtractElementInst>(L->getOperand(o));
    
    if (LEE) {
      VectorType *EEType = cast<VectorType>(LEE->getOperand(0)->getType());
      unsigned LowIndx = cast<ConstantInt>(LEE->getOperand(1))->getZExtValue();
      return LEE->getOperand(0);
    }
    
    Value *newIndx = ConstantInt::get(Type::getInt32Ty(Context), 0);        
    Instruction *BV1 = InsertElementInst::Create(
                                          UndefValue::get(VArgType),
                                          L->getOperand(o), newIndx,
                                          getReplacementName(I, true, o, 0));
    BV1->insertBefore(I);
    Instruction *BV2 = NULL;
    for (unsigned i = 0; i < vec->size(); i++) {
        Value *newIndx = ConstantInt::get(Type::getInt32Ty(Context), i+1);        
        Value *v = (*vec)[i];        
        Instruction* J = cast<Instruction>(v);
        BV2 = InsertElementInst::Create(BV1, J->getOperand(o),
                                            newIndx, 
                                            getReplacementName(I, true, o, i+1));
       /* if (J->getMetadata("wi") != NULL) {
            BV2->setMetadata("wi",J->getMetadata("wi"));
        }*/
        BV2->insertBefore(I);
    }
    return BV2;
  }

  Value *WIVectorize::CommonShuffleSource(Instruction *I, Instruction *J, unsigned o) {
      DenseMap<Value*, Value*>::iterator vi = storedSources.find(I);
      DenseMap<Value*, Value*>::iterator vj = storedSources.find(J);
      if (vi != storedSources.end() 
          && vj != storedSources.end()) {
          if ((*vi).second == (*vj).second) {
            return (*vi).second;
          }
      }
      return NULL;
  }
  // Returns the value to be used as the specified operand of the vector
  // instruction that fuses I with J.
  Value *WIVectorize::getReplacementInput(LLVMContext& Context, Instruction *I,
                     Instruction *J, unsigned o, bool FlipMemInputs) {
    Value *CV0 = ConstantInt::get(Type::getInt32Ty(Context), 0);
    Value *CV1 = ConstantInt::get(Type::getInt32Ty(Context), 1);

      // Compute the fused vector type for this operand
    Type *ArgType = I->getOperand(o)->getType();
    VectorType *VArgType = getVecTypeForPair(ArgType);
    Instruction *L = I, *H = J;
    if (FlipMemInputs) {
      L = J;
      H = I;
    }

    if (ArgType->isVectorTy()) {      
      ShuffleVectorInst *LSV
	= dyn_cast<ShuffleVectorInst>(L->getOperand(o));
      ShuffleVectorInst *HSV
	= dyn_cast<ShuffleVectorInst>(H->getOperand(o));	
      if (LSV && HSV &&
	  LSV->getOperand(0)->getType() == HSV->getOperand(0)->getType() &&
	  LSV->getOperand(1)->getType() == HSV->getOperand(1)->getType() &&
	  LSV->getOperand(2)->getType() == HSV->getOperand(2)->getType()) {
	  if (LSV->getOperand(0) == HSV->getOperand(0) &&
	      LSV->getOperand(1) == HSV->getOperand(1)) {
              if (LSV->getOperand(0)->getType()->getVectorNumElements() ==
                  2 * LSV->getOperand(2)->getType()->getVectorNumElements()) {
                  return LSV->getOperand(0);
              }
	  }
          Value* res = CommonShuffleSource(LSV, HSV, o);
          if (res)
              return res;	  
      }
      InsertElementInst *LIN
	= dyn_cast<InsertElementInst>(L->getOperand(o));
      InsertElementInst *HIN
	= dyn_cast<InsertElementInst>(H->getOperand(o));
      
      unsigned numElem = cast<VectorType>(VArgType)->getNumElements();
      if (LIN && HIN) {
	  Instruction *newIn = InsertElementInst::Create(
                                          UndefValue::get(VArgType),
                                          LIN->getOperand(1), 
					  LIN->getOperand(2),
                                          getReplacementName(I, true, o, 1));	  
	  if (I->getMetadata("wi"))
	    newIn->setMetadata("wi", I->getMetadata("wi"));
	  newIn->insertBefore(J);
	  
	  LIN = dyn_cast<InsertElementInst>(LIN->getOperand(0));
	  int counter = 2;
	  int rounds = 0;
	  while (rounds < 2) {
	    while(LIN) {      
	      unsigned Indx = cast<ConstantInt>(LIN->getOperand(2))->getZExtValue();
	      Indx += rounds * (numElem/2);
	      Value *newIndx = ConstantInt::get(Type::getInt32Ty(Context), Indx);	      
	      newIn = InsertElementInst::Create(
					newIn,
				        LIN->getOperand(1),
					newIndx,
					getReplacementName(I, true, o ,counter));
	      counter++;
	      if (I->getMetadata("wi"))
		newIn->setMetadata("wi", I->getMetadata("wi"));
	      newIn->insertBefore(J);	    
	      LIN = dyn_cast<InsertElementInst>(LIN->getOperand(0));	    
	    }
	    rounds ++;
	    LIN = HIN;
	  }	  
	  return newIn;
	      
      }
      std::vector<Constant*> Mask(numElem);      
      for (unsigned v = 0; v < numElem; ++v)
	  Mask[v] = ConstantInt::get(Type::getInt32Ty(Context), v);

      Instruction *BV = new ShuffleVectorInst(L->getOperand(o),
                                              H->getOperand(o),
                                              ConstantVector::get(Mask),
                                              getReplacementName(I, true, o));      
      if (L->getMetadata("wi") != NULL) {
	BV->setMetadata("wi", L->getMetadata("wi"));
      }
      BV->insertBefore(J);
      return BV;
    }

    // If these two inputs are the output of another vector instruction,
    // then we should use that output directly. It might be necessary to
    // permute it first. [When pairings are fused recursively, you can
    // end up with cases where a large vector is decomposed into scalars
    // using extractelement instructions, then built into size-2
    // vectors using insertelement and the into larger vectors using
    // shuffles. InstCombine does not simplify all of these cases well,
    // and so we make sure that shuffles are generated here when possible.
    ExtractElementInst *LEE
      = dyn_cast<ExtractElementInst>(L->getOperand(o));
    ExtractElementInst *HEE
      = dyn_cast<ExtractElementInst>(H->getOperand(o));

    if (LEE && HEE &&
        LEE->getOperand(0)->getType() == HEE->getOperand(0)->getType()) {
      VectorType *EEType = cast<VectorType>(LEE->getOperand(0)->getType());
      unsigned LowIndx = cast<ConstantInt>(LEE->getOperand(1))->getZExtValue();
      unsigned HighIndx = cast<ConstantInt>(HEE->getOperand(1))->getZExtValue();
      if (LEE->getOperand(0) == HEE->getOperand(0)) {
        if (LowIndx == 0 && HighIndx == 1)
          return LEE->getOperand(0);

        std::vector<Constant*> Mask(2);
        Mask[0] = ConstantInt::get(Type::getInt32Ty(Context), LowIndx);
        Mask[1] = ConstantInt::get(Type::getInt32Ty(Context), HighIndx);

        Instruction *BV = new ShuffleVectorInst(LEE->getOperand(0),
                                          UndefValue::get(EEType),
                                          ConstantVector::get(Mask),
                                          getReplacementName(I, true, o));
	if (I->getMetadata("wi") != NULL) {
	  BV->setMetadata("wi", I->getMetadata("wi"));
	}	
        BV->insertBefore(J);
        return BV;
      }

      std::vector<Constant*> Mask(2);
      HighIndx += EEType->getNumElements();
      Mask[0] = ConstantInt::get(Type::getInt32Ty(Context), LowIndx);
      Mask[1] = ConstantInt::get(Type::getInt32Ty(Context), HighIndx);

      Instruction *BV = new ShuffleVectorInst(LEE->getOperand(0),
                                          HEE->getOperand(0),
                                          ConstantVector::get(Mask),
                                          getReplacementName(I, true, o));
      if (I->getMetadata("wi") != NULL) {
	BV->setMetadata("wi", I->getMetadata("wi"));
      }      
      BV->insertBefore(J);
      return BV;
    }

    Instruction *BV1 = InsertElementInst::Create(
                                          UndefValue::get(VArgType),
                                          L->getOperand(o), CV0,
                                          getReplacementName(I, true, o, 1));
    if (I->getMetadata("wi") != NULL) {
      BV1->setMetadata("wi", I->getMetadata("wi"));
    }
    
    BV1->insertBefore(I);
    
    Instruction *BV2 = InsertElementInst::Create(BV1, H->getOperand(o),
                                          CV1,
                                          getReplacementName(I, true, o, 2));
    if (J->getMetadata("wi") != NULL) {
      BV2->setMetadata("wi",J->getMetadata("wi"));
    }
    BV2->insertBefore(J);
    return BV2;
  }

  // This function creates an array of values that will be used as the inputs
  // to the vector instruction that fuses I with J.
  void WIVectorize::getReplacementInputsForPair(LLVMContext& Context,
                     Instruction *I, Instruction *J,
                     SmallVector<Value *, 3> &ReplacedOperands,
                     bool &FlipMemInputs) {
    FlipMemInputs = false;
    unsigned NumOperands = I->getNumOperands();

    for (unsigned p = 0, o = NumOperands-1; p < NumOperands; ++p, --o) {
      // Iterate backward so that we look at the store pointer
      // first and know whether or not we need to flip the inputs.

      if (isa<LoadInst>(I) || (o == 1 && isa<StoreInst>(I))) {
        // This is the pointer for a load/store instruction.
        ReplacedOperands[o] = getReplacementPointerInput(Context, I, J, o,
                                FlipMemInputs);
        continue;
      } else if (isa<CallInst>(I) && o == NumOperands-1) {
        Function *F = cast<CallInst>(I)->getCalledFunction();
        unsigned IID = F->getIntrinsicID();
        BasicBlock &BB = *I->getParent();

        Module *M = BB.getParent()->getParent();
        Type *ArgType = I->getType();
        Type *VArgType = getVecTypeForPair(ArgType);

        // FIXME: is it safe to do this here?
        ReplacedOperands[o] = Intrinsic::getDeclaration(M,
          (Intrinsic::ID) IID, VArgType);
        continue;
      } else if (isa<ShuffleVectorInst>(I) && o == NumOperands-1) {
        ReplacedOperands[o] = getReplacementShuffleMask(Context, I, J);
        continue;
      }

      ReplacedOperands[o] =
        getReplacementInput(Context, I, J, o, FlipMemInputs);
    }
  }

    // This function creates an array of values that will be used as the inputs
  // to the vector instruction that fuses I with J.
  void WIVectorize::getReplacementInputsForVector(LLVMContext& Context,
                     Instruction *I, ValueVector *vec,
                     SmallVector<Value *, 3> &ReplacedOperands) {

    unsigned NumOperands = I->getNumOperands();

    for (unsigned p = 0, o = NumOperands-1; p < NumOperands; ++p, --o) {
      // Iterate backward so that we look at the store pointer
      // first and know whether or not we need to flip the inputs.

      if (isa<LoadInst>(I) || (o == 1 && isa<StoreInst>(I))) {
        // This is the pointer for a load/store instruction.
        ReplacedOperands[o] = getReplacementPointerInput(Context, I, vec, o);
        continue;
      } else if (isa<CallInst>(I) && o == NumOperands-1) {
        Function *F = cast<CallInst>(I)->getCalledFunction();
        unsigned IID = F->getIntrinsicID();
        BasicBlock &BB = *I->getParent();

        Module *M = BB.getParent()->getParent();
        Type *ArgType = I->getType();
        Type *VArgType = getVecTypeForPair(ArgType);

        // FIXME: is it safe to do this here?
        ReplacedOperands[o] = Intrinsic::getDeclaration(M,
          (Intrinsic::ID) IID, VArgType);
        continue;
      }/* else if (isa<ShuffleVectorInst>(I) && o == NumOperands-1) {
        ReplacedOperands[o] = getReplacementShuffleMask(Context, I, J);
        continue;
      }*/
      ReplacedOperands[o] =
        getReplacementInput(Context, I, vec, o);
    }
  }

  void WIVectorize::replaceOutputsOfVector(LLVMContext& Context, Instruction *I,
                     ValueVector* vec, Instruction *K,
                     Instruction *&InsertionPt,
                     ValueVector *newVec) {
    Value *CV0 = ConstantInt::get(Type::getInt32Ty(Context), 0);
    Value *CV1 = ConstantInt::get(Type::getInt32Ty(Context), 1);
    newVec->clear();
    if (isa<StoreInst>(I)) {
      AA->replaceWithNewValue(I, K);
      for (int i = 0; i < vec->size(); i++) {
          Value* v = (*vec)[i];
          Instruction* tmp = cast<Instruction>(v);
          AA->replaceWithNewValue(tmp, K);
      }
    } else {
      Type *IType = I->getType();
      Type *VType = getVecTypeForVector(IType);

        Instruction* K1 = ExtractElementInst::Create(K, CV0,
                                          getReplacementName(K, false, 1));
        /*if (I->getMetadata("wi") != NULL) 
            K1->setMetadata("wi", I->getMetadata("wi"));*/
        K1->insertAfter(K); 
        newVec->push_back(K1);
        Instruction* ins = K1;
        for (int i = 0; i < vec->size(); i++) {
            Value *X = ConstantInt::get(Type::getInt32Ty(Context), i+1);            
            Instruction* other = ExtractElementInst::Create(K, X,
                                        getReplacementName(K, false, i+1));
            other->insertAfter(ins);
            ins = other;
            InsertionPt = other;
            newVec->push_back(other);
        }
      
    }
  }

  // This function creates two values that represent the outputs of the
  // original I and J instructions. These are generally vector shuffles
  // or extracts. In many cases, these will end up being unused and, thus,
  // eliminated by later passes.
  void WIVectorize::replaceOutputsOfPair(LLVMContext& Context, Instruction *I,
                     Instruction *J, Instruction *K,
                     Instruction *&InsertionPt,
                     Instruction *&K1, Instruction *&K2,
                     bool &FlipMemInputs) {
    Value *CV0 = ConstantInt::get(Type::getInt32Ty(Context), 0);
    Value *CV1 = ConstantInt::get(Type::getInt32Ty(Context), 1);

    if (isa<StoreInst>(I)) {
      AA->replaceWithNewValue(I, K);
      AA->replaceWithNewValue(J, K);
    } else {
      Type *IType = I->getType();
      Type *VType = getVecTypeForPair(IType);

      if (IType->isVectorTy()) {
          unsigned numElem = cast<VectorType>(IType)->getNumElements();
          std::vector<Constant*> Mask1(numElem), Mask2(numElem);
          for (unsigned v = 0; v < numElem; ++v) {
            Mask1[v] = ConstantInt::get(Type::getInt32Ty(Context), v);
            Mask2[v] = ConstantInt::get(Type::getInt32Ty(Context), numElem+v);
          }

          K1 = new ShuffleVectorInst(K, UndefValue::get(VType),
                                       ConstantVector::get(
                                         FlipMemInputs ? Mask2 : Mask1),
                                       getReplacementName(K, false, 1));
          K2 = new ShuffleVectorInst(K, UndefValue::get(VType),
                                       ConstantVector::get(
                                         FlipMemInputs ? Mask1 : Mask2),
                                       getReplacementName(K, false, 2));
            storedSources.insert(ValuePair(K1,K));
            storedSources.insert(ValuePair(K2,K)); 
            flippedStoredSources.insert(ValuePair(K, K1));
            flippedStoredSources.insert(ValuePair(K, K2));
            VPIteratorPair v1 = 
                flippedStoredSources.equal_range(I);
            for (std::multimap<Value*, Value*>::iterator ii = v1.first;
                 ii != v1.second; ii++) {        
                storedSources.erase((*ii).second);            
                storedSources.insert(ValuePair((*ii).second,K));
                flippedStoredSources.insert(ValuePair(K, (*ii).second));
                storedSources.erase(I);
            }
            flippedStoredSources.erase(I);              
            VPIteratorPair v2 = flippedStoredSources.equal_range(J);
            for (std::multimap<Value*, Value*>::iterator ji = v2.first;
                 ji != v2.second; ji++) {        
                storedSources.erase((*ji).second);
                storedSources.insert(ValuePair((*ji).second,K));
                flippedStoredSources.insert(ValuePair(K, (*ji).second));            
                storedSources.erase(J);
            }
            flippedStoredSources.erase(J);                        
      } else {
        K1 = ExtractElementInst::Create(K, FlipMemInputs ? CV1 : CV0,
                                          getReplacementName(K, false, 1));
        K2 = ExtractElementInst::Create(K, FlipMemInputs ? CV0 : CV1,
                                          getReplacementName(K, false, 2));
        storedSources.insert(ValuePair(K1,K));
        storedSources.insert(ValuePair(K2,K));    
        flippedStoredSources.insert(ValuePair(K, K1));
        flippedStoredSources.insert(ValuePair(K, K2));
      }
      if (I->getMetadata("wi") != NULL) 
	K1->setMetadata("wi", I->getMetadata("wi"));
      if (J->getMetadata("wi") != NULL) 
	K2->setMetadata("wi", J->getMetadata("wi"));
      
      K1->insertAfter(K);
      K2->insertAfter(K1);
      InsertionPt = K2;
    }
  }

  // Move all uses of the function I (including pairing-induced uses) after J.
  bool WIVectorize::canMoveUsesOfIAfterJ(BasicBlock &BB,
                     std::multimap<Value *, Value *> &LoadMoveSet,
                     Instruction *I, Instruction *J) {
    // Skip to the first instruction past I.
    BasicBlock::iterator L = llvm::next(BasicBlock::iterator(I));

    DenseSet<Value *> Users;
    AliasSetTracker WriteSet(*AA);
    for (; cast<Instruction>(L) != J; ++L)
      (void) trackUsesOfI(Users, WriteSet, I, L, true, &LoadMoveSet);

    assert(cast<Instruction>(L) == J &&
      "Tracking has not proceeded far enough to check for dependencies");
    // If J is now in the use set of I, then trackUsesOfI will return true
    // and we have a dependency cycle (and the fusing operation must abort).
    return !trackUsesOfI(Users, WriteSet, I, J, true, &LoadMoveSet);
  }

  // Move all uses of the function I (including pairing-induced uses) after J.
  void WIVectorize::moveUsesOfIAfterJ(BasicBlock &BB,
                     std::multimap<Value *, Value *> &LoadMoveSet,
                     Instruction *&InsertionPt,
                     Instruction *I, Instruction *J) {
    // Skip to the first instruction past I.
    BasicBlock::iterator L = llvm::next(BasicBlock::iterator(I));

    DenseSet<Value *> Users;
    AliasSetTracker WriteSet(*AA);
    for (; cast<Instruction>(L) != J;) {
      if (trackUsesOfI(Users, WriteSet, I, L, true, &LoadMoveSet)) {
        // Move this instruction
        Instruction *InstToMove = L; ++L;

        /*DEBUG(dbgs() << "WIV: moving: " << *InstToMove <<
                        " to after " << *InsertionPt << "\n");*/
        InstToMove->removeFromParent();
        InstToMove->insertAfter(InsertionPt);
        InsertionPt = InstToMove;
      } else {
        ++L;
      }
    }
  }

  // Collect all load instruction that are in the move set of a given first
  // pair member.  These loads depend on the first instruction, I, and so need
  // to be moved after J (the second instruction) when the pair is fused.
  void WIVectorize::collectPairLoadMoveSet(BasicBlock &BB,
                     DenseMap<Value *, Value *> &ChosenPairs,
                     std::multimap<Value *, Value *> &LoadMoveSet,
                     Instruction *I) {
    // Skip to the first instruction past I.
    BasicBlock::iterator L = llvm::next(BasicBlock::iterator(I));

    DenseSet<Value *> Users;
    AliasSetTracker WriteSet(*AA);

    // Note: We cannot end the loop when we reach J because J could be moved
    // farther down the use chain by another instruction pairing. Also, J
    // could be before I if this is an inverted input.
    for (BasicBlock::iterator E = BB.end(); cast<Instruction>(L) != E; ++L) {
      if (trackUsesOfI(Users, WriteSet, I, L)) {
        if (L->mayReadFromMemory())
          LoadMoveSet.insert(ValuePair(L, I));
      }
    }
  }

  // In cases where both load/stores and the computation of their pointers
  // are chosen for vectorization, we can end up in a situation where the
  // aliasing analysis starts returning different query results as the
  // process of fusing instruction pairs continues. Because the algorithm
  // relies on finding the same use trees here as were found earlier, we'll
  // need to precompute the necessary aliasing information here and then
  // manually update it during the fusion process.
  void WIVectorize::collectLoadMoveSet(BasicBlock &BB,
                     std::vector<Value *> &PairableInsts,
                     ValueVectorMap &ChosenVectors,
                     std::multimap<Value *, Value *> &LoadMoveSet) {
    for (std::vector<Value *>::iterator PI = PairableInsts.begin(),
         PIE = PairableInsts.end(); PI != PIE; ++PI) {
      ValueVectorMap::iterator P = ChosenVectors.find(*PI);
      if (P == ChosenVectors.end()) continue;

      Instruction *I = cast<Instruction>(P->first);
      DenseMap<Value*, Value*> map;
      collectPairLoadMoveSet(BB, map, LoadMoveSet, I);
    }
  }  
  void WIVectorize::collectLoadMoveSet(BasicBlock &BB,
                     std::vector<Value *> &PairableInsts,
                     DenseMap<Value *, Value *> &ChosenPairs,
                     std::multimap<Value *, Value *> &LoadMoveSet) {
    for (std::vector<Value *>::iterator PI = PairableInsts.begin(),
         PIE = PairableInsts.end(); PI != PIE; ++PI) {
      DenseMap<Value *, Value *>::iterator P = ChosenPairs.find(*PI);
      if (P == ChosenPairs.end()) continue;

      Instruction *I = cast<Instruction>(P->first);
      collectPairLoadMoveSet(BB, ChosenPairs, LoadMoveSet, I);    
    }
  }

    void WIVectorize::fuseChosenVectors(BasicBlock &BB,
                     std::vector<Value *> &PairableInsts,
                     ValueVectorMap &ChosenVectors) {
        LLVMContext& Context = BB.getContext();    
        
        std::multimap<Value *, Value *> LoadMoveSet;
        collectLoadMoveSet(BB, PairableInsts, ChosenVectors, LoadMoveSet);
        
        DEBUG(dbgs() << "WIV: initial: \n" << BB << "\n");
        for (ValueVectorMap::iterator it = ChosenVectors.begin();
             it != ChosenVectors.end(); it++) {
            for (int i = 0; i < (*it).second->size(); i++) {
                ValueVector& v = *(*it).second;
                v[i]->dump();
            }
        }
        for (BasicBlock::iterator PI = BB.getFirstInsertionPt(); PI != BB.end();) {
            ValueVectorMap::iterator P = ChosenVectors.find(PI);
            if (P == ChosenVectors.end()) {
                ++PI;                
                continue;
            }        
            Instruction *I = cast<Instruction>(P->first);
            ValueVector& vec = *P->second;
            DEBUG(dbgs() << "WIV: fusing: \n" << *I << "\n");                    
            bool mismatch = false;
            for (unsigned i = 0; i < vec.size(); i++) {
                DEBUG(dbgs() << "WIV: with: \n" << *vec[i] << "\n");        
                if (I->getType() != vec[i]->getType()) {
                    mismatch = true;
                }
            }
            if (mismatch) {
                ++PI;
                continue;
            }
            ChosenVectors.erase(P);
            unsigned NumOperands = I->getNumOperands();
            SmallVector<Value *, 3> ReplacedOperands(NumOperands);       
            getReplacementInputsForVector(Context, I, &vec, ReplacedOperands);
            Instruction *K = I->clone();
            if (I->hasName()) K->takeName(I);
            
            /*if (I->getMetadata("wi") != NULL) {
                K->setMetadata("wi", I->getMetadata("wi"));
            }*/
            if (!isa<StoreInst>(K))
                K->mutateType(getVecTypeForVector(I->getType()));

            for (unsigned o = 0; o < NumOperands; ++o)
                K->setOperand(o, ReplacedOperands[o]);
            if (MemOpsOnly && isa<StoreInst>(K)) {
                Instruction *ins = cast<Instruction>(vec[vec.size()-1]);
                K->insertAfter(ins);
            } else {
                K->insertAfter(I);
            }
            Instruction* InsertionPt = K;
            ValueVector newVec;
                replaceOutputsOfVector(Context, I, &vec, K, InsertionPt, &newVec);            
            if (!isa<StoreInst>(I)) {
                I->replaceAllUsesWith((newVec)[0]);
                AA->replaceWithNewValue(I, (newVec)[0]);        
                for (int i = 0; i < vec.size(); i++) {
                    vec[i]->replaceAllUsesWith((newVec)[i+1]);        
                    AA->replaceWithNewValue(vec[i], (newVec)[i+1]);
                }
            }        
            //moveUsesOfIAfterJ(BB, LoadMoveSet, InsertionPt, I, vec);
            PI = llvm::next(BasicBlock::iterator(I));

            SE->forgetValue(I);
            I->eraseFromParent();            
            for (int i = 0; i < vec.size(); i++) { 
                Instruction* ins = cast<Instruction>(vec[i]);
                SE->forgetValue(ins);                
                ins->eraseFromParent();
            }
        }
        DEBUG(dbgs() << "WIV: final: \n" << BB << "\n");                
    }
  // This function fuses the chosen instruction pairs into vector instructions,
  // taking care preserve any needed scalar outputs and, then, it reorders the
  // remaining instructions as needed (users of the first member of the pair
  // need to be moved to after the location of the second member of the pair
  // because the vector instruction is inserted in the location of the pair's
  // second member).
  void WIVectorize::fuseChosenPairs(BasicBlock &BB,
                     std::vector<Value *> &PairableInsts,
                     DenseMap<Value *, Value *> &ChosenPairs) {
    LLVMContext& Context = BB.getContext();

    // During the vectorization process, the order of the pairs to be fused
    // could be flipped. So we'll add each pair, flipped, into the ChosenPairs
    // list. After a pair is fused, the flipped pair is removed from the list.
    std::vector<ValuePair> FlippedPairs;
    FlippedPairs.reserve(ChosenPairs.size());
    for (DenseMap<Value *, Value *>::iterator P = ChosenPairs.begin(),
         E = ChosenPairs.end(); P != E; ++P)
      FlippedPairs.push_back(ValuePair(P->second, P->first));
    for (std::vector<ValuePair>::iterator P = FlippedPairs.begin(),
         E = FlippedPairs.end(); P != E; ++P)
      ChosenPairs.insert(*P);

    std::multimap<Value *, Value *> LoadMoveSet;
    collectLoadMoveSet(BB, PairableInsts, ChosenPairs, LoadMoveSet);

    DEBUG(dbgs() << "WIV: initial: \n" << BB << "\n");

    for (BasicBlock::iterator PI = BB.getFirstInsertionPt(); PI != BB.end();) {
      DenseMap<Value *, Value *>::iterator P = ChosenPairs.find(PI);
      if (P == ChosenPairs.end()) {
        ++PI;
        continue;
      }

      if (getDepthFactor(P->first) == 0) {
        // These instructions are not really fused, but are tracked as though
        // they are. Any case in which it would be interesting to fuse them
        // will be taken care of by InstCombine.
        --NumFusedOps;
        ++PI;
        continue;
      }

      Instruction *I = cast<Instruction>(P->first),
        *J = cast<Instruction>(P->second);

      DEBUG(dbgs() << "WIV: fusing: " << *I <<
             " <-> " << *J << "\n");

      // Remove the pair and flipped pair from the list.
      DenseMap<Value *, Value *>::iterator FP = ChosenPairs.find(P->second);
      assert(FP != ChosenPairs.end() && "Flipped pair not found in list");
      ChosenPairs.erase(FP);
      ChosenPairs.erase(P);

      if (!canMoveUsesOfIAfterJ(BB, LoadMoveSet, I, J)) {
        DEBUG(dbgs() << "WIV: fusion of: " << *I <<
               " <-> " << *J <<
               " aborted because of non-trivial dependency cycle\n");
        --NumFusedOps;
        ++PI;
        continue;
      }

      bool FlipMemInputs;
      unsigned NumOperands = I->getNumOperands();
      SmallVector<Value *, 3> ReplacedOperands(NumOperands);
      getReplacementInputsForPair(Context, I, J, ReplacedOperands,
        FlipMemInputs);

      // Make a copy of the original operation, change its type to the vector
      // type and replace its operands with the vector operands.
      Instruction *K = I->clone();
      if (I->hasName()) K->takeName(I);
      
      if (I->getMetadata("wi") != NULL) {
	K->setMetadata("wi", I->getMetadata("wi"));
      }
      if (!isa<StoreInst>(K))
        K->mutateType(getVecTypeForPair(I->getType()));

      for (unsigned o = 0; o < NumOperands; ++o)
        K->setOperand(o, ReplacedOperands[o]);

      // If we've flipped the memory inputs, make sure that we take the correct
      // alignment.
      if (FlipMemInputs) {
        if (isa<StoreInst>(K))
          cast<StoreInst>(K)->setAlignment(cast<StoreInst>(J)->getAlignment());
        else
          cast<LoadInst>(K)->setAlignment(cast<LoadInst>(J)->getAlignment());
      }

      K->insertAfter(J);

      // Instruction insertion point:
      Instruction *InsertionPt = K;
      Instruction *K1 = 0, *K2 = 0;
      replaceOutputsOfPair(Context, I, J, K, InsertionPt, K1, K2,
        FlipMemInputs);

      // The use tree of the first original instruction must be moved to after
      // the location of the second instruction. The entire use tree of the
      // first instruction is disjoint from the input tree of the second
      // (by definition), and so commutes with it.

      moveUsesOfIAfterJ(BB, LoadMoveSet, InsertionPt, I, J);

      if (!isa<StoreInst>(I)) {
        I->replaceAllUsesWith(K1);
        J->replaceAllUsesWith(K2);
        AA->replaceWithNewValue(I, K1);
        AA->replaceWithNewValue(J, K2);
      }

      // Instructions that may read from memory may be in the load move set.
      // Once an instruction is fused, we no longer need its move set, and so
      // the values of the map never need to be updated. However, when a load
      // is fused, we need to merge the entries from both instructions in the
      // pair in case those instructions were in the move set of some other
      // yet-to-be-fused pair. The loads in question are the keys of the map.
      if (I->mayReadFromMemory()) {
        std::vector<ValuePair> NewSetMembers;
        VPIteratorPair IPairRange = LoadMoveSet.equal_range(I);
        VPIteratorPair JPairRange = LoadMoveSet.equal_range(J);
        for (std::multimap<Value *, Value *>::iterator N = IPairRange.first;
             N != IPairRange.second; ++N)
          NewSetMembers.push_back(ValuePair(K, N->second));
        for (std::multimap<Value *, Value *>::iterator N = JPairRange.first;
             N != JPairRange.second; ++N)
          NewSetMembers.push_back(ValuePair(K, N->second));
        for (std::vector<ValuePair>::iterator A = NewSetMembers.begin(),
             AE = NewSetMembers.end(); A != AE; ++A)
          LoadMoveSet.insert(*A);
      }

      // Before removing I, set the iterator to the next instruction.
      PI = llvm::next(BasicBlock::iterator(I));
      if (cast<Instruction>(PI) == J)
        ++PI;

      SE->forgetValue(I);
      SE->forgetValue(J);
      I->eraseFromParent();
      J->eraseFromParent();
    }

    DEBUG(dbgs() << "WIV: final: \n" << BB << "\n");
  }
  
  void WIVectorize::dropUnused(BasicBlock& BB) {
    
    BasicBlock::iterator J = BB.end();        
    BasicBlock::iterator I = llvm::prior(J);
    while (I != BB.begin()) {
      
      if (isa<ShuffleVectorInst>(*I) ||
	isa<ExtractElementInst>(*I) ||
	isa<InsertElementInst>(*I) ||
        isa<BitCastInst>(*I)) {
	
	  Value* V = dyn_cast<Value>(&(*I));
	  
	  if (V && V->use_empty()) {
	    SE->forgetValue(&(*I));
	    (*I).eraseFromParent();
	    // removed instruction could have messed up things
	    // start again from the end
	    I = BB.end();
	    J = llvm::prior(I);
	  } else {
	    J = llvm::prior(I);      		
	  }	  
      } else {
	J = llvm::prior(I);      		
      }
      I = J;      
    }
  }
  
}
char WIVectorize::ID = 0;
RegisterPass<WIVectorize>
  X("wi-vectorize", "Work item vectorization.");


/*static const char wi_vectorize_name[] = "Work Item Vectorization";
INITIALIZE_PASS_BEGIN(WIVectorize, WIV_NAME, wi_vectorize_name, false, false)
INITIALIZE_AG_DEPENDENCY(AliasAnalysis)
INITIALIZE_PASS_DEPENDENCY(ScalarEvolution)
INITIALIZE_PASS_END(WIVectorize, WIV_NAME, wi_vectorize_name, false, false)*/

FunctionPass *createWIVectorizePass() {
  return new WIVectorize();
}

