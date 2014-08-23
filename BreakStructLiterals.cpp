//===- Hello.cpp - Example code from "Writing an LLVM Pass" ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements two versions of the LLVM "Hello World" pass described
// in docs/WritingAnLLVMPass.html
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "break-struct-literals"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Scalar.h"
using namespace llvm;

namespace {
  struct BreakStructLiterals : public BasicBlockPass {
    static char ID; // Pass identification, replacement for typeid
    BreakStructLiterals() : BasicBlockPass(ID) { }

    virtual bool runOnBasicBlock(BasicBlock &BB) {
      SmallVector<Instruction*, 8> killed;
      for (BasicBlock::iterator I = BB.begin(), E = BB.end(); I != E; ++I) {
        if (StoreInst* storeInst = dyn_cast<StoreInst>(&*I)) {
          if (ConstantStruct* constStruct = dyn_cast<ConstantStruct>(storeInst->getValueOperand())) {
            walkStructConstant(storeInst->getPointerOperand(), constStruct, storeInst);
            killed.push_back(storeInst);
          }
        }
      }
      for (SmallVector<Instruction*, 8>::iterator I = killed.begin(), E = killed.end(); I != E; ++I) {
        (*I)->eraseFromParent();
      }
      return killed.size() > 0;
    }

    void walkStructConstant(Value *PtrVal, ConstantStruct *StructVal, Instruction *LastInst) {
      StructType *StructType = StructVal->getType();
      Value *ZeroVal = ConstantInt::get(StructType->getContext(), APInt(32, 0));
      for (unsigned I = 0, E = StructType->getNumElements(); I < E; ++I) {
        Value *ElementVal = StructVal->getOperand(I);

        Value *IndexVal = ConstantInt::get(StructType->getContext(), APInt(32, I));
        Value *GEPIndexes[2] = { ZeroVal, IndexVal };
        Instruction *ElementPtr = GetElementPtrInst::CreateInBounds(
                PtrVal, ArrayRef<Value*>(GEPIndexes, GEPIndexes + 2), "", LastInst);

        if (ConstantStruct* constStruct = dyn_cast<ConstantStruct>(ElementVal)) {
          walkStructConstant(ElementPtr, constStruct, LastInst);
        } else {
          new StoreInst(ElementVal, ElementPtr, LastInst);
        }
      }
    }
  };
}

char BreakStructLiterals::ID = 0;
static RegisterPass<BreakStructLiterals> X("break-struct-literals",
        "break stores of struct literals into several scalar stores");
