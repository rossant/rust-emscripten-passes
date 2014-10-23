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

#define DEBUG_TYPE "break-struct-arguments"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils/Cloning.h"

#include <vector>

using namespace std;
using namespace llvm;

namespace {
  struct BreakStructArguments : public ModulePass {
    static char ID; // Pass identification, replacement for typeid
    BreakStructArguments() : ModulePass(ID) { }

    virtual bool runOnModule(Module &M) {
      // Step 0: Find all functions with struct arguments.
      vector<Function*> StructArgFuncs;
      for (Module::iterator F = M.begin(), FE = M.end(); F != FE; ++F) {
        errs() << "function: " << F->getName() << '\n';
        for (Function::arg_iterator A = F->arg_begin(), AE = F->arg_end(); A != AE; ++A) {
          if (A->getType()->getTypeID() == Type::StructTyID) {
            errs() << "  found struct argument on " << F->getName() << '\n';
            assert(!F->isDeclaration() && "can't flatten nonlocal functions");
            StructArgFuncs.push_back(&*F);
          }
        }
      }

      for (vector<Function*>::iterator FI = StructArgFuncs.begin(), FE = StructArgFuncs.end(); FI != FE; ++FI) {
        // Create flattened shim for struct-argument function.
        Function *OldF = *FI;
        Constant *NewFConst = M.getOrInsertFunction(
            ("flat$" + OldF->getName()).str(),
            flattenType(OldF->getFunctionType()),
            OldF->getAttributes());
        Function *NewF = dyn_cast<Function>(NewFConst);


        // Rewrite all callsites to call the new function instead.
        OldF->replaceAllUsesWith(ConstantExpr::getBitCast(NewF, OldF->getType()));


        // Finish creating flattened shim.
        BasicBlock *BB = BasicBlock::Create(NewF->getContext(), "flat-wrapper", NewF);

        IRBuilder<> Build(BB);
        vector<Value*> ArgVals;

        Function::arg_iterator NewA = NewF->arg_begin();
        for (Function::arg_iterator OldA = OldF->arg_begin(), OldAE = OldF->arg_end(); OldA != OldAE; ++OldA) {
          // packArgument advances NewA as needed.
          Value *PackedArg = packArgument(Build, &*OldA, NewA);
          ArgVals.push_back(PackedArg);
        }
        assert(NewA == NewF->arg_end());

        CallInst *Call = Build.CreateCall(OldF, ArgVals);
        if (OldF->getReturnType()->getTypeID() == Type::VoidTyID) {
          Build.CreateRetVoid();
        } else {
          Build.CreateRet(Call);
        }

        // Inline original function into shim.
        InlineFunctionInfo IFI;
        InlineFunction(Call, IFI);
      }

      vector<Instruction*> DeadCalls;
      for (Module::iterator F = M.begin(), FE = M.end(); F != FE; ++F) {
        for (Function::iterator BB = F->begin(), BBE = F->end(); BB != BBE; ++BB) {
          for (BasicBlock::iterator I = BB->begin(), IE = BB->end(); I != IE; ++I) {
            if (InvokeInst *Invoke = dyn_cast<InvokeInst>(&*I)) {
              IRBuilder<> Build(&*BB, I);

              Value *OldFuncPtr = Invoke->getCalledValue();
              PointerType *OldFuncPtrType = dyn_cast<PointerType>(OldFuncPtr->getType());
              Type *OldFuncType = OldFuncPtrType->getElementType();
              Type *NewFuncType = flattenType(dyn_cast<FunctionType>(OldFuncType));
              PointerType *NewFuncPtrType = PointerType::get(NewFuncType, OldFuncPtrType->getAddressSpace());

              if (NewFuncType == OldFuncType) {
                continue;
              }

              Value *NewFuncPtr = Build.CreateBitCast(OldFuncPtr, NewFuncPtrType);

              errs() << "want to fixup invoke:\n"
                << *Invoke << '\n'
                << "  old func ty: " << *OldFuncType << '\n'
                << "  new func ty: " << *NewFuncType << '\n';

              vector<Value*> NewArgs;
              for (unsigned I = 0, E = Invoke->getNumArgOperands(); I != E; ++I) {
                unpackArgumentInto(Build, NewArgs, Invoke->getArgOperand(I));
              }
              Build.CreateInvoke(NewFuncPtr, Invoke->getNormalDest(), Invoke->getUnwindDest(), NewArgs);
              DeadCalls.push_back(Invoke);
            } else if (CallInst *Call = dyn_cast<CallInst>(&*I)) {
              IRBuilder<> Build(&*BB, I);

              Value *OldFuncPtr = Call->getCalledValue();
              PointerType *OldFuncPtrType = dyn_cast<PointerType>(OldFuncPtr->getType());
              Type *OldFuncType = OldFuncPtrType->getElementType();
              Type *NewFuncType = flattenType(dyn_cast<FunctionType>(OldFuncType));
              PointerType *NewFuncPtrType = PointerType::get(NewFuncType, OldFuncPtrType->getAddressSpace());

              if (NewFuncType == OldFuncType) {
                continue;
              }

              Value *NewFuncPtr = Build.CreateBitCast(OldFuncPtr, NewFuncPtrType);

              errs() << "want to fixup call:\n"
                << *Call << '\n'
                << "  old func ty: " << *OldFuncType << '\n'
                << "  new func ty: " << *NewFuncType << '\n';

              vector<Value*> NewArgs;
              for (unsigned I = 0, E = Call->getNumArgOperands(); I != E; ++I) {
                unpackArgumentInto(Build, NewArgs, Call->getArgOperand(I));
              }
              Build.CreateCall(NewFuncPtr, NewArgs);
              DeadCalls.push_back(Call);
            }
          }
        }
      }

      for (vector<Instruction*>::iterator II = DeadCalls.begin(), IE = DeadCalls.end(); II != IE; ++II) {
        Instruction *I = *II;
        I->eraseFromParent();
      }

      return true;
    }

