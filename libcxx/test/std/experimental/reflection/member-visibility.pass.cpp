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
// ADDITIONAL_COMPILE_FLAGS: -freflection
// ADDITIONAL_COMPILE_FLAGS: -faccess-contexts

// <experimental/reflection>
//
// [reflection]

#include <experimental/meta>


using std::meta::access_context;

struct PublicBase { int mem; };
struct ProtectedBase { int mem; };
struct PrivateBase { int mem; };

struct Access
    : PublicBase, protected ProtectedBase, private PrivateBase {
    static consteval access_context token() {
      return access_context::current();
    }

    int pub;
public:
    struct PublicCls {};
    template <typename T> void PublicTFn();
protected:
    int prot;
    struct ProtectedCls {};
    template <typename T> void ProtectedTFn();
private:
    int priv;
    struct PrivateCls {};
    template <typename T> void PrivateTFn();

    void complete_class_context() {
        // Public members.
        static_assert(is_public(^^pub));
        static_assert(!is_access_specified(^^pub));
        static_assert(is_public(^^PublicCls));
        static_assert(is_access_specified(^^PublicCls));
        static_assert(is_public(^^PublicTFn));
        static_assert(is_access_specified(^^PublicTFn));
        static_assert(is_public(bases_of(^^Access,
                                         access_context::unchecked())[0]));
        static_assert(
                !is_access_specified(bases_of(^^Access,
                                              access_context::unchecked())[0]));

        // Not public members.
        static_assert(!is_public(^^prot));
        static_assert(!is_public(^^priv));
        static_assert(!is_public(^^ProtectedCls));
        static_assert(!is_public(^^PrivateCls));
        static_assert(!is_public(^^ProtectedTFn));
        static_assert(!is_public(^^PrivateTFn));
        static_assert(!is_public(bases_of(^^Access,
                                          access_context::unchecked())[1]));
        static_assert(!is_public(bases_of(^^Access,
                                          access_context::unchecked())[2]));

        // Protected members.
        static_assert(is_protected(^^prot));
        static_assert(is_protected(^^ProtectedCls));
        static_assert(is_protected(^^ProtectedTFn));
        static_assert(is_protected(bases_of(^^Access,
                                            access_context::unchecked())[1]));

        // Not protected members.
        static_assert(!is_protected(^^pub));
        static_assert(!is_protected(^^priv));
        static_assert(!is_protected(^^PublicCls));
        static_assert(!is_protected(^^PrivateCls));
        static_assert(!is_protected(^^PublicTFn));
        static_assert(!is_protected(^^PrivateTFn));
        static_assert(!is_protected(bases_of(^^Access,
                                             access_context::unchecked())[0]));
        static_assert(!is_protected(bases_of(^^Access,
                                             access_context::unchecked())[2]));

        // Private members.
        static_assert(is_private(^^priv));
        static_assert(is_private(^^PrivateCls));
        static_assert(is_private(^^PrivateTFn));
        static_assert(is_private(bases_of(^^Access,
                                          access_context::unchecked())[2]));

        // Not private members.
        static_assert(!is_private(^^pub));
        static_assert(!is_private(^^prot));
        static_assert(!is_private(^^PublicCls));
        static_assert(!is_private(^^ProtectedCls));
        static_assert(!is_private(^^PublicTFn));
        static_assert(!is_private(^^ProtectedTFn));
        static_assert(!is_private(bases_of(^^Access,
                                           access_context::unchecked())[0]));
        static_assert(!is_private(bases_of(^^Access,
                                           access_context::unchecked())[1]));

        // Everything in this class is accessible.
        constexpr auto ctx = access_context::current();
        static_assert(is_accessible(^^pub, ctx));
        static_assert(is_accessible(^^prot, ctx));
        static_assert(is_accessible(^^priv, ctx));
        static_assert(is_accessible(^^PublicCls, ctx));
        static_assert(is_accessible(^^ProtectedCls, ctx));
        static_assert(is_accessible(^^PublicTFn, ctx));
        static_assert(is_accessible(^^ProtectedTFn, ctx));
        static_assert(is_accessible(^^PrivateCls, ctx));
        static_assert(is_accessible(bases_of(^^Access,
                                             access_context::unchecked())[0],
                                    ctx));
        static_assert(is_accessible(bases_of(^^Access,
                                             access_context::unchecked())[1],
                                    ctx));
        static_assert(is_accessible(bases_of(^^Access,
                                             access_context::unchecked())[2],
                                    ctx));
    }

    friend struct FriendClsOfAccess;
    friend consteval access_context FriendFnOfAccess();

public:
    static constexpr auto r_prot = ^^prot;
};

struct Derived : Access {
  static constexpr auto ctx = access_context::current();
  static constexpr auto obj_ctx = access_context::current().via(^^Derived);

  static_assert(is_accessible(^^Access::PublicBase::mem, ctx));
  static_assert(is_accessible(^^Access::ProtectedBase::mem, ctx));
  static_assert(is_accessible(^^::PrivateBase::mem, ctx));
  static_assert(!is_accessible(^^::PrivateBase::mem, obj_ctx));
  static_assert(!is_accessible(Access::r_prot, ctx));
  static_assert(is_accessible(Access::r_prot, obj_ctx));
};

static constexpr auto gctx = access_context::current();
static_assert(is_accessible(^^Access::pub, gctx));
static_assert(is_accessible(^^Access::PublicCls, gctx));
static_assert(is_accessible(^^Access::PublicTFn, gctx));
static_assert(is_accessible(^^Access::PublicBase::mem, gctx));

static_assert(  // Access::prot
    !is_accessible((members_of(^^Access, access_context::unchecked()) |
                      std::views::filter(std::meta::is_nonstatic_data_member) |
                      std::views::filter(std::meta::is_protected)).front(),
                   gctx));
