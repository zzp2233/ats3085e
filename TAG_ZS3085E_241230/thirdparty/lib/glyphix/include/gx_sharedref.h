/*
 * This file is part of Glyphix.
 * Copyright (c) 2023-2024, Glyphix Development Team.
 */
#pragma once

#include "gx_atomic.h"
#include <utility>

namespace gx {
namespace utils {
template<class T> struct Duplicate {
    T *operator()(const T &x) { return new T(x); }
};
} // namespace utils

namespace internal {
class SharedRefBase;
}

class SharedValue {
public:
    enum SharedType { Mutable = 0, Immutable = 1 };
    explicit SharedValue(SharedType type = Mutable) : m_refcnt(type << 31) {}
    SharedValue(const SharedValue &other);
    virtual ~SharedValue();

    GX_NODISCARD bool isShared() const { return m_refcnt > 1; }
    GX_NODISCARD bool isMutable() const { return !(m_refcnt & (1 << 31)); }

protected:
    void setMutable(bool status);

private:
    // bit 31: mutable flag
    // bit [30: 0]: refcnt
    Atomic<uint32_t> m_refcnt;
    friend class internal::SharedRefBase;
};

namespace internal {
class SharedRefBase {
public:
    GX_NODISCARD bool empty() const { return m_ptr == nullptr; }
    void swap(SharedRefBase &ref);
    void reset();
    GX_NODISCARD int use_count() const GX_NOEXCEPT { // NOLINT(readability-*)
        return int(m_ptr->m_refcnt & ~(1 << 31));
    }

    operator bool() const { return !empty(); }
    bool operator==(const SharedRefBase &ref) const { return m_ptr == ref.m_ptr; }
    bool operator!=(const SharedRefBase &ref) const { return m_ptr != ref.m_ptr; }
    bool operator<(const SharedRefBase &ref) const { return m_ptr < ref.m_ptr; }
    bool operator<=(const SharedRefBase &ref) const { return m_ptr <= ref.m_ptr; }
    bool operator>(const SharedRefBase &ref) const { return m_ptr > ref.m_ptr; }
    bool operator>=(const SharedRefBase &ref) const { return m_ptr >= ref.m_ptr; }

protected:
    explicit SharedRefBase(SharedValue *ptr = nullptr);
    SharedRefBase(const SharedRefBase &other);
#ifndef GX_COPMATIBLE_CXX_98
    SharedRefBase(SharedRefBase &&ref) noexcept : m_ptr(ref.m_ptr) { ref.m_ptr = nullptr; }
#endif
    ~SharedRefBase();

    GX_NODISCARD SharedValue *get() const { return m_ptr; }
    void assign(const SharedRefBase &other);
    void assign(SharedValue *ptr) { m_ptr = ptr; }
#ifndef GX_COPMATIBLE_CXX_98
    void assign(SharedRefBase &&other) noexcept;
#endif

    void ref() GX_NOEXCEPT { m_ptr->m_refcnt++; }
    bool deref() GX_NOEXCEPT { return --m_ptr->m_refcnt & ~(1 << 31); }

private:
    SharedValue *m_ptr;
};
} // namespace internal

template<class T> class SharedRef : public internal::SharedRefBase {
public:
    typedef T type;
    typedef T *pointer;
    typedef T &reference;

    SharedRef() : SharedRefBase() {}
    explicit SharedRef(T *ptr) : SharedRefBase(static_cast<SharedValue *>(ptr)) {}
    SharedRef(const SharedRef &other) : SharedRefBase(other) {}
#ifndef GX_COPMATIBLE_CXX_98
    SharedRef(SharedRef &&ref) noexcept : SharedRefBase(std::move(ref)) {}
#endif
    pointer get() { return static_cast<pointer>(SharedRefBase::get()); }
    GX_NODISCARD pointer get() const { return static_cast<pointer>(SharedRefBase::get()); }
    bool detach() {
        if (get() && get()->isShared()) {
            SharedValue *ptr = utils::Duplicate<T>()(*get());
            deref();
            assign(ptr);
            ref();
        }
        return get();
    }
    pointer operator->() const { return get(); }
    reference operator*() const { return *operator->(); }
    SharedRef &operator=(const SharedRef &rhs) {
        assign(rhs);
        return *this;
    }
#ifndef GX_COPMATIBLE_CXX_98
    SharedRef &operator=(SharedRef &&rhs) noexcept {
        assign(static_cast<SharedRefBase &&>(rhs));
        return *this;
    }
#endif
};

// When copying, the reference count will be cleared, and the copied object must be mutable.
inline SharedValue::SharedValue(const SharedValue &) : m_refcnt() {}
} // namespace gx
