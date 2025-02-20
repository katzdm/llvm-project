//===--- SemaExpand.cpp - Semantic Analysis for Expansion Statements-------===//
//
// Copyright 2024 Bloomberg Finance L.P.
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements semantic analysis for expansion statements.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/Decl.h"
#include "clang/Basic/DiagnosticSema.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Sema/EnterExpressionEvaluationContext.h"
#include "clang/Sema/Lookup.h"
#include "clang/Sema/Overload.h"
#include "clang/Sema/Sema.h"
#include "clang/Sema/Template.h"


using namespace clang;
using namespace sema;

namespace {

VarDecl *ExtractVarDecl(Stmt *S) {
  if (auto *DS = dyn_cast_or_null<DeclStmt>(S); S)
    if (Decl *D = DS->getSingleDecl(); D && !D->isInvalidDecl())
      return dyn_cast<VarDecl>(D);
  return nullptr;
}

unsigned ExtractParmVarDeclDepth(Expr *E) {
  if (auto *DRE = cast<DeclRefExpr>(E))
    if (auto *PVD = cast<NonTypeTemplateParmDecl>(DRE->getDecl()))
      return PVD->getDepth();
  return 0;
}

// Returns how many layers of templates the current scope is nested within.
unsigned ComputeTemplateEmbeddingDepth(Scope *CurScope) {
  int Depth = 0;
  while ((CurScope = CurScope->getParent())) {
    if (CurScope->isTemplateParamScope())
      ++Depth;
  }
  return Depth;
}

// Returns whether 'Range' is an iterable expression ([stmt.ranged]).
//
// If the function returns 'true', 'BeginExpr' and 'EndExpr' point to the
// (possibly value-dependent) begin and end expressions.
bool CheckIterableExpression(Sema &S, Expr *Range, Expr *&Begin, Expr *&End) {
  if (Range->getType()->isArrayType())
    return false;
  SourceLocation RangeLoc = Range->getExprLoc();

  ExprResult BeginResult;
  {
    DeclarationNameInfo DNI(&S.PP.getIdentifierTable().get("begin"), RangeLoc);

    LookupResult LR(S, DNI, Sema::LookupMemberName);
    if (auto *RD =
            dyn_cast<CXXRecordDecl>(Range->getType()->getAsCXXRecordDecl())) {
      //S.LookupQualifiedName(LR, RD);
      if (LR.isAmbiguous())
        return false;
    }

    OverloadCandidateSet CandidateSet(RangeLoc,
                                      OverloadCandidateSet::CSK_Normal);
    Sema::ForRangeStatus Status =
        S.BuildForRangeBeginEndCall(RangeLoc, RangeLoc, DNI, LR, &CandidateSet,
                                    Range, &BeginResult);
    if (Status != Sema::FRS_Success)
      return false;
    assert(!BeginResult.isInvalid());
  }

  ExprResult EndResult;
  {
    DeclarationNameInfo DNI(&S.PP.getIdentifierTable().get("end"), RangeLoc);

    LookupResult LR(S, DNI, Sema::LookupMemberName);
    if (auto *RD =
            dyn_cast<CXXRecordDecl>(Range->getType()->getAsCXXRecordDecl())) {
      S.LookupQualifiedName(LR, RD);
      if (LR.isAmbiguous())
        return false;
    }

    OverloadCandidateSet CandidateSet(RangeLoc,
                                      OverloadCandidateSet::CSK_Normal);
    Sema::ForRangeStatus Status =
        S.BuildForRangeBeginEndCall(RangeLoc, RangeLoc, DNI, LR, &CandidateSet,
                                    Range, &EndResult);
    if (Status != Sema::FRS_Success)
      return false;
    assert(!EndResult.isInvalid());
  }

  Begin = BeginResult.get();
  End = EndResult.get();
  return true;
}

bool FindIterableExpressionSize(Sema &S, Expr *BeginExpr, Expr *EndExpr,
                                Expr::EvalResult &Result) {
  if (BeginExpr->isValueDependent() || EndExpr->isValueDependent())
    return false;

  SourceLocation BeginLoc = BeginExpr->getExprLoc();

  OverloadedOperatorKind Op = OO_Minus;
  DeclarationName DNI(S.Context.DeclarationNames.getCXXOperatorName(Op));

  Expr *Args[2] = {EndExpr, BeginExpr};
  OverloadCandidateSet CandidateSet(BeginLoc,
                                    OverloadCandidateSet::CSK_Operator);
  S.AddArgumentDependentLookupCandidates(DNI, BeginLoc, Args, nullptr,
                                         CandidateSet);

  UnresolvedSet<4> Fns;
  S.AddFunctionCandidates(Fns, Args, CandidateSet);

  ExprResult ER = S.CreateOverloadedBinOp(BeginLoc, BO_Sub, Fns, EndExpr,
                                          BeginExpr);
  if (ER.isInvalid())
    return false;

  return ER.get()->EvaluateAsInt(Result, S.Context, Expr::SE_NoSideEffects,
                                 /*InConstantContext=*/true);
}

}  // close anonymous namespace

