//=--3CStats.cpp--------------------------------------------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Implementation of all the methods in 3CStats.h
//===----------------------------------------------------------------------===//

#include "clang/3C/3CStats.h"
#include "clang/3C/ProgramInfo.h"
#include "clang/3C/Utils.h"
#include <time.h>

void PerformanceStats::startCompileTime() { CompileTimeSt = clock(); }

void PerformanceStats::endCompileTime() {
  CompileTime += getTimeSpentInSeconds(CompileTimeSt);
}

void PerformanceStats::startConstraintBuilderTime() {
  ConstraintBuilderTimeSt = clock();
}

void PerformanceStats::endConstraintBuilderTime() {
  ConstraintBuilderTime += getTimeSpentInSeconds(ConstraintBuilderTimeSt);
}

void PerformanceStats::startConstraintSolverTime() {
  ConstraintSolverTimeSt = clock();
}

void PerformanceStats::endConstraintSolverTime() {
  ConstraintSolverTime += getTimeSpentInSeconds(ConstraintSolverTimeSt);
}

void PerformanceStats::startArrayBoundsInferenceTime() {
  ArrayBoundsInferenceTimeSt = clock();
}

void PerformanceStats::endArrayBoundsInferenceTime() {
  ArrayBoundsInferenceTime += getTimeSpentInSeconds(ArrayBoundsInferenceTimeSt);
}

void PerformanceStats::startRewritingTime() { RewritingTimeSt = clock(); }

void PerformanceStats::endRewritingTime() {
  RewritingTime += getTimeSpentInSeconds(RewritingTimeSt);
}

void PerformanceStats::startTotalTime() { TotalTimeSt = clock(); }

void PerformanceStats::endTotalTime() {
  TotalTime += getTimeSpentInSeconds(TotalTimeSt);
}

void PerformanceStats::incrementNumAssumeBounds() { NumAssumeBoundsCasts++; }
void PerformanceStats::incrementNumCheckedCasts() { NumCheckedCasts++; }

void PerformanceStats::incrementNumWildCasts() { NumWildCasts++; }

void PerformanceStats::incrementNumFixedCasts() { NumFixedCasts++; }

void PerformanceStats::incrementNumITypes() { NumITypes++; }

// This is needed in some corner cases where we have to decrement the count.
void PerformanceStats::decrementNumITypes() { NumITypes--; }

void PerformanceStats::incrementNumCheckedRegions() { NumCheckedRegions++; }

void PerformanceStats::incrementNumUnCheckedRegions() { NumUnCheckedRegions++; }

void PerformanceStats::printPerformanceStats(llvm::raw_ostream &O,
                                             bool JsonFormat) {
  if (JsonFormat) {
    O << "[";

    O << "{\"TimeStats\": {\"TotalTime\":" << TotalTime;
    O << ", \"ConstraintBuilderTime\":" << ConstraintBuilderTime;
    O << ", \"ConstraintSolverTime\":" << ConstraintSolverTime;
    O << ", \"ArrayBoundsInferenceTime\":" << ArrayBoundsInferenceTime;
    O << ", \"RewritingTime\":" << RewritingTime;
    O << "}},\n";

    O << "{\"ReWriteStats\":{";
    O << "\"NumAssumeBoundsCasts\":" << NumAssumeBoundsCasts;
    O << ", \"NumCheckedCasts\":" << NumCheckedCasts;
    O << ", \"NumWildCasts\":" << NumWildCasts;
    O << ", \"NumFixedCasts\":" << NumFixedCasts;
    O << ", \"NumITypes\":" << NumITypes;
    O << ", \"NumCheckedRegions\":" << NumCheckedRegions;
    O << ", \"NumUnCheckedRegions\":" << NumUnCheckedRegions;
    O << "}}";

    O << "]";
  } else {
    O << "TimeStats\n";
    O << "TotalTime:" << TotalTime << "\n";
    O << "ConstraintBuilderTime:" << ConstraintBuilderTime << "\n";
    O << "ConstraintSolverTime:" << ConstraintSolverTime << "\n";
    O << "ArrayBoundsInferenceTime:" << ArrayBoundsInferenceTime << "\n";
    O << "RewritingTime:" << RewritingTime << "\n";

    O << "ReWriteStats\n";
    O << "NumAssumeBoundsCasts:" << NumAssumeBoundsCasts << "\n";
    O << "NumCheckedCasts:" << NumCheckedCasts << "\n";
    O << "NumWildCasts:" << NumWildCasts << "\n";
    O << "NumFixedCasts:" << NumFixedCasts << "\n";
    O << "NumITypes:" << NumITypes << "\n";
    O << "NumCheckedRegions:" << NumCheckedRegions << "\n";
    O << "NumUnCheckedRegions:" << NumUnCheckedRegions << "\n";
  }
}

