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

#define DEBUG_TYPE "fix-nested-extract-value"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Scalar.h"
#include <vector>

using namespace std;
using namespace llvm;

namespace {
  struct FixNestedExtractValue : public BasicBlockPass {
    static char ID; // Pass identification, replacement for typeid
    FixNestedExtractValue() : BasicBlockPass(ID) { }

    virtual bool runOnBasicBlock(BasicBlock &BB) {
      SmallVector<Instruction*, 8> killed;
      for (BasicBlock::iterator I = BB.begin(), E = BB.end(); I != E; ++I) {
        if (ExtractValueInst* EVInst = dyn_cast<ExtractValueInst>(&*I)) {
          IRBuilder<> Builder(&BB, EVInst);

          Value* Agg = EVInst->getAggregateOperand();
          Value* Storage = Builder.CreateAlloca(Agg->getType());
          Builder.CreateStore(Agg, Storage);

          vector<Value*> GEPIndexes;
          GEPIndexes.push_back(ConstantInt::get(EVInst->getContext(), APInt(32, 0)));
          for (unsigned I = 0, E = EVInst->getNumIndices(); I < E; ++I) {
            unsigned Index = EVInst->getIndices()[I];
            GEPIndexes.push_back(ConstantInt::get(EVInst->getContext(), APInt(32, Index)));
          }
          Value* Ptr = Builder.CreateInBoundsGEP(Storage, GEPIndexes);
          Value* Element = Builder.CreateLoad(Ptr);

          EVInst->replaceAllUsesWith(Element);
          killed.push_back(EVInst);
        }
      }
      for (SmallVector<Instruction*, 8>::iterator I = killed.begin(), E = killed.end(); I != E; ++I) {
        (*I)->eraseFromParent();
      }
      return killed.size() > 0;
    }
  };
}

char FixNestedExtractValue::ID = 0;
static RegisterPass<FixNestedExtractValue> X("fix-nested-extract-value",
        "eliminate nested extractvalue/insertvalue instructions");
