//=--3CInteractiveData.cpp----------------------------------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Data structure methods
//
//===----------------------------------------------------------------------===//

#include "clang/3C/3CInteractiveData.h"
#include "llvm/Support/JSON.h"

void ConstraintsInfo::clear() {
  RootWildAtomsWithReason.clear();
  AtomSourceMap.clear();
  AllWildAtoms.clear();
  TotalNonDirectWildAtoms.clear();
  ValidSourceFiles.clear();
  RCMap.clear();
  SrcWMap.clear();
}

CVars &ConstraintsInfo::getRCVars(ConstraintKey Ckey) { return RCMap[Ckey]; }

std::map<ConstraintKey, CVars> &ConstraintsInfo::getRCMap() { return RCMap; }

CVars &ConstraintsInfo::getSrcCVars(ConstraintKey Ckey) {
  return SrcWMap[Ckey];
}

CVars ConstraintsInfo::getWildAffectedCKeys(const CVars &DWKeys) {
  CVars IndirectWKeys;
  for (auto CK : DWKeys) {
    auto &TK = getSrcCVars(CK);
    IndirectWKeys.insert(TK.begin(), TK.end());
  }
  return IndirectWKeys;
}

float ConstraintsInfo::getAtomAffectedScore(const CVars &AllKeys) {
  float TS = 0.0;
  for (auto CK : AllKeys) {
    TS += (1.0 / getRCVars(CK).size());
  }
  return TS;
}

float ConstraintsInfo::getPtrAffectedScore(
    const std::set<ConstraintVariable *> CVs) {
  float TS = 0.0;
  for (auto *CV : CVs)
    TS += (1.0 / PtrRCMap[CV].size());
  return TS;
}

void ConstraintsInfo::printStats(llvm::raw_ostream &O) {
  O << "{\"WildPtrInfo\":{";
  O << "\"InDirectWildPtrNum\":" << TotalNonDirectWildAtoms.size() << ",";
  O << "\"InSrcInDirectWildPtrNum\":" << InSrcNonDirectWildAtoms.size() << ",";
  O << "\"DirectWildPtrs\":{";
  O << "\"Num\":" << AllWildAtoms.size() << ",";
  O << "\"InSrcNum\":" << InSrcWildAtoms.size() << ",";
  O << "\"Reasons\":[";

  std::map<std::string, std::set<ConstraintKey>> RsnBasedWildCKeys;
  for (auto &PtrR : RootWildAtomsWithReason) {
    if (AllWildAtoms.find(PtrR.first) != AllWildAtoms.end()) {
      RsnBasedWildCKeys[PtrR.second.getReason()].insert(PtrR.first);
    }
  }
  bool AddComma = false;
  for (auto &T : RsnBasedWildCKeys) {
    if (AddComma) {
      O << ",\n";
    }
    O << "{\"" << T.first << "\":{";
    O << "\"Num\":" << T.second.size() << ",";
    CVars TmpKeys;
    findIntersection(InSrcWildAtoms, T.second, TmpKeys);
    O << "\"InSrcNum\":" << TmpKeys.size() << ",";
    CVars InDWild, Tmp;
    InDWild = getWildAffectedCKeys(T.second);
    findIntersection(InDWild, InSrcNonDirectWildAtoms, Tmp);
    O << "\"TotalIndirect\":" << InDWild.size() << ",";
    O << "\"InSrcIndirect\":" << Tmp.size() << ",";
    O << "\"InSrcScore\":" << getAtomAffectedScore(Tmp);
    O << "}}";
    AddComma = true;
  }
  O << "]";
  O << "}";
  O << "}}";
}