// Record Checked/Unchecked regions.
bool StatsRecorder::VisitCompoundStmt(clang::CompoundStmt *S) {
  auto &PStats = Info->getPerfStats();
  if (S != nullptr) {
    auto PSL = PersistentSourceLoc::mkPSL(S, *Context);
    if (PSL.valid() && canWrite(PSL.getFileName())) {
      switch (S->getWrittenCheckedSpecifier()) {
      case CSS_None:
        // Do nothing
        break;
      case CSS_Unchecked:
        PStats.incrementNumUnCheckedRegions();
        break;
      case CSS_Memory:
      case CSS_Bounds:
        PStats.incrementNumCheckedRegions();
        break;
      }
    }
  }
  return true;
}

// Record itype declarations.
bool StatsRecorder::VisitDecl(clang::Decl *D) {
  auto &PStats = Info->getPerfStats();
  
  auto StaticMarker = [this, &PStats](std::string FuncName, std::string VarName, PersistentSourceLoc PSL) -> void {
    if (isFunctionRetOrParamVisited(FuncName, VarName, PSL)) {
      return;
    }
    markFunctionRetOrParamVisited(FuncName, VarName, PSL);
    PStats.incrementNumITypes();
  };

  auto GlobalMarker = [&PStats](std::string FuncName, std::string VarName) -> void {
    if (isFunctionRetOrParamVisited(FuncName, VarName)) {
      return;
    }
    markFunctionRetOrParamVisited(FuncName, VarName);
    PStats.incrementNumITypes();
  };

  if (D != nullptr) {
    auto PSL = PersistentSourceLoc::mkPSL(D, *Context);
    if (PSL.valid() && canWrite(PSL.getFileName())) {
      if (DeclaratorDecl *DD = dyn_cast<DeclaratorDecl>(D)) {
        if (DD->hasInteropTypeExpr()) {
          // We have to handle multiple cases here.
          // FunctionDecl is for return types.
          if (FunctionDecl *FD = dyn_cast<FunctionDecl>(DD)) {
            // If it is a FunctionDecl and it is a static function, then we add
            // it to the function visited map along with the return type.
            bool IsStatic = !FD->isGlobal();
            if (IsStatic) {
              auto DefPSL = PersistentSourceLoc::mkPSL(FD, *Context);
              StaticMarker(FD->getNameAsString(), RETVAR, DefPSL);
            } else {
              // If the FunctionDecl is not static, then we add it to the global
              // function visited map along with the return type.
              GlobalMarker(FD->getNameAsString(), RETVAR);
            }
          } else if (ParmVarDecl *PVD = dyn_cast<ParmVarDecl>(DD)) {
            // If it is a ParmVarDecl, then we add it to the function visited
            // map along with the function it is a part of.
            DeclContext *DC = PVD->getParentFunctionOrMethod();
            if (DC) {
              if (FunctionDecl *FD = dyn_cast<FunctionDecl>(DC)) {
                bool IsStatic = !FD->isGlobal();
                if (IsStatic) {
                  FunctionDecl *FDef = FD->getDefinition();
                  // static function should be defined in the same file.
                  if (FDef) {
                    auto DefPSL = PersistentSourceLoc::mkPSL(FDef, *Context);
                    StaticMarker(FDef->getNameAsString(), PVD->getNameAsString(), DefPSL);
                  }
                } else {
                  GlobalMarker(FD->getNameAsString(), PVD->getNameAsString());
                }
              }
            }
          } else {
            // If it is a anything else, then we can just increment the count.
            PStats.incrementNumITypes();
          }
        }
      }
    }
  }
  return true;
}

// Record checked to wild casts.
bool StatsRecorder::VisitCStyleCastExpr(clang::CStyleCastExpr *C) {
  auto &PStats = Info->getPerfStats();
  if (C != nullptr) {
    auto PSL = PersistentSourceLoc::mkPSL(C, *Context);
    if (PSL.valid() && canWrite(PSL.getFileName())) {
      QualType SrcT = C->getSubExpr()->getType();
      QualType DstT = C->getType();
      if (SrcT->isCheckedPointerType() && !DstT->isCheckedPointerType())
        PStats.incrementNumWildCasts();
    }
  }
  return true;
}

// Record bounds casts.
bool StatsRecorder::VisitBoundsCastExpr(clang::BoundsCastExpr *B) {
  auto &PStats = Info->getPerfStats();
  if (B != nullptr) {
    auto PSL = PersistentSourceLoc::mkPSL(B, *Context);
    if (PSL.valid() && canWrite(PSL.getFileName()))
      PStats.incrementNumAssumeBounds();
  }
  return true;
}