StmtResult Sema::ActOnCXXExpansionStmt(Scope *S, SourceLocation TemplateKWLoc,
                                       SourceLocation ForLoc,
                                       SourceLocation LParenLoc, Stmt *Init,
                                       Stmt *ExpansionVarStmt,
                                       SourceLocation ColonLoc, Expr *Range,
                                       SourceLocation RParenLoc,
                                       BuildForRangeKind Kind) {
  if (!Range || Kind == BFRK_Check)
    return StmtError();

  // Compute how many layers of template parameters wrap this statement.
  unsigned TemplateDepth = ComputeTemplateEmbeddingDepth(S);

  // Create a template parameter '__N'.
  IdentifierInfo *ParmName = &Context.Idents.get("__N");
  QualType ParmTy = Context.getSizeType();
  TypeSourceInfo *ParmTI = Context.getTrivialTypeSourceInfo(ParmTy, ColonLoc);

  NonTypeTemplateParmDecl *TParam =
        NonTypeTemplateParmDecl::Create(Context,
                                        Context.getTranslationUnitDecl(),
                                        ColonLoc, ColonLoc, TemplateDepth,
                                        /*Position=*/0, ParmName, ParmTy, false,
                                        ParmTI);

  // Build a 'DeclRefExpr' designating the template parameter '__N'.
  ExprResult ER = BuildDeclRefExpr(TParam, Context.getSizeType(), VK_PRValue,
                                   ColonLoc);
  if (ER.isInvalid())
    return StmtError();
  Expr *TParamRef = ER.get();

  // Build an expansion statement depending on what kind of 'Range' we have.
  if (auto *EILE = dyn_cast<CXXExpansionInitListExpr>(Range))
    return BuildCXXInitListExpansionStmt(TemplateKWLoc, ForLoc, LParenLoc, Init,
                                         ExpansionVarStmt, ColonLoc, EILE,
                                         RParenLoc, TParamRef);
  else
    return BuildCXXExprExpansionStmt(TemplateKWLoc, ForLoc, LParenLoc, Init,
                                     ExpansionVarStmt, ColonLoc, Range,
                                     RParenLoc, TParamRef);
}

StmtResult Sema::BuildCXXExprExpansionStmt(SourceLocation TemplateKWLoc,
                                           SourceLocation ForLoc,
                                           SourceLocation LParenLoc, Stmt *Init,
                                           Stmt *ExpansionVarStmt,
                                           SourceLocation ColonLoc, Expr *Range,
                                           SourceLocation RParenLoc,
                                           Expr *TParamRef) {
  if (Range->isTypeDependent())
    return BuildCXXIndeterminateExpansionStmt(TemplateKWLoc, ForLoc, LParenLoc,
                                              Init, ExpansionVarStmt, ColonLoc,
                                              Range, RParenLoc, TParamRef);

  Expr *Begin = nullptr, *End = nullptr;
  if (CheckIterableExpression(*this, Range, Begin, End))
    return BuildCXXIterableExpansionStmt(TemplateKWLoc, ForLoc, LParenLoc,
                                         Init, ExpansionVarStmt, ColonLoc,
                                         Range, RParenLoc, TParamRef, Begin,
                                         End);
  else
    return BuildCXXDestructurableExpansionStmt(TemplateKWLoc, ForLoc, LParenLoc,
                                               Init, ExpansionVarStmt, ColonLoc,
                                               Range, RParenLoc, TParamRef);
}

StmtResult
Sema::BuildCXXIndeterminateExpansionStmt(SourceLocation TemplateKWLoc,
                                         SourceLocation ForLoc,
                                         SourceLocation LParenLoc,
                                         Stmt *Init, Stmt *ExpansionVarStmt,
                                         SourceLocation ColonLoc, Expr *Range,
                                         SourceLocation RParenLoc,
                                         Expr *TParamRef) {
  return CXXIndeterminateExpansionStmt::Create(
          Context, Init, cast<DeclStmt>(ExpansionVarStmt), Range, TemplateKWLoc,
          ForLoc, LParenLoc, ColonLoc, RParenLoc, TParamRef);
}

