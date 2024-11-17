#include "AlligatorfunctiondeprecatedCheck.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Lex/Lexer.h"

using namespace clang::ast_matchers;

namespace clang::tidy::misc {

    void AlligatorfunctiondeprecatedCheck::registerMatchers(MatchFinder *Finder) {
        Finder->addMatcher(
                        callExpr(callee(functionDecl(anyOf(
                                                functionDecl(hasName("strcspn")),
                                                functionDecl(hasName("printf")),
                                                functionDecl(hasName("fprintf")),
                                                functionDecl(hasName("puts")),
                                                functionDecl(hasName("selector_split_metric"))
                        )))).bind("deprecatedCall"),
                        this);
    }

    void AlligatorfunctiondeprecatedCheck::check(const MatchFinder::MatchResult &Result) {
        const auto *MatchedDecl = Result.Nodes.getNodeAs<CallExpr>("deprecatedCall");
        if (!MatchedDecl)
            return;

        const SourceManager &SourceMgr = Result.Context->getSourceManager();
        llvm::StringRef funcName = MatchedDecl->getDirectCallee()->getName();
        if (funcName == "strcspn") {
                const Expr *ArgS = MatchedDecl->getArg(0);
                const Expr *ArgFind = MatchedDecl->getArg(1);
                std::string ArgSExpr = Lexer::getSourceText(CharSourceRange::getTokenRange(ArgS->getSourceRange()), SourceMgr, LangOptions()).str();
                std::string ArgFindExpr = Lexer::getSourceText(CharSourceRange::getTokenRange(ArgFind->getSourceRange()), SourceMgr, LangOptions()).str();
                std::string suggestedFix = "strcspn_n(" + ArgSExpr + ", " + ArgFindExpr + ", <max>)";
                diag(MatchedDecl->getBeginLoc(), "Using deprecated function '%0'!; consider replacing with strcspn_n")
                    << MatchedDecl->getDirectCallee()->getName()
                    << FixItHint::CreateReplacement(MatchedDecl->getSourceRange(), suggestedFix);

                diag(MatchedDecl->getBeginLoc(), "insert '%0'", DiagnosticIDs::Note) <<  suggestedFix;
        }
        else if (funcName == "puts") {
                const Expr *ArgS = MatchedDecl->getArg(0);
                std::string ArgSExpr = Lexer::getSourceText(CharSourceRange::getTokenRange(ArgS->getSourceRange()), SourceMgr, LangOptions()).str();
                std::string suggestedFix = "glog(LOG_LEVEL, " + ArgSExpr + ")";
                diag(MatchedDecl->getBeginLoc(), "Using deprecated function '%0'!; consider replacing with glog/carglog/...")
                    << MatchedDecl->getDirectCallee()->getName()
                    << FixItHint::CreateReplacement(MatchedDecl->getSourceRange(), suggestedFix);

                diag(MatchedDecl->getBeginLoc(), "insert '%0'", DiagnosticIDs::Note) <<  suggestedFix;
        }
        else {
                diag(MatchedDecl->getBeginLoc(), "Using deprecated function '%0'! Try to replace this!")
                    << MatchedDecl->getDirectCallee()->getName();
        }
    }

}
