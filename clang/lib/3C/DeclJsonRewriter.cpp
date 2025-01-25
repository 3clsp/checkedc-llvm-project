//=--DeclJsonRewriter.cpp-----------------------------------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Function that handles writing the results back to json
//===----------------------------------------------------------------------===//

#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/3C/RewriteUtils.h"
#include "clang/3C/3CGlobalOptions.h"
#include <sstream>

using namespace llvm;
using namespace clang;

std::set<std::string> isHavingCast(ProgramInfo &I, ConstraintVariable &CV) {
  for (auto &CIs : I.getCastInformation()) {
    if (CIs.first.find(&CV) != CIs.first.end()) {
      return std::set<std::string>(CIs.second.begin(), CIs.second.end());
    }
  }
  return std::set<std::string>();
}

class DeclJsonVisitor : public RecursiveASTVisitor<DeclJsonVisitor> {
public:
  explicit DeclJsonVisitor(ASTContext *Context,
                           ProgramInfo &I)
    : Context(Context), Info(I) { }

  bool VisitFunctionDecl(FunctionDecl *D) {
    if (D->hasBody() && !D->getNameAsString().empty() && D->isThisDeclarationADefinition()) {
      std::string FuncName = D->getNameAsString();
      bool IsStatic = D->isStatic();
      auto PSL = PersistentSourceLoc::mkPSL(D, *Context);
      auto FuncK = std::make_tuple(FuncName, IsStatic, PSL.getFileName());
      // Ignore if in system header
      if (DeclJsonVisitor::isInSystemHeader(PSL.getFileName())) {
        return true;
      }
      // Did we already process this function?
      if (Info.FnArrPtrs.find(FuncK) == Info.FnArrPtrs.end() &&
        Info.FnNtArrPtrs.find(FuncK) == Info.FnNtArrPtrs.end()) {
        auto &ABInfo = Info.getABoundsInfo();
        // No!
        unsigned i = 0;
        auto &EnvMap = Info.getConstraints().getVariables();
        for (i = 0; i < D->getNumParams(); i++) {
          ParmVarDecl *PVD = D->getParamDecl(i);
          std::string BaseTypeStr = PVD->getType().getAsString();
          auto COpt = Info.getVariable(PVD, Context);
          std::set<unsigned> ArrInds;
          std::set<unsigned> NtArrInds;
          if (COpt.hasValue()) {
            ConstraintVariable &CV = COpt.getValue();
            std::set<std::string> Casts = isHavingCast(Info, CV);
            PVConstraint *PV = dyn_cast_or_null<PVConstraint>(&CV);
            if (PV) {
              ArrInds.clear();
              NtArrInds.clear();
              for (unsigned j = 0; j < PV->getCvars().size(); j++) {
                if (PV->hasPtyNtArr(EnvMap, j)) {
                  NtArrInds.insert(j);
                } else if (PV->hasPtyArr(EnvMap, j)) {
                  ArrInds.insert(j);
                }
              }
              std::string BVar = "Invalid";
              unsigned bidx = 0;
              std::string BVarN = "";
              if ((!ArrInds.empty() || !NtArrInds.empty()) && PV->hasBoundsKey()) {
                BoundsKey PrmBKey = PV->getBoundsKey();
                ABounds *RB = ABInfo.getBounds(PrmBKey);
                if (RB != nullptr) {
                  BoundsKey RBKey = RB->getLengthKey();
                  auto *RBVar = ABInfo.getProgramVar(RBKey);
                  if (RBVar != nullptr) {
                    auto RBVarName = RBVar->getVarName();
                    if (RBVar->isNumConstant()) {
                      BVar = "CONSTANT";
                      std::stringstream g(RBVarName);
                      g >> bidx;
                    } else if (RBVar->getScope() == GlobalScope::getGlobalScope()) {
                      BVar = "GLOBAL";
                      BVarN = RBVarName;
                    } else {
                      bool found = false;
                      for (unsigned k = 0; k < D->getNumParams(); k++) {
                        if (D->getParamDecl(k)->getName().str() == RBVarName) {
                          bidx = k;
                          found = true;
                          BVar = "PARAMETER";
                          break;
                        }
                      }
                      if (!found) {
                        for (unsigned k = 0; k < D->getNumParams(); k++) {
                          if (ABInfo.getVariable(D->getParamDecl(k)) ==
                              RBKey) {
                            bidx = k;
                            BVar = "PARAMETER";
                            break;
                          }
                        }
                      }
                    }
                  }
                }
              }
              auto BndsTup = std::make_tuple(BVar, bidx, BVarN);
              if (!NtArrInds.empty() && !ArrInds.empty()) {
                auto ToInNtArr = std::make_tuple(i, BaseTypeStr, Casts, NtArrInds, BndsTup);
                Info.FnNtArrPtrs[FuncK].insert(ToInNtArr);
                auto ToInArr = std::make_tuple(i, BaseTypeStr, Casts, ArrInds, BndsTup);
                Info.FnArrPtrs[FuncK].insert(ToInArr);
              }
              else if (!NtArrInds.empty()) {
                auto ToIn = std::make_tuple(i, BaseTypeStr, Casts, NtArrInds, BndsTup);
                Info.FnNtArrPtrs[FuncK].insert(ToIn);
              } else {
                auto ToIn = std::make_tuple(i, BaseTypeStr, Casts, ArrInds, BndsTup);
                Info.FnArrPtrs[FuncK].insert(ToIn);
              }

            }
          }


        }
      }
    }
    return true;
  }