StmtResult
Sema::BuildCXXIterableExpansionStmt(SourceLocation TemplateKWLoc,
                                    SourceLocation ForLoc,
                                    SourceLocation LParenLoc,
                                    Stmt *Init, Stmt *ExpansionVarStmt,
                                    SourceLocation ColonLoc, Expr *Range,
                                    SourceLocation RParenLoc,
                                    Expr *TParamRef, Expr *BeginExpr,
                                    Expr *EndExpr) {
  assert(!Range->isTypeDependent() &&
         "use CXXIndeterminateExpansionStmt for type-dependent expansions");
  VarDecl *ExpansionVar = ExtractVarDecl(ExpansionVarStmt);
  if (!ExpansionVar)
    return StmtError();

  if (!ExpansionVar->getInit()) {
    auto Ctx = ExpressionEvaluationContext::PotentiallyEvaluated;
    if (ExpansionVar->isConstexpr())
      Ctx = ExpressionEvaluationContext::ImmediateFunctionContext;
    EnterExpressionEvaluationContext ExprEvalCtx(*this, Ctx);

    // Build accessor for getting the expression naming the __N'th subobject.
    ExprResult Accessor = BuildCXXIterableExpansionSelectExpr(BeginExpr,
                                                              EndExpr,
                                                              TParamRef);
    if (Accessor.isInvalid())
      return StmtError();

    // Attach the accessor as the initializer for the expansion variable.
    AddInitializerToDecl(ExpansionVar, Accessor.get(), /*DirectInit=*/false);
    if (ExpansionVar->isInvalidDecl())
      return StmtError();
  }

  unsigned NumExpansions = 0;
  if (!BeginExpr->isValueDependent()) {
    assert(!EndExpr->isValueDependent());

    Expr::EvalResult ER;
    if (FindIterableExpressionSize(*this, BeginExpr, EndExpr, ER))
      NumExpansions = ER.Val.getInt().getZExtValue();
    else {
      llvm_unreachable("TODO(P2996): diagnostic");
    }
  }

  return CXXIterableExpansionStmt::Create(
          Context, Init, cast<DeclStmt>(ExpansionVarStmt), Range, NumExpansions,
          TemplateKWLoc, ForLoc, LParenLoc, ColonLoc, RParenLoc, TParamRef);
}

StmtResult
Sema::BuildCXXDestructurableExpansionStmt(SourceLocation TemplateKWLoc,
                                          SourceLocation ForLoc,
                                          SourceLocation LParenLoc,
                                          Stmt *Init, Stmt *ExpansionVarStmt,
                                          SourceLocation ColonLoc, Expr *Range,
                                          SourceLocation RParenLoc,
                                          Expr *TParamRef) {
  assert(!Range->isTypeDependent() &&
         "use CXXIndeterminateExpansionStmt for type-dependent expansions");

  VarDecl *ExpansionVar = ExtractVarDecl(ExpansionVarStmt);
  if (!ExpansionVar)
    return StmtError();

  if (!ExpansionVar->getInit()) {
    // Build accessor for getting the expression naming the __N'th subobject.
    bool Constexpr = ExpansionVar->isConstexpr();
    ExprResult Accessor = BuildCXXDestructurableExpansionSelectExpr(Range,
                                                                    nullptr,
                                                                    TParamRef,
                                                                    Constexpr);
    if (Accessor.isInvalid())
      return StmtError();

    // Attach the accessor as the initializer for the expansion variable.
    AddInitializerToDecl(ExpansionVar, Accessor.get(), /*DirectInit=*/false);
    if (ExpansionVar->isInvalidDecl())
      return StmtError();
  }
  auto *Selector =
      cast<CXXDestructurableExpansionSelectExpr>(ExpansionVar->getInit());

  unsigned NumExpansions = 0;
  if (auto *DD = Selector->getDecompositionDecl())
    NumExpansions = DD->bindings().size();

  return CXXDestructurableExpansionStmt::Create(
          Context, Init, cast<DeclStmt>(ExpansionVarStmt), Range, NumExpansions,
          TemplateKWLoc, ForLoc, LParenLoc, ColonLoc, RParenLoc, TParamRef);
}

