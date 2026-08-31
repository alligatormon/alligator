//===--- AlligatorparserinfoCheck.h - clang-tidy -----------------*- C++ -*-===//
//
// Warn when src/parsers/*.c uses L_INFO for per-step diagnostic logs.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MISC_ALLIGATORPARSERINFOCHECK_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MISC_ALLIGATORPARSERINFOCHECK_H

#include "../ClangTidyCheck.h"

namespace clang::tidy::misc {

class AlligatorparserinfoCheck : public ClangTidyCheck {
public:
  AlligatorparserinfoCheck(StringRef Name, ClangTidyContext *Context)
      : ClangTidyCheck(Name, Context) {}
  void registerMatchers(ast_matchers::MatchFinder *Finder) override;
  void check(const ast_matchers::MatchFinder::MatchResult &Result) override;
};

} // namespace clang::tidy::misc

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MISC_ALLIGATORPARSERINFOCHECK_H
