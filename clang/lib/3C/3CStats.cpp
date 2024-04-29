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
  
  auto StaticMarker = [this, &PStats](std::string FuncName, std::string VarName,
                                      FunctionDecl *FD) -> void {
    if (isFunctionRetOrParamVisited(FuncName, VarName, FD, *Context)) {
      return;
    }
    markFunctionRetOrParamVisited(FuncName, VarName, FD, *Context);
    PStats.incrementNumITypes();
  };

  auto GlobalMarker = [this, &PStats](std::string FuncName, std::string VarName,
                                FunctionDecl *FD) -> void {
    if (isFunctionRetOrParamVisited(FuncName, VarName, FD, *Context, true)) {
      return;
    }
    markFunctionRetOrParamVisited(FuncName, VarName, FD, *Context, true);
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
              StaticMarker(FD->getNameAsString(), RETVAR, FD);
            } else {
              GlobalMarker(FD->getNameAsString(), RETVAR, FD);
            }
          } else if (ParmVarDecl *PVD = dyn_cast<ParmVarDecl>(DD)) {
            // If it is a ParmVarDecl, then we add it to the function visited
            // map along with the function it is a part of.
            DeclContext *DC = PVD->getParentFunctionOrMethod();
            if (DC) {
              if (FunctionDecl *FD = dyn_cast<FunctionDecl>(DC)) {
                bool IsStatic = !FD->isGlobal();
                if (IsStatic) {
                  StaticMarker(FD->getNameAsString(), PVD->getNameAsString(),
                               FD);
                } else {
                  GlobalMarker(FD->getNameAsString(), PVD->getNameAsString(),
                               FD);
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

void CastInfoAggregator::dumpStats(std::string FilePath) {
  std::error_code EC;
  llvm::raw_fd_ostream Output(FilePath, EC, llvm::sys::fs::F_Text);

  if (!EC) {
    Output << "[";
    bool FirstOuter = true;
    for (auto &It : getData()) {

      if (!FirstOuter)
        Output << ",";
      else
        FirstOuter = false;

      bool FirstInner = true;
      Output << "{\"Dst\":\"" << It.Dst << "\",";
      Output << "\"Src\":\"" << It.Src << "\",";
      Output << "\"Locs\":[";
      for (auto &L : It.Locs) {

        if (!FirstInner)
          Output << ",";
        else
          FirstInner = false;

        Output << "{\"file\":\"" << L.getFileName() << "\",";
        Output << "\"line\":" << L.getLineNo() << ",";
        Output << "\"colstart\":" << L.getColSNo() << ",";
        Output << "\"colend\":" << L.getColENo() << "}";
      }
      Output << "]}";
    }
    Output << "]";
  }
}

void CastInfoAggregator::addCastInfo(std::string &Dst, std::string &Src,
                                     PersistentSourceLoc &Loc) {
  std::vector<CastInfoMapType> &M = getData();

  // Check if the cast already exists.
  for (auto &It : M) {
    if (It.Dst == Dst && It.Src == Src) {
      It.Locs.push_back(Loc);
      return;
    }
  }
  // Insert a new entry.
  CastInfoMapType C;
  C.Dst = Dst;
  C.Src = Src;
  C.Locs.push_back(Loc);
  M.push_back(C);
  return;
}

void VoidInfoAggregator::dumpStats(std::string FilePath) {
  std::error_code EC;
  llvm::raw_fd_ostream Output(FilePath, EC, llvm::sys::fs::F_Text);

  if (!EC) {
    Output << "[";
    bool FirstOuter = true;
    for (auto &It : getData()) {

      if (!FirstOuter)
        Output << ",";
      else
        FirstOuter = false;

      Output << "{\"file\":\"" << It.Loc.getFileName() << "\",";
      Output << "\"line\":" << It.Loc.getLineNo() << ",";
      Output << "\"colstart\":" << It.Loc.getColSNo() << ",";
      Output << "\"colend\":" << It.Loc.getColENo() << ",";
      Output << "\"type\":\"" << getTypeString(It.Type) << "\",";
      Output << "\"name\":\"" << It.Name << "\",";
      Output << "\"generic\":" << It.Generic << "}";
    }
    Output << "]";
  }
}

void VoidInfoAggregator::addVoidInfo(PersistentSourceLoc &Loc, std::string &Name) {
  std::vector<VoidInfoMapType> &M = getData();
  for (auto &It : M) {
    if (It.Loc == Loc)
      return;
  }
  VoidInfoMapType V;
  V.Loc = Loc;
  V.Type = VoidInfoMapType::T_UNKNOWN;
  V.Name = Name;
  V.Generic = false;
  M.push_back(V);
}

void VoidInfoAggregator::updateType(PersistentSourceLoc &Loc,
                                   VoidInfoMapType::VType Type,
                                   const std::string &Name) {
  std::vector<VoidInfoMapType> &M = getData();
  bool Found = false;
  for (auto &It : M) {
    if (It.Loc == Loc && It.Type == VoidInfoMapType::T_UNKNOWN && Loc.valid()) {
      Found = true;
      It.Type = Type;
    }
  }
  // Special case for Typedefs where PSL is not valid.
  // If Loc is Invalid and the Type is TYPEDEF, then we try to match the name.
  if (Type == VoidInfoMapType::T_TYPEDEF) {
    if (!Found) {
      for (auto &It : M) {
        if (!It.Loc.valid() && It.Name == Name && It.Type == VoidInfoMapType::T_UNKNOWN) {
          It.Type = Type;
          // If we currently have a location, then we update it.
          if (Loc.valid())
            It.Loc = Loc;
          return;
        }
      }
    }
  }
}

// This function only gets used for function params and returns.
// Currenly 3C doesn't write generics for members.
void VoidInfoAggregator::updateGeneric(PersistentSourceLoc &Loc, bool Generic) {
  std::vector<VoidInfoMapType> &M = getData();
  for (auto &It : M) {
    if (It.Loc == Loc) {
      It.Generic = Generic;
      return;
    }
  }
}

std::string VoidInfoAggregator::getTypeString(VoidInfoMapType::VType Type) {
  switch (Type) {
  case VoidInfoMapType::T_LOCAL:
    return "Local";
  case VoidInfoMapType::T_PARAM:
    return "Param";
  case VoidInfoMapType::T_RETURN:
    return "Return";
  case VoidInfoMapType::T_TYPEDEF:
    return "Typedef";
  case VoidInfoMapType::T_MEMBER:
    return "Member";
  case VoidInfoMapType::T_GLOBAL:
    return "Global";
  case VoidInfoMapType::T_UNKNOWN:
    return "Unknown";
  }
  return "Unknown";
}

void MacroInfoAggregator::dumpStats(std::string FilePath) {
  std::error_code EC;
  llvm::raw_fd_ostream Output(FilePath, EC, llvm::sys::fs::F_Text);

  if (!EC) {
    Output << "[";
    bool First = true;
    for (auto &It : getData()) {
      if (!First)
        Output << ",";
      else
        First = false;

      Output << "{\"file\":\"" << It.getFileName() << "\",";
      Output << "\"line\":" << It.getLineNo() << ",";
      Output << "\"colstart\":" << It.getColSNo() << ",";
      Output << "\"colend\":" << It.getColENo() << "}";
    }
    Output << "]";
  }
}

void MacroInfoAggregator::addMacroInfo(std::vector<PersistentSourceLoc> &Locs) {
  std::vector<PersistentSourceLoc> &M = getData();
  for (auto &It : Locs) {
    M.push_back(It);
  }
}