  bool VisitRecordDecl(RecordDecl *Declaration) {
    static int InnerCount = 0;
    if (Declaration->isThisDeclarationADefinition()) {
      RecordDecl *Definition = Declaration->getDefinition();
      assert("Declaration is a definition, but getDefinition() is null?"
             && Definition);
      FullSourceLoc FL = Context->getFullLoc(Definition->getBeginLoc());
      if (FL.isValid()) {
        SourceManager &SM = Context->getSourceManager();
        FileID FID = FL.getFileID();
        const FileEntry *FE = SM.getFileEntryForID(FID);
        std::string StName = Definition->getNameAsString();
        if (StName.empty()) {
          const TypedefNameDecl *TypedefDecl = nullptr;
          if (Definition->getTypedefNameForAnonDecl()) {
            TypedefDecl = Definition->getTypedefNameForAnonDecl();
          }
          if (TypedefDecl) {
            StName = TypedefDecl->getNameAsString();
          } else {
            DeclContext *ParentDeclCtx = Definition->getParent();
            if (ParentDeclCtx && isa<RecordDecl>(ParentDeclCtx)) {
              StName = cast<RecordDecl>(ParentDeclCtx)->getNameAsString();
              if (StName.empty()) {
                RecordDecl *ParentDecl = cast<RecordDecl>(ParentDeclCtx);
                if (ParentDecl->getTypedefNameForAnonDecl()) {
                  TypedefDecl = ParentDecl->getTypedefNameForAnonDecl();
                }
                if (TypedefDecl) {
                  StName = TypedefDecl->getNameAsString();
                }
              }
              if (StName.empty()) {
                StName = "AnonymousStructOrUnion";
              }
              StName += "_anon_" + std::to_string(InnerCount);
              InnerCount++;
            } else {
              StName = "AnonymousStructOrUnion" + std::to_string(InnerCount);
            }
          }
        }
        auto &ABInfo = Info.getABoundsInfo();
        if (FE && FE->isValid()
            && Info.StArrPtrs.find(StName) == Info.StArrPtrs.end()
            && Info.StNtArrPtrs.find(StName) == Info.StNtArrPtrs.end()) {
          unsigned i = 0;
          auto &EnvMap = Info.getConstraints().getVariables();
          for (auto *const D : Definition->fields()) {
            FieldDecl *PVD = D;
            auto COpt = Info.getVariable(PVD, Context);
            std::string BaseTypeStr = PVD->getType().getAsString();
            std::set<unsigned> ArrInds;
            std::set<unsigned> NtArrInds;
            if (COpt.hasValue()) {
              ConstraintVariable &CV = COpt.getValue();
              std::set<std::string> Casts = isHavingCast(Info, CV);
              PVConstraint *PV = dyn_cast_or_null<PVConstraint>(&CV);
              if (PV) {
                ArrInds.clear();
                NtArrInds.clear();
                for (unsigned j = 0; j < PV->getCvars().size(); j++) {
                  if (PV->hasPtyNtArr(EnvMap, j)) {
                    NtArrInds.insert(j);
                  } else if (PV->hasPtyArr(EnvMap, j)) {
                    ArrInds.insert(j);
                  }
                }
                std::string BVar = "Invalid";
                unsigned bidx = 0;
                std::string BVarN = "";
                if ((!ArrInds.empty() || !NtArrInds.empty()) && PV->hasBoundsKey()) {
                  BoundsKey PrmBKey = PV->getBoundsKey();
                  ABounds *RB = ABInfo.getBounds(PrmBKey);
                  if (RB != nullptr) {
                    BoundsKey RBKey = RB->getLengthKey();
                    auto *RBVar = ABInfo.getProgramVar(RBKey);
                    if (RBVar != nullptr) {
                      auto RBVarName = RBVar->getVarName();
                      if (RBVar->isNumConstant()) {
                        BVar = "CONSTANT";
                        std::stringstream g(RBVarName);
                        g >> bidx;
                      } else if (RBVar->getScope() == GlobalScope::getGlobalScope()) {
                        BVar = "GLOBAL";
                        BVarN = RBVarName;
                      } else {
                        unsigned k = 0;
                        for (auto *const TmpD : Definition->fields()) {
                          if (TmpD->getName().str() == RBVarName) {
                            bidx = k;
                            BVar = "FIELD";
                            break;
                          }
                          k++;
                        }
                      }
                    }
                  }
                }
                auto BndsTup = std::make_tuple(BVar, bidx, BVarN);
                if (!NtArrInds.empty() && !ArrInds.empty()) {
                  auto ToInNtArr = std::make_tuple(i, BaseTypeStr, Casts, NtArrInds, BndsTup);
                  Info.StNtArrPtrs[StName].insert(ToInNtArr);
                  auto ToInArr = std::make_tuple(i, BaseTypeStr, Casts, ArrInds, BndsTup);
                  Info.StArrPtrs[StName].insert(ToInArr);
                }
                else if (!NtArrInds.empty()) {
                  auto ToIn = std::make_tuple(i, BaseTypeStr, Casts, NtArrInds, BndsTup);
                  Info.StNtArrPtrs[StName].insert(ToIn);
                } else {
                  auto ToIn = std::make_tuple(i, BaseTypeStr, Casts, ArrInds, BndsTup);
                  Info.StArrPtrs[StName].insert(ToIn);
                }
              }
            }

            i++;
          }
        }
      }
    }
    return true;
  }