static_assert(  // Access::priv
    !is_accessible((members_of(^^Access, access_context::unchecked()) |
                      std::views::filter(std::meta::is_nonstatic_data_member) |
                      std::views::filter(std::meta::is_private)).front(),
                   gctx));
static_assert(  // Access::ProtectedCls
    !is_accessible((members_of(^^Access, access_context::unchecked()) |
                      std::views::filter(std::meta::is_type) |
                      std::views::filter(std::meta::is_protected)).front(),
                   gctx));
static_assert(  // Access::PrivateCls
    !is_accessible((members_of(^^Access, access_context::unchecked()) |
                      std::views::filter(std::meta::is_type) |
                      std::views::filter(std::meta::is_private)).front(),
                   gctx));
static_assert(  // Access::ProtectedTFn
    !is_accessible((members_of(^^Access, access_context::unchecked()) |
                      std::views::filter(std::meta::is_template) |
                      std::views::filter(std::meta::is_protected)).front(),
                   gctx));
static_assert(  // Access::ProtectedTFn
    !is_accessible((members_of(^^Access, access_context::unchecked()) |
                      std::views::filter(std::meta::is_template) |
                      std::views::filter(std::meta::is_private)).front(),
                   gctx));
static_assert(is_accessible(bases_of(^^Access,   // PublicBase
                                     access_context::unchecked())[0], gctx));
static_assert(!is_accessible(bases_of(^^Access,  // ProtectedBase
                                      access_context::unchecked())[1], gctx));
static_assert(!is_accessible(bases_of(^^Access,  // PrivateBase
                                      access_context::unchecked())[2], gctx));

struct FriendClsOfAccess {
  static constexpr auto ctx = access_context::current();

  static_assert(is_accessible(^^Access::pub, ctx));
  static_assert(is_accessible(^^Access::prot, ctx));
  static_assert(is_accessible(^^Access::priv, ctx));
  static_assert(is_accessible(^^Access::PublicCls, ctx));
  static_assert(is_accessible(^^Access::ProtectedCls, ctx));
  static_assert(is_accessible(^^Access::PublicTFn, ctx));
  static_assert(is_accessible(^^Access::ProtectedTFn, ctx));
  static_assert(is_accessible(^^Access::PrivateCls, ctx));
  static_assert(is_accessible(^^Access::PublicBase::mem, ctx));
  static_assert(is_accessible(^^Access::ProtectedBase::mem, ctx));
  static_assert(is_accessible(^^Access::PrivateBase::mem, ctx));
  static_assert(is_accessible(bases_of(^^Access,  // PublicBase
                                       access_context::unchecked())[0],
                ctx));
  static_assert(is_accessible(bases_of(^^Access,  // ProtectedBase
                                       access_context::unchecked())[1],
                ctx));
  static_assert(is_accessible(bases_of(^^Access,  // PrivateBase
                                       access_context::unchecked())[2],
                ctx));
};

consteval access_context FriendFnOfAccess() {
  static constexpr auto ctx = access_context::current();

  static_assert(is_accessible(^^Access::pub, ctx));
  static_assert(is_accessible(^^Access::prot, ctx));
  static_assert(is_accessible(^^Access::priv, ctx));
  static_assert(is_accessible(^^Access::PublicCls, ctx));
  static_assert(is_accessible(^^Access::ProtectedCls, ctx));
  static_assert(is_accessible(^^Access::PublicTFn, ctx));
  static_assert(is_accessible(^^Access::ProtectedTFn, ctx));
  static_assert(is_accessible(^^Access::PrivateCls, ctx));
  static_assert(is_accessible(^^Access::PublicBase::mem, ctx));
  static_assert(is_accessible(^^Access::ProtectedBase::mem, ctx));
  static_assert(is_accessible(^^Access::PrivateBase::mem, ctx));
  static_assert(is_accessible(bases_of(^^Access,  // PublicBase
                                       access_context::unchecked())[0],
                ctx));
  static_assert(is_accessible(bases_of(^^Access,  // ProtctedBase
                                       access_context::unchecked())[1],
                ctx));
  static_assert(is_accessible(bases_of(^^Access,  // PrivateBase
                                       access_context::unchecked())[2],
                ctx));

  return ctx;
}

                            // =====================
                            // new_accessibility_api
                            // =====================


static_assert(access_context{}.scope == ^^::);
static_assert(access_context::current().scope == ^^::);
namespace new_accessibility_api {
static_assert(access_context::current().scope == ^^::new_accessibility_api);

void fn() {
  static_assert(access_context{}.scope == ^^::);
  static_assert(access_context::current().scope == ^^fn);
  [] {
    constexpr auto repr = access_context::current().scope;
    static_assert(is_function(repr));
    static_assert(repr != ^^fn);
  }();
}

static_assert(
    !is_accessible((members_of(^^Access, access_context::unchecked()) |
                       std::views::filter(std::meta::is_private)).front(),
                   access_context::current()));
static_assert(
    is_accessible((members_of(^^Access, access_context::unchecked()) |
                      std::views::filter(std::meta::is_private)).front(),
                  FriendFnOfAccess()));
static_assert(
    is_accessible((members_of(^^Access, access_context::unchecked()) |
                      std::views::filter(std::meta::is_private)).front(),
                  Access::token()));
}  // namespace new_accessibility_api

                              // ================
                              // nonsense_queries
                              // ================

namespace nonsense_queries {
static_assert(!is_public(std::meta::info{}));
static_assert(!is_public(^^int));
static_assert(!is_protected(std::meta::info{}));
static_assert(!is_protected(^^int));
static_assert(!is_private(std::meta::info{}));
static_assert(!is_private(^^int));
}  // namespace queries

int main() { }
