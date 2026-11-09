/*
 * This file is part of Glyphix.
 * Copyright (c) 2023-2024, Glyphix Development Team.
 */
#pragma once

#include "gx_internal.h"
#include <iterator>
#include <utility>

namespace gx {
// NOLINTBEGIN(readability-*)
namespace utils {
// clang-format off
template<class T> struct remove_reference      { typedef T type; };
template<class T> struct remove_reference<T&>  { typedef T type; };
#ifndef GX_COPMATIBLE_CXX_98
template<class T> struct remove_reference<T&&> { typedef T type; };
#endif

template<class T> struct remove_pointer                    { typedef T type; };
template<class T> struct remove_pointer<T*>                { typedef T type; };
template<class T> struct remove_pointer<T* const>          { typedef T type; };
template<class T> struct remove_pointer<T* volatile>       { typedef T type; };
template<class T> struct remove_pointer<T* const volatile> { typedef T type; };

template<class T> struct remove_cv                   { typedef T type; };
template<class T> struct remove_cv<const T>          { typedef T type; };
template<class T> struct remove_cv<volatile T>       { typedef T type; };
template<class T> struct remove_cv<const volatile T> { typedef T type; };

template<class T> struct remove_const                { typedef T type; };
template<class T> struct remove_const<const T>       { typedef T type; };

template<class T> struct remove_volatile             { typedef T type; };
template<class T> struct remove_volatile<volatile T> { typedef T type; };
// clang-format on

template<typename T> struct hash {
    std::size_t operator()(const T &v) const { return v.hash(); }
};

template<typename T> struct hash<const T> {
    std::size_t operator()(const T &v) const { return hash<T>()(v); }
};

template<> struct hash<std::size_t> {
    std::size_t operator()(std::size_t v) { return std::size_t(v * 265443576u); }
};

template<> struct hash<int> {
    std::size_t operator()(int v) { return hash<std::size_t>()(std::size_t(v)); }
};

template<typename T> struct hash<T *> {
    std::size_t operator()(T *v) { return hash<std::size_t>()(std::size_t(uintptr_t(v))); }
};

template<typename T1> struct equal {
    template<typename T2> bool operator()(const T1 &a, const T2 &b) const { return a == b; }
};

template<typename T> struct cache_helper {
    typedef typename remove_const<typename T::key_type>::type key_type;
    typedef std::size_t size_type;
    key_type key(const T &value) { return value.key(); }
    size_type size(const T &) { return 1; }
    size_type release(T &, size_type) { return 0; }
};

template<typename FT, typename ST> struct cache_helper<std::pair<FT, ST> > {
    typedef typename remove_const<typename std::pair<FT, ST>::first_type>::type key_type;
    typedef std::size_t size_type;
    key_type key(const std::pair<FT, ST> &value) { return value.first; }
    size_type size(const std::pair<FT, ST> &) { return 1; }
    size_type release(std::pair<FT, ST> &, size_type) { return 0; }
};

template<class Iter, class T, class Compare>
static Iter lower_bound(Iter first, Iter last, T &value, Compare comp) {
    typedef typename std::iterator_traits<Iter>::difference_type Distance;
    Distance len = std::distance(first, last);
    while (len > 0) {
        Distance half = len >> 1;
        Iter middle = first;
        std::advance(middle, half);
        if (comp(*middle, value)) {
            first = middle;
            ++first;
            len = len - half - 1;
        } else
            len = half;
    }
    return first;
}
} // namespace utils
// NOLINTEND(readability-*)
} // namespace gx