  bool VisitVarDecl(VarDecl *G) {
    auto &EnvMap = Info.getConstraints().getVariables();
    auto &ABInfo = Info.getABoundsInfo();
    if (G->hasGlobalStorage() &&
        isPtrOrArrayType(G->getType())) {
      std::string VName = G->getNameAsString();
      if (Info.GlobalArrPtrs.find(VName) == Info.GlobalArrPtrs.end() &&
        Info.GlobalNtArrPtrs.find(VName) == Info.GlobalNtArrPtrs.end()) {
        auto COpt = Info.getVariable(G, Context);
        std::string BaseTypeStr = G->getType().getAsString();
        std::set<unsigned> ArrInds;
        std::set<unsigned> NtArrInds;
        if (COpt.hasValue()) {
          ConstraintVariable &CV = COpt.getValue();
          std::set<std::string> Casts = isHavingCast(Info, CV);
          PVConstraint *PV = dyn_cast_or_null<PVConstraint>(&CV);
          if (PV) {
            ArrInds.clear();
            NtArrInds.clear();
            for (unsigned j = 0; j < PV->getCvars().size(); j++) {
              if (PV->hasPtyNtArr(EnvMap, j)) {
                NtArrInds.insert(j);
              } else if (PV->hasPtyArr(EnvMap, j)) {
                ArrInds.insert(j);
              }
            }
            std::string BVar = "Invalid";
            unsigned bidx = 0;
            std::string BVarN = "";
            if ((!ArrInds.empty() || !NtArrInds.empty()) && PV->hasBoundsKey()) {
              BoundsKey PrmBKey = PV->getBoundsKey();
              ABounds *RB = ABInfo.getBounds(PrmBKey);
              if (RB != nullptr) {
                BoundsKey RBKey = RB->getLengthKey();
                auto *RBVar = ABInfo.getProgramVar(RBKey);
                if (RBVar != nullptr) {
                  auto RBVarName = RBVar->getVarName();
                  if (RBVar->isNumConstant()) {
                    BVar = "CONSTANT";
                    std::stringstream g(RBVarName);
                    g >> bidx;
                  } else if (RBVar->getScope() == GlobalScope::getGlobalScope()) {
                    BVar = "GLOBAL";
                    BVarN = RBVarName;
                  }
                }
              }
            }
            auto BndsTup = std::make_tuple(BVar, bidx, BVarN);
            if (!NtArrInds.empty() && !ArrInds.empty()) {
              auto ToInNtArr = std::make_tuple(0, BaseTypeStr, Casts, NtArrInds, BndsTup);
              Info.GlobalNtArrPtrs[VName].insert(ToInNtArr);
              auto ToInArr = std::make_tuple(0, BaseTypeStr, Casts, ArrInds, BndsTup);
              Info.GlobalArrPtrs[VName].insert(ToInArr);
            }
            else if (!NtArrInds.empty()) {
              auto ToIn = std::make_tuple(0, BaseTypeStr, Casts, NtArrInds, BndsTup);
              Info.GlobalNtArrPtrs[VName].insert(ToIn);
            } else {
              auto ToIn = std::make_tuple(0, BaseTypeStr, Casts,ArrInds, BndsTup);
              Info.GlobalArrPtrs[VName].insert(ToIn);
            }

          }
        }
      }
    }
    return true;
  }

private:
  ASTContext *Context;
  ProgramInfo &Info;

