//===----------------------------------------------------------------------===//
//
// Copyright 2024 Bloomberg Finance L.P.
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// UNSUPPORTED: c++03 || c++11 || c++14 || c++17 || c++20
// ADDITIONAL_COMPILE_FLAGS: -freflection -fconsteval-blocks
// ADDITIONAL_COMPILE_FLAGS: -Wno-unneeded-internal-declaration
// ADDITIONAL_COMPILE_FLAGS: -Wno-unused-private-field

// <experimental/reflection>
//
// [reflection]

#include <experimental/meta>

#include <print>

                                 // ===========
                                 // well_formed
                                 // ===========

namespace well_formed {
struct I1;
constexpr auto r1 = define_aggregate(^^I1, {});

struct I2;
constinit auto r2 = define_aggregate(^^I2, {});

struct I3;
consteval { define_aggregate(^^I3, {}); }

struct I4;
struct S {
  consteval { define_aggregate(^^I4, {}); }

  struct I5;
  consteval { define_aggregate(^^I5, {}); }
};

template <typename>
struct TCls {
  struct I6;
  consteval { define_aggregate(^^I6, {}); }

  struct I7;
  static constexpr auto r = define_aggregate(^^I7, {});
};
static constexpr auto r3 = TCls<int>::r;

consteval auto fn1(std::meta::info r) {
  return define_aggregate(r, {});
}
struct I8;
consteval { fn1(^^I8); }

template <typename>
consteval auto fn2(std::meta::info r) {
  return define_aggregate(r, {});
}
struct I9;
consteval { fn2<int>(^^I9); }

consteval void fn3() {
  struct S;
  consteval { define_aggregate(^^S, {}); }
}

template <typename>
consteval auto fn4() {
  struct S;
  consteval { define_aggregate(^^S, {}); }
  return ^^S;
}
constexpr auto r4 = fn4<int>();

}  // namespace well_formed


                       // ==============================
                       // non_plainly_constant_evaluated
                       // ==============================

namespace non_plainly_constant_evaluated {
struct I1;
auto u1 = define_aggregate(^^I1, {});
// expected-error@-1 {{not plainly constant-evaluated}}
// expected-error@-2 {{'u1' of consteval-only type must either be constexpr}}

template <typename>
struct S1 {
  struct I1;

  void mfn1() requires((define_aggregate(^^I1, {}), false)) {}
  // expected-error@-1 {{not plainly constant-evaluated}}

  void mfn1() requires(true) {}

  struct I2;
  void mfn2() noexcept((define_aggregate(^^I2, {}), false)) {}
  // expected-error@-1 {{not plainly constant-evaluated}}
};

void trigger_instantiations() {
  S1<int> s;
  s.mfn1();
  s.mfn2();
}

template <typename Ty>
constexpr auto Completion = define_aggregate(^^Ty, {});
// expected-error@-1{{not plainly constant-evaluated}}

struct I4;
constexpr auto D = Completion<I4>;

}  // namespace non_plainly_constant_evaluated


                            // =====================
                            // scope_rule_violations
                            // =====================

namespace scope_rule_violations {
struct I1;
void fn1() {
  consteval { define_aggregate(^^I1, {}); }
  // expected-error@-1 {{function 'fn1' does not enclose both declarations}}
}

consteval auto fn2() {
  struct I;
  return ^^I;
}
consteval { define_aggregate(fn2(), {}); }
// expected-error@-1 {{function 'fn2' does not enclose both declarations}}

struct I2;
consteval {
  [] consteval {
    consteval { define_aggregate(^^I2, {}); }
  }();
}
// expected-error-re@-3 {{function '{{.*}}' does not enclose both declarations}}

struct I3;
template <typename>
struct TCls1 {
  consteval { define_aggregate(^^I3, {}); }
  // expected-error@-1 {{specialization 'TCls1<int>' does not enclose both}}

  struct I;
};
consteval { define_aggregate(^^TCls1<int>::I, {}); }
// expected-error@-1 {{specialization 'TCls1<int>' does not enclose both}}

template <std::meta::info R>
consteval auto tfn() {
  struct I;
  if constexpr (R != std::meta::info{}) {
    consteval { define_aggregate(R, {}); }
    // expected-error-re@-1 {{function 'tfn<{{.*}}>' does not enclose both}}
  }
  return ^^I;
}
consteval { tfn<tfn<std::meta::info{}>()>(); }

}  // namespace scope_rule_violations

int main() { }