void ConstraintsInfo::printRCMap(llvm::raw_ostream &O,
                Constraints &CS) {
  O << "{\"RCMap\":[";
  bool AddComma = false;
  for (auto &T : getRCMap()) {
    if (AddComma)
      O << ",\n";
    O << "{\"Key\":" << T.first << ", ";
    O << "\"Name\":\"" << CS.getVar(T.first)->getStr() << "\", ";
    PersistentSourceLoc PSL = AtomSourceMap[T.first];
    if (PSL.valid())
      O << "\"Location\":" << llvm::json::Value(PSL.toString()) << ", ";
    else
      O << "\"Location\":null, ";
    O << "\"Reasons\": [";
    bool AddComma2 = false;
    for (auto &R : T.second) {
      // If the name has `_tyarg_` and the location is 
      // invalid, then it is a type argument and we don't
      // want to count it as a root cause.
      std::string Name = CS.getVar(R)->getStr();
      RootCauseDiagnostic PtrInfo = RootWildAtomsWithReason.at(R);
      PersistentSourceLoc PSL = PtrInfo.getLocation();
      if (Name.find("_tyarg_") != std::string::npos &&
          !PSL.valid())
        continue;
      if (AddComma2)
        O << ",\n";
      O << "\"" << Name << "\"";
      AddComma2 = true;
    }
    O << "]}";
    AddComma = true;
  }
  O << "]}";
}

std::vector<PersistentSourceLoc>
  ConstraintsInfo::printRootCauseStats(llvm::raw_ostream &O,
                                       Constraints &CS) {
  std::vector<PersistentSourceLoc> PSLs;
  O << "{\"RootCauseStats\":[";
  bool AddComma = false;
  for (auto &T : AllWildAtoms) {
    if (AddComma)
      O << ",\n";
    const PersistentSourceLoc CurrPSL = printConstraintStats(O, CS, T);
    if (CurrPSL.valid() && 
        std::find(PSLs.begin(), PSLs.end(), CurrPSL) == PSLs.end())
      PSLs.push_back(CurrPSL);
    AddComma = true;
  }
  O << "]}";
  return PSLs;
}

const PersistentSourceLoc
  ConstraintsInfo::printConstraintStats(llvm::raw_ostream &O,
                                        Constraints &CS,
                                        ConstraintKey Cause) {
  O << "{\"ConstraintKey\":" << Cause << ", ";
  O << "\"Name\":\"" << CS.getVar(Cause)->getStr() << "\", ";
  RootCauseDiagnostic PtrInfo = RootWildAtomsWithReason.at(Cause);
  O << "\"Reason\":\"" << PtrInfo.getReason() << "\", ";
  O << "\"InSrc\":" << (InSrcWildAtoms.find(Cause) != InSrcWildAtoms.end())
    << ", ";
  O << "\"Location\":";
  const PersistentSourceLoc &PSL = PtrInfo.getLocation();
  if (PSL.valid())
    O << llvm::json::Value(PSL.toString());
  else
    O << "null";
  O << ", ";

  std::set<ConstraintKey> AtomsAffected = getWildAffectedCKeys({Cause});
  O << "\"AtomsAffected\":" << AtomsAffected.size() << ", ";
  O << "\"AtomsScore\":" << getAtomAffectedScore(AtomsAffected) << ", ";

  std::set<ConstraintVariable *> PtrsAffected = PtrSrcWMap[Cause];
  O << "\"PtrsAffected\":" << PtrsAffected.size() << ",";
  O << "\"PtrsScore\":" << getPtrAffectedScore(PtrsAffected) << ",";

  O << "\"SubReasons\":" << "[";
  bool AddComma = false;
  for(const ReasonLoc &Rsn : PtrInfo.additionalNotes()) {
    if (AddComma) O << ",";
    O << "{";
    O << "\"Rsn\":\"" << Rsn.Reason << "\", ";
    O << "\"Location\":";
    const PersistentSourceLoc &RPSL = Rsn.Location;
    if (RPSL.valid())
      O << llvm::json::Value(RPSL.toString());
    else
      O << "null";
    AddComma = true;
    O << "}";
  }
  O << "]}";
  
  if (PtrInfo.getReason() == MACRO_REASON)
    return PSL;
  else
    return PersistentSourceLoc();
}

int ConstraintsInfo::getNumPtrsAffected(ConstraintKey CK) {
  return PtrSrcWMap[CK].size();
}
