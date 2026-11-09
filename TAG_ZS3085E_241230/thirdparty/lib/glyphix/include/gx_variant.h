/*
 * This file is part of Glyphix.
 * Copyright (c) 2023-2024, Glyphix Development Team.
 */
#pragma once

#include "gx_internal.h"
#include <new>
#include <utility>

#ifdef GX_SOLID_VARIANT
#undef GX_SOLID_VARIANT
#endif

#if defined(__GNUC__) || defined(__clang) || defined(__thumb__) || defined(__arm__)
#if !defined(__EMSCRIPTEN__)
#define GX_SOLID_VARIANT
#endif
#endif

namespace gx {
class Variant {
    enum Op {
        OpCreate,
        OpCopy,
#ifndef GX_COPMATIBLE_CXX_98
        OpMove,
#endif
        OpDestroy
    };
#ifdef _MSC_VER
    typedef const void *RT;
#else
    typedef void RT;
#endif

public:
    typedef RT (*type_t)(Op, Variant *, void *);
    //! Indicates that the Variant is an invalid value.
    struct invalid_t {}; // NOLINT

    Variant() { u.s.tag = 0; }
    explicit Variant(type_t type);
    template<typename T> explicit Variant(const T &v);
    Variant(const Variant &rhs);
#ifndef GX_COPMATIBLE_CXX_98
    Variant(Variant &&rhs) noexcept;
#endif
    ~Variant();
    GX_NODISCARD bool isNull() const { return !u.s.tag; }
    GX_NODISCARD bool isValid() const { return !is<invalid_t>(); }
    GX_NODISCARD type_t type() const;
    GX_NODISCARD bool is(type_t type) const;
    template<typename T> GX_NODISCARD bool is() const { return is(&typeId<T>); }
    bool convertTo(type_t type);
    GX_NODISCARD bool convert(type_t type) const;
    template<typename T> GX_NODISCARD bool convert() const { return convert(&typeId<T>); }
    template<typename T> GX_NODISCARD const T &get(const T &def = T()) const;
    template<typename T> GX_NODISCARD T to(const T &def = T()) const;
    template<typename T> const T &convertTo(const T &def = T());
    void assign(const Variant &rhs);
    template<typename T> void assign(const T &value);
    Variant &operator=(const Variant &rhs);
#ifndef GX_COPMATIBLE_CXX_98
    void assign(Variant &&rhs);
    template<typename T> void assign(T &&value);
    Variant &operator=(Variant &&rhs) noexcept;
#endif

    GX_NODISCARD static bool convert(type_t tagFrom, type_t tagTo);
    template<typename T> GX_NODISCARD static bool convert(type_t type);
    template<typename T> static RT typeId(Op op, Variant *, void *);

private:
    static const int LocalSize = sizeof(intptr_t) * 2;
    struct Data {
        explicit Data(type_t type) : type(type), refcnt(1) {}
        template<typename T> T *data() { return reinterpret_cast<T *>(this + 1); }
        const type_t type;
        int refcnt;
    };
    union {
        struct {
            uintptr_t tag;
#if !defined(GX_SOLID_VARIANT)
            // in some cases, the type field must be stored exclusively.
            type_t type;
#endif
            uint8_t buf[LocalSize];
        } s;
        Data *d;
    } u;
    GX_NODISCARD bool isLocal() const { return u.s.tag & 1; }
    template<typename T> GX_NODISCARD T *cache() { return (T *)&u.s.buf; }
    template<typename T> GX_NODISCARD const T *cache() const { return (T *)&u.s.buf; }
};

template<typename T> Variant::Variant(const T &v) { typeId<T>(OpCreate, this, (void *)&v); }

template<typename T> const T &Variant::get(const T &def) const {
    if (!is<T>())
        return def;
    return isLocal() ? *cache<T>() : *u.d->data<T>();
}

template<typename T> T Variant::to(const T &def) const {
    if (is<T>())
        return isLocal() ? *cache<T>() : *u.d->data<T>();
    Variant temp = *this;
    if (temp.convertTo(typeId<T>))
        return temp.isLocal() ? *temp.cache<T>() : *temp.u.d->data<T>();
    return def;
}

template<typename T> bool Variant::convert(type_t type) { return convert(type, &typeId<T>); }

template<typename T> const T &Variant::convertTo(const T &def) {
    if (!convertTo(&typeId<T>))
        *this = Variant(def);
    return isLocal() ? *cache<T>() : u.d ? *u.d->data<T>() : def;
}

template<typename T> void Variant::assign(const T &value) {
    this->~Variant();
    new (this) Variant(value);
}

template<typename T> void Variant::assign(T &&value) {
    this->~Variant();
    new (this) Variant(std::forward<T>(value));
}

template<typename T> Variant::RT Variant::typeId(Op op, Variant *v, void *args) {
    switch (op) {
    case OpCreate: {
        T *data;
        if (sizeof(T) <= LocalSize) { // NOLINT(bugprone-sizeof-expression)
#ifdef GX_SOLID_VARIANT
            GX_CRT_ASSERT((intptr_t(&typeId<T>) & 1) == 0);
#endif
#ifdef __thumb__
            v->u.s.tag = intptr_t(&typeId<T>);
#elif defined(GX_SOLID_VARIANT)
            v->u.s.tag = intptr_t(&typeId<T>) | 1;
#else
            v->u.s.tag = 1;
            v->u.s.type = &typeId<T>;
#endif
            data = v->cache<T>();
        } else {
            // NOLINTNEXTLINE(bugprone-sizeof-expression)
            void *ptr = operator new(sizeof(Data) + sizeof(T));
            v->u.d = new (ptr) Data(&typeId<T>);
            data = v->u.d->data<T>();
        }
        args ? new (data) T(*static_cast<const T *>(args)) : new (data) T();
        break;
    }
    case OpCopy:
        if (sizeof(T) <= LocalSize) // NOLINT(bugprone-sizeof-expression)
            new (args) T(*v->cache<T>());
        else
            new (args) T(*v->u.d->data<T>());
        break;
#ifndef GX_COPMATIBLE_CXX_98
    case OpMove:
        if (sizeof(T) <= LocalSize) // NOLINT(bugprone-sizeof-expression)
            new ((T *)args) T(std::move(*v->cache<T>()));
        else
            new ((T *)args) T(std::move(*v->u.d->data<T>()));
        break;
#endif
    case OpDestroy:
        if (sizeof(T) <= LocalSize) // NOLINT(bugprone-sizeof-expression)
            v->cache<T>()->~T();
        else
            v->u.d->data<T>()->~T();
        break;
    }
#ifdef _MSC_VER
    static const char avoidOptimization = 0;
    return &avoidOptimization;
#endif
}
} // namespace gx
