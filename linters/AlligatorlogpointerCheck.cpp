#include "AlligatorlogpointerCheck.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Lex/Lexer.h"

using namespace clang::ast_matchers;

namespace clang::tidy::misc {

namespace {

bool formatUsesPointer(const Expr *FormatArg, const SourceManager &SM,
                       const LangOptions &LO) {
  if (!FormatArg)
    return false;

  StringRef Text = Lexer::getSourceText(
      CharSourceRange::getTokenRange(FormatArg->getSourceRange()), SM, LO);
  return Text.contains("%p");
}

bool isPointerFormatAllowedLevel(const Expr *PriArg) {
  if (!PriArg)
    return false;

  PriArg = PriArg->IgnoreImpCasts();
  if (const auto *DRE = dyn_cast<DeclRefExpr>(PriArg)) {
    StringRef Name = DRE->getDecl()->getName();
    /* L_DEBUG is transitional; L_TRACE is the intended level for %p dumps. */
    return Name == "L_TRACE" || Name == "L_DEBUG";
  }

  if (const auto *IL = dyn_cast<IntegerLiteral>(PriArg)) {
    unsigned V = IL->getValue().getZExtValue();
    return V >= 5;
  }

  return false;
}

bool logPriorityAndFormat(const CallExpr *Call, unsigned &PriIdx,
                          unsigned &FmtIdx) {
  if (!Call || !Call->getDirectCallee())
    return false;

  StringRef Name = Call->getDirectCallee()->getName();
  if (Name == "glog" || Name == "cslog") {
    PriIdx = 0;
    FmtIdx = 1;
    return Call->getNumArgs() >= 2;
  }
  if (Name == "carglog" || Name == "carg_or_glog" || Name == "langlog") {
    PriIdx = 1;
    FmtIdx = 2;
    return Call->getNumArgs() >= 3;
  }
  return false;
}

} // namespace

void AlligatorlogpointerCheck::registerMatchers(MatchFinder *Finder) {
  Finder->addMatcher(
      callExpr(callee(functionDecl(hasAnyName("glog", "carglog", "carg_or_glog",
                                             "langlog", "cslog"))))
          .bind("logCall"),
      this);
}

void AlligatorlogpointerCheck::check(const MatchFinder::MatchResult &Result) {
  const auto *Call = Result.Nodes.getNodeAs<CallExpr>("logCall");
  if (!Call)
    return;

  unsigned PriIdx = 0;
  unsigned FmtIdx = 0;
  if (!logPriorityAndFormat(Call, PriIdx, FmtIdx))
    return;

  const Expr *PriArg = Call->getArg(PriIdx);
  const Expr *FmtArg = Call->getArg(FmtIdx);
  if (isPointerFormatAllowedLevel(PriArg))
    return;

  const SourceManager &SM = *Result.SourceManager;
  const LangOptions &LO = Result.Context->getLangOpts();
  if (!formatUsesPointer(FmtArg, SM, LO))
    return;

  diag(Call->getBeginLoc(),
       "diagnostic log uses '%%p' at L_INFO or above; use key=value fields, "
       "demote to L_DEBUG/L_TRACE, or remove '%%p'")
      << FixItHint::CreateInsertion(Call->getBeginLoc(),
                                    "/* use L_TRACE for %%p or remove %%p */ ");
}

} // namespace clang::tidy::misc
