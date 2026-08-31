#include "AlligatorparserinfoCheck.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Lex/Lexer.h"

using namespace clang::ast_matchers;

namespace clang::tidy::misc {

namespace {

bool isParserSourceFile(const SourceManager &SM, SourceLocation Loc) {
  Loc = SM.getSpellingLoc(Loc);
  if (Loc.isInvalid())
    return false;

  StringRef File = SM.getFilename(Loc);
  return File.contains("/parsers/") || File.contains("\\parsers\\");
}

bool isLogInfoLevel(const Expr *PriArg) {
  if (!PriArg)
    return false;

  PriArg = PriArg->IgnoreImpCasts();
  if (const auto *DRE = dyn_cast<DeclRefExpr>(PriArg))
    return DRE->getDecl()->getName() == "L_INFO";

  if (const auto *IL = dyn_cast<IntegerLiteral>(PriArg))
    return IL->getValue() == 4;

  return false;
}

bool logPriorityAndFormat(const CallExpr *Call, unsigned &PriIdx,
                          unsigned &FmtIdx) {
  if (!Call || !Call->getDirectCallee())
    return false;

  StringRef Name = Call->getDirectCallee()->getName();
  if (Name == "glog") {
    PriIdx = 0;
    FmtIdx = 1;
    return Call->getNumArgs() >= 2;
  }
  if (Name == "carglog" || Name == "carg_or_glog") {
    PriIdx = 1;
    FmtIdx = 2;
    return Call->getNumArgs() >= 3;
  }
  return false;
}

bool parserInfoMilestoneAllowed(const Expr *FmtArg, const SourceManager &SM,
                                const LangOptions &LO) {
  if (!FmtArg)
    return false;

  StringRef Text = Lexer::getSourceText(
      CharSourceRange::getTokenRange(FmtArg->getSourceRange()), SM, LO);

  static const char *const Allow[] = {
      "cycle start",
      "cycle done",
      "reconcile complete",
      "removed targets for pod",
      "agent error_status",
      nullptr,
  };

  for (const char *const *P = Allow; *P; ++P) {
    if (Text.contains(*P))
      return true;
  }
  return false;
}

} // namespace

void AlligatorparserinfoCheck::registerMatchers(MatchFinder *Finder) {
  Finder->addMatcher(
      callExpr(callee(functionDecl(hasAnyName("glog", "carglog", "carg_or_glog"))))
          .bind("logCall"),
      this);
}

void AlligatorparserinfoCheck::check(const MatchFinder::MatchResult &Result) {
  const auto *Call = Result.Nodes.getNodeAs<CallExpr>("logCall");
  if (!Call)
    return;

  if (!isParserSourceFile(*Result.SourceManager, Call->getBeginLoc()))
    return;

  unsigned PriIdx = 0;
  unsigned FmtIdx = 0;
  if (!logPriorityAndFormat(Call, PriIdx, FmtIdx))
    return;

  const Expr *PriArg = Call->getArg(PriIdx);
  const Expr *FmtArg = Call->getArg(FmtIdx);
  if (!isLogInfoLevel(PriArg))
    return;

  const SourceManager &SM = *Result.SourceManager;
  const LangOptions &LO = Result.Context->getLangOpts();
  if (parserInfoMilestoneAllowed(FmtArg, SM, LO))
    return;

  diag(Call->getBeginLoc(),
       "parser diagnostic log uses L_INFO; use L_DEBUG/L_TRACE for parse steps "
       "or add a milestone substring to the allowlist in "
       "AlligatorparserinfoCheck.cpp")
      << FixItHint::CreateInsertion(Call->getBeginLoc(),
                                    "/* demote to L_DEBUG or NOLINT */ ");
}

} // namespace clang::tidy::misc
