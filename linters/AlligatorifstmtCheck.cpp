#include "AlligatorifstmtCheck.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Lex/Lexer.h"

using namespace clang::ast_matchers;

namespace clang::tidy::misc {
    void AlligatorifstmtCheck::registerMatchers(MatchFinder *Finder) {
        Finder->addMatcher(ifStmt().bind("ifStmt"), this);
    }

    void AlligatorifstmtCheck::check(const MatchFinder::MatchResult &Result) {
        const auto *IfStatement = Result.Nodes.getNodeAs<clang::IfStmt>("ifStmt");
        if (!IfStatement)
                return;

        const Expr *Condition = IfStatement->getCond();
        if (!Condition)
                return;

        const SourceManager &SM = Result.Context->getSourceManager();
        SourceRange SR = Condition->getSourceRange();
        StringRef ConditionText = Lexer::getSourceText(CharSourceRange::getTokenRange(SR), SM, Result.Context->getLangOpts());

        if (ConditionText.contains("log_level")) {
            diag(IfStatement->getBeginLoc(), "Matched an 'if' statement with log_level. Likely it shold be replaced with carglog, glog or other compatible function.")
                << FixItHint::CreateReplacement(IfStatement->getSourceRange(), "/* modify this statement */");
        }
    }
}
