#pragma once
#include "../EcsConfig.h"
#include "KdMathReflect.h"
#include <type_traits>
#include <utility>

// ---- 可変長引数の FOR_EACH (MSVC 既定モード対応) ----
#define KD_EXPAND(x) x
#define KD_CAT_(a, b) a##b
#define KD_CAT(a, b) KD_CAT_(a, b)

#define KD_ARG_N(_1,_2,_3,_4,_5,_6,_7,_8,_9,_10,_11,_12,_13,_14,_15,_16, N, ...) N
#define KD_COUNT(...) KD_EXPAND(KD_ARG_N(__VA_ARGS__, \
    16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1))

#define KD_FE_1(W, x)       W(x)
#define KD_FE_2(W, x, ...)  W(x) KD_EXPAND(KD_FE_1(W, __VA_ARGS__))
#define KD_FE_3(W, x, ...)  W(x) KD_EXPAND(KD_FE_2(W, __VA_ARGS__))
#define KD_FE_4(W, x, ...)  W(x) KD_EXPAND(KD_FE_3(W, __VA_ARGS__))
#define KD_FE_5(W, x, ...)  W(x) KD_EXPAND(KD_FE_4(W, __VA_ARGS__))
#define KD_FE_6(W, x, ...)  W(x) KD_EXPAND(KD_FE_5(W, __VA_ARGS__))
#define KD_FE_7(W, x, ...)  W(x) KD_EXPAND(KD_FE_6(W, __VA_ARGS__))
#define KD_FE_8(W, x, ...)  W(x) KD_EXPAND(KD_FE_7(W, __VA_ARGS__))
#define KD_FE_9(W, x, ...)  W(x) KD_EXPAND(KD_FE_8(W, __VA_ARGS__))
#define KD_FE_10(W, x, ...) W(x) KD_EXPAND(KD_FE_9(W, __VA_ARGS__))
#define KD_FE_11(W, x, ...) W(x) KD_EXPAND(KD_FE_10(W, __VA_ARGS__))
#define KD_FE_12(W, x, ...) W(x) KD_EXPAND(KD_FE_11(W, __VA_ARGS__))
#define KD_FE_13(W, x, ...) W(x) KD_EXPAND(KD_FE_12(W, __VA_ARGS__))
#define KD_FE_14(W, x, ...) W(x) KD_EXPAND(KD_FE_13(W, __VA_ARGS__))
#define KD_FE_15(W, x, ...) W(x) KD_EXPAND(KD_FE_14(W, __VA_ARGS__))
#define KD_FE_16(W, x, ...) W(x) KD_EXPAND(KD_FE_15(W, __VA_ARGS__))

#define KD_FE_DISPATCH(N, W, ...) KD_EXPAND(KD_CAT(KD_FE_, N)(W, __VA_ARGS__))
#define KD_FOR_EACH(W, ...) KD_FE_DISPATCH(KD_COUNT(__VA_ARGS__), W, __VA_ARGS__)

// ---- 各フィールドへの適用 ----
#define KD_REFLECT_FIELD(x) fn(#x, _v.x);

// ---- 本体: kdReflect(T&, fn) / kdReflect(const T&, fn) を生成 ----
#define KD_REFLECT(Type, ...)                                                  \
    template<class Fn> inline void kdReflect(Type& _v, Fn&& fn) {              \
        KD_FOR_EACH(KD_REFLECT_FIELD, __VA_ARGS__)                             \
    }                                                                          \
    template<class Fn> inline void kdReflect(const Type& _v, Fn&& fn) {        \
        KD_FOR_EACH(KD_REFLECT_FIELD, __VA_ARGS__)                             \
    }

// ---- タグ(フィールド無し)用 ----
#define KD_REFLECT_EMPTY(Type)                                                 \
    template<class Fn> inline void kdReflect(Type&, Fn&&) {}                   \
    template<class Fn> inline void kdReflect(const Type&, Fn&&) {}

// =============================================================================
//  HasReflect<T>: 型 T が kdReflect を持つか (ネスト構造体の再帰判定に使う)
// =============================================================================
namespace kd::reflect {

struct NullVisitor { template<class U> void operator()(const char*, U&) const {} };

template<class T, class = void>
struct HasReflect : std::false_type {};

template<class T>
struct HasReflect<T, std::void_t<
    decltype(kdReflect(std::declval<T&>(), std::declval<NullVisitor>()))
>> : std::true_type {};

template<class T> inline constexpr bool HasReflect_v = HasReflect<T>::value;

} // namespace kd::reflect