    FunctionType *flattenType(FunctionType *Ty) {
      vector<Type*> ArgTys;

      for (FunctionType::param_iterator I = Ty->param_begin(), E = Ty->param_end(); I != E; ++I) {
        if (StructType *StructTy = dyn_cast<StructType>(*I)) {
          flattenStructInto(ArgTys, StructTy);
        } else {
          ArgTys.push_back(*I);
        }
      }

      return FunctionType::get(Ty->getReturnType(), ArgTys, Ty->isVarArg());
    }

    void flattenStructInto(vector<Type*>& ArgTys, StructType *Ty) {
      for (StructType::element_iterator I = Ty->element_begin(), E = Ty->element_end(); I != E; ++I) {
        Type *ElemTy = *I;
        if (StructType *StructElemTy = dyn_cast<StructType>(ElemTy)) {
          flattenStructInto(ArgTys, StructElemTy);
        } else {
          ArgTys.push_back(ElemTy);
        }
      }
    }

    Value *packArgument(IRBuilder<> &Build, Argument *OldArg, Function::arg_iterator &NewA) {
      if (StructType *OldArgStructTy = dyn_cast<StructType>(OldArg->getType())) {
        Value *Alloca = Build.CreateAlloca(OldArgStructTy);
        populateStruct(Build, Alloca, NewA);
        return Build.CreateLoad(Alloca);
      } else {
        Value *NewArg = &*NewA;
        ++NewA;
        return NewArg;
      }
    }

    void populateStruct(IRBuilder<> &Build, Value *Ptr, Function::arg_iterator &NewA) {
      PointerType *PtrTy = dyn_cast<PointerType>(Ptr->getType());
      if (StructType *Ty = dyn_cast<StructType>(PtrTy->getElementType())) {
        for (unsigned I = 0, E = Ty->getNumElements(); I != E; ++I) {
          vector<Value*> Indexes;
          Indexes.push_back(ConstantInt::get(Ty->getContext(), APInt(32, 0)));
          Indexes.push_back(ConstantInt::get(Ty->getContext(), APInt(32, I)));
          Value *ElemPtr = Build.CreateInBoundsGEP(Ptr, Indexes);
          populateStruct(Build, ElemPtr, NewA);
        }
      } else {
        Value *Val = &*NewA;
        ++NewA;
        Build.CreateStore(Val, Ptr);
      }
    }

    void unpackArgumentInto(IRBuilder<> &Build, vector<Value*> &NewArgs, Value *OldArg) {
      if (StructType *Ty = dyn_cast<StructType>(OldArg->getType())) {
        for (unsigned I = 0, E = Ty->getNumElements(); I != E; ++I) {
          Value *Elem = Build.CreateExtractValue(OldArg, ArrayRef<unsigned>(I));
          unpackArgumentInto(Build, NewArgs, Elem);
        }
      } else {
        NewArgs.push_back(OldArg);
      }
    }
  };
}

char BreakStructArguments::ID = 0;
static RegisterPass<BreakStructArguments> X("break-struct-arguments",
        "flatten out struct arguments of functions");
