//===----------------------------------------------------------------------===//
//
// Copyright 2025 Bloomberg Finance L.P.
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// RUN: %clang_cc1 %s -std=c++26 -freflection -verify

using info = decltype(^^int);
struct S { info m = {}; };

                                 // ===========
                                 // valid_cases
                                 // ===========

namespace valid_cases {
constexpr info r1 = ^^int;
static constexpr info r2 = ^^int;

constexpr S s1;
constexpr S s2{};
constexpr S s3 = {^^int};

constexpr info *p1 = nullptr;
constexpr const info *p2 = &r1;

void fn1() { static constexpr info r = ^^int; }
void fn2() { extern info r; }
consteval info cfn1() { return ^^int; }
consteval void cfn2() { (void) static_cast<const void *>(p2); }

}  // namespace valid_cases


                           // ======================
                           // non_consteval_contexts
                           // ======================

namespace non_consteval_contexts {
info r1;  // expected-error {{consteval-only type must either be constexpr}}
info r2 {};  // expected-error {{consteval-only type must either be constexpr}}
info r3 = ^^int;
// expected-error@-1 {{consteval-only type must either be constexpr}}
info r4 = valid_cases::cfn1();
// expected-error@-1 {{consteval-only type must either be constexpr}}
unsigned sz = sizeof(^^int);  // ok

S s1;  // expected-error {{consteval-only type must either be constexpr}}
S s2{};  // expected-error {{consteval-only type must either be constexpr}}
S s3 = {^^int};
// expected-error@-1 {{consteval-only type must either be constexpr}}

const info *p1;
// expected-error@-1 {{consteval-only type must either be constexpr}}
const info *p2 = &valid_cases::r1;
// expected-error@-1 {{consteval-only type must either be constexpr}}

info fn1() { return ^^int; }
// expected-error@-1 {{expressions of consteval-only type}}

info fn2() { return valid_cases::cfn1(); }
// expected-error@-1 {{expressions of consteval-only type}}

void fn3() { (void) valid_cases::r1; }
// expected-error@-1 {{expressions of consteval-only type}}

void fn4() { (void) valid_cases::s1.m; }
// expected-error@-1 {{expressions of consteval-only type}}

void fn5() { (void) static_cast<const void *>(valid_cases::p2); }
// expected-error@-1 {{expressions of consteval-only type}}

void fn6() { (void) [:^^valid_cases::r1:]; }
// expected-error@-1 {{expressions of consteval-only type}}

void fn7() {
  (void) info{}; // expected-error {{expressions of consteval-only type}}
  (void) ^^int; // expected-error {{expressions of consteval-only type}}
  (void) new info{}; // expected-error {{expressions of consteval-only type}}
}

}  // namespace non_consteval_contexts


                            // ====================
                            // immediate_escalation
                            // ====================

namespace immediate_escalation {
void fn() {
  [] { info r1; }();
  [] { info r2 {}; }();
  [] { info r3 = ^^int; }();
  [] { info r4 = valid_cases::cfn1(); }();

  [] { S s1; }();
  [] { S s2{}; }();
  [] { S s3 = {^^int}; }();

  [] { const info *p1; }();
  [] { const info *p2 = &valid_cases::r1; }();

  (void) [] { return ^^int; }();
  (void) [] { return valid_cases::cfn1(); }();
  [] { (void) valid_cases::r1; }();
  [] { (void) valid_cases::s1.m; }();
  [] { (void) static_cast<const void *>(valid_cases::p2); }();
  [] { (void) [:^^valid_cases::r1:]; }();

  [] { (void) info{}; }();
  [] { (void) ^^int; }();
  [] { (void) new info{}; }();
}

}  // namespace immediate_escalation


                               // ===============
                               // alias_smuggling
                               // ===============

namespace alias_smuggling {
struct Base { };
struct Derived : Base {
  info k;
  consteval Derived() : Base(), k(^^int) {}
};
consteval const Base &fn1() {
  static constexpr Derived d;
  return d;
}
constexpr auto &ref = fn1();
// expected-error@-1 {{'ref' must be initialized by a constant expression}}
// expected-note@-2 {{reference into an object of consteval-only type}}

consteval void *fn2() {
  static constexpr auto v = ^^int;
  return (void *)&v;
}
constexpr const void *ptr = fn2();
// expected-error@-1 {{'ptr' must be initialized by a constant expression}}
// expected-note@-2 {{pointer into an object of consteval-only type}}

}  // namespace alias_smuggling