StmtResult Sema::BuildCXXInitListExpansionStmt(SourceLocation TemplateKWLoc,
                                               SourceLocation ForLoc,
                                               SourceLocation LParenLoc,
                                               Stmt *Init,
                                               Stmt *ExpansionVarStmt,
                                               SourceLocation ColonLoc,
                                               CXXExpansionInitListExpr *Range,
                                               SourceLocation RParenLoc,
                                               Expr *TParamRef) {
  // Extract the declaration of the expansion variable.
  VarDecl *ExpansionVar = ExtractVarDecl(ExpansionVarStmt);
  if (!ExpansionVar)
    return StmtError();

  if (!ExpansionVar->hasInit()) {
    // Build accessor for getting the __N'th Expr from the expression-init-list.
    ExprResult Accessor = BuildCXXExpansionInitListSelectExpr(Range, TParamRef);
    if (Accessor.isInvalid())
      return StmtError();

    // Attach the accessor as the initializer for the expansion variable.
    AddInitializerToDecl(ExpansionVar, Accessor.get(), /*DirectInit=*/false);
    if (ExpansionVar->isInvalidDecl())
      return StmtError();
  }

  return CXXInitListExpansionStmt::Create(Context, Init,
                                          cast<DeclStmt>(ExpansionVarStmt),
                                          Range, Range->getSubExprs().size(),
                                          TemplateKWLoc, ForLoc, LParenLoc,
                                          ColonLoc, RParenLoc, TParamRef);
}

ExprResult
Sema::BuildCXXIterableExpansionSelectExpr(Expr *BeginExpr, Expr *EndExpr,
                                          Expr *Idx) {
  if (BeginExpr->isValueDependent())
    return CXXIterableExpansionSelectExpr::Create(Context, BeginExpr, EndExpr,
                                                  Idx);

  SourceLocation BeginLoc = BeginExpr->getExprLoc();

  OverloadedOperatorKind Op = OO_Plus;
  DeclarationName DNI(Context.DeclarationNames.getCXXOperatorName(Op));

  Expr *Args[2] = {BeginExpr, Idx};
  OverloadCandidateSet CandidateSet(BeginLoc,
                                    OverloadCandidateSet::CSK_Operator);
  AddArgumentDependentLookupCandidates(DNI, BeginLoc, Args, nullptr,
                                       CandidateSet);

  UnresolvedSet<4> Fns;
  AddFunctionCandidates(Fns, Args, CandidateSet);

  ExprResult ER = CreateOverloadedBinOp(BeginLoc, BO_Add, Fns, BeginExpr, Idx);
  if (ER.isInvalid())
    return ER;
  Expr *UnaryArg[1] = {ER.get()};

  Op = OO_Star;
  DNI = Context.DeclarationNames.getCXXOperatorName(Op);

  CandidateSet.clear(OverloadCandidateSet::CSK_Operator);
  AddArgumentDependentLookupCandidates(DNI, BeginLoc, UnaryArg, nullptr,
                                       CandidateSet);

  Fns.clear();
  AddFunctionCandidates(Fns, UnaryArg, CandidateSet);

  return CreateOverloadedUnaryOp(BeginLoc, UO_Deref, Fns, ER.get());
}

ExprResult
Sema::BuildCXXDestructurableExpansionSelectExpr(Expr *Range,
                                                DecompositionDecl *DD,
                                                Expr *Idx, bool Constexpr) {
  assert (!isa<CXXExpansionInitListExpr>(Range) &&
          "expansion-init-list should never have structured bindings");

  if (!DD && !Range->isValueDependent()) {
    unsigned Arity;
    if (!ComputeDecompositionExpansionArity(Range, Arity))
      return ExprError();

    SmallVector<BindingDecl *, 4> Bindings;
    for (size_t k = 0; k < Arity; ++k)
      Bindings.push_back(BindingDecl::Create(Context, CurContext,
                                             Range->getBeginLoc(),
                                             /*IdentifierInfo=*/nullptr));

    TypeSourceInfo *TSI = Context.getTrivialTypeSourceInfo(Range->getType());
    DD = DecompositionDecl::Create(Context, CurContext, Range->getBeginLoc(),
                                   Range->getBeginLoc(), Range->getType(), TSI,
                                   SC_Auto, Bindings);
    if (Constexpr)
      DD->setConstexpr(true);

    AddInitializerToDecl(DD, Range, false);
  }

  if (!DD || Idx->isValueDependent())
    return CXXDestructurableExpansionSelectExpr::Create(Context, Range, DD, Idx,
                                                        Constexpr);

  Expr::EvalResult ER;
  if (!Idx->EvaluateAsInt(ER, Context))
    return ExprError();  // TODO(P2996): Diagnostic.
  size_t I = ER.Val.getInt().getZExtValue();
  assert(I < DD->bindings().size());

  return DD->bindings()[I]->getBinding();
}

