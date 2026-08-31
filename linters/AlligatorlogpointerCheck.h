//===--- AlligatorlogpointerCheck.h - clang-tidy -----------------*- C++ -*-===//
//
// Warn when diagnostic log format strings use %p below L_TRACE.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MISC_ALLIGATORLOGPOINTERCHECK_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MISC_ALLIGATORLOGPOINTERCHECK_H

#include "../ClangTidyCheck.h"

namespace clang::tidy::misc {

class AlligatorlogpointerCheck : public ClangTidyCheck {
public:
  AlligatorlogpointerCheck(StringRef Name, ClangTidyContext *Context)
      : ClangTidyCheck(Name, Context) {}
  void registerMatchers(ast_matchers::MatchFinder *Finder) override;
  void check(const ast_matchers::MatchFinder::MatchResult &Result) override;
};

} // namespace clang::tidy::misc

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MISC_ALLIGATORLOGPOINTERCHECK_H
