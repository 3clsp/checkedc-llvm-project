//=--3CStats.h----------------------------------------------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// This class contains all the stats related to the conversion computed by 3C.
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_3C_3CSTATS_H
#define LLVM_CLANG_3C_3CSTATS_H

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Stmt.h"
#include "llvm/Support/raw_ostream.h"
#include "clang/3C/PersistentSourceLoc.h"

#define RETVAR "$ret"

class PerformanceStats {
public:
  double CompileTime;
  double ConstraintBuilderTime;
  double ConstraintSolverTime;
  double ArrayBoundsInferenceTime;
  double RewritingTime;
  double TotalTime;

  // Rewrite Stats
  unsigned long NumAssumeBoundsCasts;
  unsigned long NumCheckedCasts;
  unsigned long NumWildCasts;
  unsigned long NumFixedCasts;
  unsigned long NumITypes;
  unsigned long NumCheckedRegions;
  unsigned long NumUnCheckedRegions;

  PerformanceStats() {
    CompileTime = ConstraintBuilderTime = 0;
    ConstraintSolverTime = ArrayBoundsInferenceTime = 0;
    RewritingTime = TotalTime = 0;

    CompileTimeSt = ConstraintBuilderTimeSt = 0;
    ConstraintSolverTimeSt = ArrayBoundsInferenceTimeSt = 0;
    RewritingTimeSt = TotalTimeSt = 0;

    NumAssumeBoundsCasts = NumCheckedCasts = 0;
    NumWildCasts = NumITypes = NumFixedCasts = 0;

    NumCheckedRegions = NumUnCheckedRegions = 0;
  }

  void startCompileTime();
  void endCompileTime();

  void startConstraintBuilderTime();
  void endConstraintBuilderTime();

  void startConstraintSolverTime();
  void endConstraintSolverTime();

  void startArrayBoundsInferenceTime();
  void endArrayBoundsInferenceTime();

  void startRewritingTime();
  void endRewritingTime();

  void startTotalTime();
  void endTotalTime();

  void incrementNumAssumeBounds();
  void incrementNumCheckedCasts();
  void incrementNumWildCasts();
  void incrementNumFixedCasts();
  void incrementNumITypes();
  void decrementNumITypes();
  void incrementNumCheckedRegions();
  void incrementNumUnCheckedRegions();

  void printPerformanceStats(llvm::raw_ostream &O, bool JsonFormat);

private:
  clock_t CompileTimeSt;
  clock_t ConstraintBuilderTimeSt;
  clock_t ConstraintSolverTimeSt;
  clock_t ArrayBoundsInferenceTimeSt;
  clock_t RewritingTimeSt;
  clock_t TotalTimeSt;
};

class ProgramInfo;

// Class to record stats by visiting AST.
class StatsRecorder : public clang::RecursiveASTVisitor<StatsRecorder> {
public:
  explicit StatsRecorder(clang::ASTContext *C, ProgramInfo *I)
      : Context(C), Info(I) {}

  bool VisitCompoundStmt(clang::CompoundStmt *S);
  bool VisitDecl(clang::Decl *D);
  bool VisitCStyleCastExpr(clang::CStyleCastExpr *C);
  bool VisitBoundsCastExpr(clang::BoundsCastExpr *B);

private:
  clang::ASTContext *Context;
  ProgramInfo *Info;
};

// Base class to store aggregated data for root cause analysis.
// The data stored depends on the template parameter.
template <typename T>
class RootCauseAggregator {
  public:
    virtual void dumpStats(std::string FilePath) = 0;
    T &getData() { return Data; }
  private:
    T Data;
};

// Type to store invalid cast information.
// Can be made into a map of pair to vector of locations if needed.
struct CastInfoMapType {
  std::string Dst;
  std::string Src;
  std::vector<PersistentSourceLoc> Locs;
};

// Aggregator for invliad cast information.
class CastInfoAggregator : public RootCauseAggregator<std::vector<CastInfoMapType>> {
  public:
    void dumpStats(std::string FilePath) override;
    void addCastInfo(std::string &Dst, std::string &Src, PersistentSourceLoc &Loc);
};

struct VoidInfoMapType {
  PersistentSourceLoc Loc;
  enum VType {
    T_LOCAL,
    T_PARAM,
    T_RETURN,
    T_MEMBER,
    T_GLOBAL,
    T_TYPEDEF,
    T_UNKNOWN
  };
  VType Type;
  std::string Name;
  bool Generic;
};

// Aggregator for void information.
class VoidInfoAggregator : public RootCauseAggregator<std::vector<VoidInfoMapType>> {
  public:
    void dumpStats(std::string FilePath) override;
    void addVoidInfo(PersistentSourceLoc &Loc, std::string &Name);
    void updateType(PersistentSourceLoc &Loc,
                    VoidInfoMapType::VType Type,
                    const std::string &Name="");
    void updateGeneric(PersistentSourceLoc &Loc, bool Generic);
    std::string getTypeString(VoidInfoMapType::VType Type);
};

class MacroInfoAggregator : public RootCauseAggregator<std::vector<PersistentSourceLoc>> {
  public:
    void dumpStats(std::string FilePath) override;
    void addMacroInfo(std::vector<PersistentSourceLoc> &Locs);
};

#endif // LLVM_CLANG_3C_3CSTATS_H