ExprResult
Sema::BuildCXXExpansionInitListSelectExpr(CXXExpansionInitListExpr *Range,
                                          Expr *Idx) {
  // Use 'CXXExpansionInitListSelectExpr' as a placeholder until tree transform.
  if (Range->containsPack() || Idx->isValueDependent())
    return CXXExpansionInitListSelectExpr::Create(Context, Range, Idx);
  ArrayRef<Expr *> SubExprs = Range->getSubExprs();

  // Evaluate the index.
  Expr::EvalResult ER;
  if (!Idx->EvaluateAsInt(ER, Context))
    return ExprError();  // TODO(P2996): Diagnostic.
  size_t I = ER.Val.getInt().getZExtValue();
  assert(I < SubExprs.size());

  return SubExprs[I];
}

StmtResult Sema::FinishCXXExpansionStmt(Stmt *Heading, Stmt *Body) {
  if (!Heading || !Body)
    return StmtError();

  CXXExpansionStmt *Expansion = cast<CXXExpansionStmt>(Heading);
  Expansion->setBody(Body);

  // Canonical location for instantiations.
  SourceLocation Loc = Expansion->getColonLoc();

  if (Expansion->hasDependentSize())
    return Expansion;

  // Return an empty statement if the range is empty.
  if (Expansion->getNumInstantiations() == 0)
     return Expansion;

  // Create a compound statement binding loop and body.
  Stmt *VarAndBody[] = {Expansion->getExpansionVarStmt(), Body};
  Stmt *CombinedBody = CompoundStmt::Create(Context, VarAndBody,
                                            FPOptionsOverride(),
                                            Expansion->getBeginLoc(),
                                            Expansion->getEndLoc());

  // Expand the body for each instantiation.
  SmallVector<Stmt *, 4> Instantiations;
  while (Instantiations.size() < Expansion->getNumInstantiations()) {
    IntegerLiteral *Idx =
        IntegerLiteral::Create(Context,
                               llvm::APSInt::getUnsigned(Instantiations.size()),
                               Context.getSizeType(), Loc);
    TemplateArgument TArgs[] = {
        { Context, llvm::APSInt(Idx->getValue(), true), Idx->getType() }
    };
    MultiLevelTemplateArgumentList MTArgList(nullptr, TArgs, true);
    MTArgList.addOuterRetainedLevels(
            ExtractParmVarDeclDepth(Expansion->getTParamRef()));
    LocalInstantiationScope LIScope(*this, /*CombineWithOuterScope=*/true);
    InstantiatingTemplate Inst(*this, Body->getBeginLoc(), Expansion, TArgs,
                               Body->getSourceRange());

    StmtResult Instantiation = SubstStmt(CombinedBody, MTArgList);
    if (Instantiation.isInvalid())
      return StmtError();
    Instantiations.push_back(Instantiation.get());
  }

  // Allocate a more permanent buffer to hold pointers to Stmts.
  Stmt **StmtStorage = new (Context) Stmt *[Instantiations.size()];
  std::memcpy(StmtStorage, Instantiations.data(),
              Instantiations.size() * sizeof(Stmt *));

  // Attach Stmt buffer to the CXXExpansionStmt, and return.
  Expansion->setInstantiations(StmtStorage);
  return Expansion;
}

ExprResult Sema::ActOnCXXExpansionInitList(SourceLocation LBraceLoc,
                                           MultiExprArg SubExprs,
                                           SourceLocation RBraceLoc) {
  return BuildCXXExpansionInitList(LBraceLoc, SubExprs, RBraceLoc);
}

ExprResult Sema::BuildCXXExpansionInitList(SourceLocation LBraceLoc,
                                           MultiExprArg SubExprs,
                                           SourceLocation RBraceLoc) {
  Expr **SubExprList = new (Context) Expr *[SubExprs.size()];
  std::memcpy(SubExprList, SubExprs.data(), SubExprs.size() * sizeof(Expr *));

  return CXXExpansionInitListExpr::Create(Context, SubExprList,
                                          SubExprs.size(), LBraceLoc,
                                          RBraceLoc);
}