  static bool isInSystemHeader(const std::string &FilePath) {
    return FilePath.rfind("/usr/", 0) == 0;
  }
};

void DeclToJsonConsumer::HandleTranslationUnit(ASTContext &C) {
  Info.enterCompilationUnit(C);
  DeclJsonVisitor DJV(&C, Info);
  TranslationUnitDecl *TUD = C.getTranslationUnitDecl();
  for (const auto &D : TUD->decls()) {
    // Dump the decl
    // llvm::outs() << "Decl: ";
    // D->dump();
    DJV.TraverseDecl(D);
  }
  Info.exitCompilationUnit();
}

static void DumpIndxes(llvm::raw_ostream &O, const std::set<unsigned> &Idx) {
  bool addC = false;
  O << "[";
  for (auto i : Idx) {
    if (addC) {
      O << ",";
    }
    O << i;
    addC = true;
  }
  O << "]";
}

static void DumpCasts(llvm::raw_ostream &O, const std::set<std::string> &Casts) {
  bool addC = false;
  O << "[";
  for (auto &C : Casts) {
    if (addC) {
      O << ",";
    }
    O << "\"" << C << "\"";
    addC = true;
  }
  O << "]";
}

static void DumpBInfo(llvm::raw_ostream &O,
                      const std::tuple<std::string, unsigned, std::string> &B) {
  O << "{";
  O << "\"Type\":\"" << std::get<0>(B) <<
    "\", \"Idx\":" << std::get<1>(B) <<
      ", \"Name\":\"" << std::get<2>(B);
  O << "\"}";
}
void DumpAnalysisResultsToJson(ProgramInfo &I, llvm::raw_ostream &O) {
  O << "{\"3CInfo\":[";

  O << "{";
  O << "\"FuncArrInfo\":[";
  bool addC = false;
  for (auto &FI : I.FnArrPtrs) {
    if (addC) {
      O << "\n,";
    }
    O << "{\"name\":\"" << std::get<0>(FI.first) << "\", \"static\":" <<
      std::get<1>(FI.first) << ", \"FileName\":\"" << std::get<2>(FI.first) << "\",";
    O << "\"ArrInfo\":[";
    bool addC1 = false;

    for (auto &AI: FI.second) {
      if (addC1) {
        O << "\n,";
      }
      O << "{\"ParamNum\":" << std::get<0>(AI)  << ", \"OrigType\":\"" <<
        std::get<1>(AI) << "\", \"CastedTypes\":";
      DumpCasts(O, std::get<2>(AI)); 
      O <<  ", \"ArrPtrsIdx\":";
      DumpIndxes(O, std::get<3>(AI));
      O << ", \"BoundsInfo\":";
      DumpBInfo(O, std::get<4>(AI));
      O << "}";
      addC1 = true;
    }
    O << "]}";
    addC = true;
  }
  O << "]},\n";

  O << "{";
  O << "\"FuncNtArrInfo\":[";
  addC = false;
  for (auto &FI : I.FnNtArrPtrs) {
    if (addC) {
      O << "\n,";
    }
    O << "{\"name\":\"" << std::get<0>(FI.first) << "\", \"static\":" <<
      std::get<1>(FI.first) << ", \"FileName\":\"" << std::get<2>(FI.first) << "\",";
    O << "\"ArrInfo\":[";
    bool addC1 = false;

    for (auto &AI: FI.second) {
      if (addC1) {
        O << "\n,";
      }
      O << "{\"ParamNum\":" << std::get<0>(AI)  << ", \"OrigType\":\"" <<
        std::get<1>(AI) << "\", \"CastedTypes\":";
      DumpCasts(O, std::get<2>(AI)); 
      O <<  ", \"ArrPtrsIdx\":";
      DumpIndxes(O, std::get<3>(AI));
      O << ", \"BoundsInfo\":";
      DumpBInfo(O, std::get<4>(AI));
      O << "}";
      addC1 = true;
    }
    O << "]}";
    addC = true;
  }
  O << "]},\n";

  O << "{";
  O << "\"StArrInfo\":[";
  addC = false;
  for (auto &FI : I.StArrPtrs) {
    if (addC) {
      O << "\n,";
    }
    O << "{\"name\":\"" << FI.first << "\",";
    O << "\"ArrInfo\":[";
    bool addC1 = false;

    for (auto &AI: FI.second) {
      if (addC1) {
        O << "\n,";
      }
      O << "{\"FieldIdx\":" << std::get<0>(AI)  << ", \"OrigType\":\"" <<
        std::get<1>(AI) << "\", \"CastedTypes\":";
      DumpCasts(O, std::get<2>(AI)); 
      O <<  ", \"ArrPtrsIdx\":";
      DumpIndxes(O, std::get<3>(AI));
      O << ", \"BoundsInfo\":";
      DumpBInfo(O, std::get<4>(AI));
      O << "}";
      addC1 = true;
    }
    O << "]}";
    addC = true;
  }
  O << "]},\n";


  O << "{";
  O << "\"StNtArrInfo\":[";
  addC = false;
  for (auto &FI : I.StNtArrPtrs) {
    if (addC) {
      O << "\n,";
    }
    O << "{\"name\":\"" << FI.first << "\",";
    O << "\"ArrInfo\":[";
    bool addC1 = false;

    for (auto &AI: FI.second) {
      if (addC1) {
        O << "\n,";
      }
      O << "{\"FieldIdx\":" << std::get<0>(AI)  << ", \"OrigType\":\"" <<
        std::get<1>(AI) << "\", \"CastedTypes\":";
      DumpCasts(O, std::get<2>(AI)); 
      O <<  ", \"ArrPtrsIdx\":";
      DumpIndxes(O, std::get<3>(AI));
      O << ", \"BoundsInfo\":";
      DumpBInfo(O, std::get<4>(AI));
      O << "}";
      addC1 = true;
    }
    O << "]}";
    addC = true;
  }
  O << "]},\n";


  O << "{";
  O << "\"GlobalArrInfo\":[";
  addC = false;
  for (auto &FI : I.GlobalArrPtrs) {
    if (addC) {
      O << "\n,";
    }
    O << "{\"name\":\"" << FI.first << "\",";
    O << "\"ArrInfo\":[";
    bool addC1 = false;

    for (auto &AI: FI.second) {
      if (addC1) {
        O << "\n,";
      }
      O << "{\"ParamNum\":" << std::get<0>(AI)  << ", \"OrigType\":\"" <<
        std::get<1>(AI) << "\", \"CastedTypes\":";
      DumpCasts(O, std::get<2>(AI)); 
      O <<  ", \"ArrPtrsIdx\":";
      DumpIndxes(O, std::get<3>(AI));
      O << ", \"BoundsInfo\":";
      DumpBInfo(O, std::get<4>(AI));
      O << "}";
      addC1 = true;
    }
    O << "]}";
    addC = true;
  }
  O << "]},\n";


  O << "{";
  O << "\"GlobalNTArrInfo\":[";
  addC = false;
  for (auto &FI : I.GlobalNtArrPtrs) {
    if (addC) {
      O << "\n,";
    }
    O << "{\"name\":\"" << FI.first << "\",";
    O << "\"ArrInfo\":[";
    bool addC1 = false;

    for (auto &AI: FI.second) {
      if (addC1) {
        O << "\n,";
      }
      O << "{\"ParamNum\":" << std::get<0>(AI)  << ", \"OrigType\":\"" <<
        std::get<1>(AI) << "\", \"CastedTypes\":";
      DumpCasts(O, std::get<2>(AI)); 
      O <<  ", \"ArrPtrsIdx\":";
      DumpIndxes(O, std::get<3>(AI));
      O << ", \"BoundsInfo\":";
      DumpBInfo(O, std::get<4>(AI));
      O << "}";
      addC1 = true;
    }
    O << "]}";
    addC = true;
  }
  O << "]}\n";

  O << "]}";
}