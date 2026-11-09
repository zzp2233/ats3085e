/*
 * This file is part of Glyphix.
 * Copyright (c) 2023-2024, Glyphix Development Team.
 */
#pragma once

#include "gx_internal.h"
#include "gx_metadefs.h"
#include "gx_string.h"
#include "gx_utils.h"
#include "gx_vector.h"

namespace gx {
namespace internal {
class PointerBase;
} // namespace internal

class Event;
class Object;
class Method;
class Property;
class MetaObject;

typedef Vector<Object *> ObjectList;

class PrimitiveObject : public NonCopyable<PrimitiveObject> {
    GX_OBJECT

public:
    struct GuardRef;
    //! Object kinds hint flags. These flags are used in the return value of the objectKinds().
    enum ObjectKinds {
        NoneKind = 0, //!< No kind.
        //! The object is a root node, Object::objectKinds(int) supports the RootKind tag.
        RootKind = 1 << 0,
        //! The object is a leaf node, Object::objectKinds(int) supports the LeafKind tag.
        LeafKind = 1 << 1,
        //! The object needs to be deleted explicitly instead of automatically. Objects with this
        //! kind are not recursively deleted when the parent object is deleted.
        //! @see JsVM::newObject(PrimitiveObject *).
        ExplicitDeleteKind = 1 << 2,
        //! Mask for all tags.
        AllKinds = 0xff
    };

    PrimitiveObject();
    virtual ~PrimitiveObject();
    //! Get all object kind hints.
    GX_NODISCARD int objectKinds() const { return objectKinds(AllKinds); }
    /**
     * Gets the specified object kind hint. These hints can help the program manage objects, but
     * are not used in most situations. However, the derived class needs to implement this virtual
     * function correctly if necessary.
     * @param hint The kind mask of interest to the caller, e.g. `RootKind | LeafKind`.
     * @return Returns the kind flags requested by (but not limited to) \b hint. The implementation
     * of this base class just return NoneKind.
     */
    GX_NODISCARD virtual int objectKinds(int hint) const;
    Property property(const char *name);
    Property property(const String &name);
    Method method(const char *name);
    Method method(const String &name);
    template<class T> GX_NODISCARD bool isInstance() const;
    GX_NODISCARD bool isInstance(const char *className) const;
    GX_NODISCARD bool isInstance(const MetaObject *type) const;
    virtual void deleteLater();

private:
    GuardRef *guardRef();
    uintptr_t m_guard;
    friend class internal::PointerBase;
};

class Object : public PrimitiveObject {
    GX_OBJECT

public:
    struct ShowTree;
    enum // flags
    {
        UserObjectFlags = 1 << 0
    };
    enum FindOptions { DirectChild, RecursiveChild };

    explicit Object(Object *parent = nullptr);
    virtual ~Object();

    GX_NODISCARD int flags() const { return int(d.flags); }
    GX_NODISCARD bool isWidget() const { return d.isWidget; }
    GX_NODISCARD Object *parent() const { return d.parent; }
    GX_NODISCARD const String &objectName() const { return d.name; }
    //! Get the event filter object.
    GX_NODISCARD Object *eventFilterObject() const { return d.eventFilter; }
    GX_NODISCARD const ObjectList &children() const { return d.children; }
    GX_NODISCARD ObjectList findChildren(const String &name,
                                         FindOptions options = DirectChild) const;
    template<typename T>
    GX_NODISCARD T *findChild(const String &name, FindOptions options = DirectChild) const;
    void setFlags(int flags, bool status = true);
    void setParent(Object *parent);
    void setParent(Object *parent, int position);
    void setObjectName(const String &name) { d.name = name; }
    void setEventFilter(Object *filter) { d.eventFilter = filter; }
    bool sendEvent(Event *event);
    GX_NODISCARD virtual int objectKinds(int hint) const;
    virtual void deleteLater();
    virtual bool event(Event *event);

    GX_PROPERTY(String id, set setObjectName, get objectName)

protected:
    //! Delete all child objects.
    void clearChildren();
    virtual bool eventFilter(Object *receiver, Event *event);

private:
    struct Extra;
    Object *findChild(const String &name, FindOptions options, const MetaObject *metaObject) const;
    void findChildren(ObjectList &result, const String &name, FindOptions options) const;

    struct Private {
        Object *parent;
        ObjectList children;
        String name;
        Object *eventFilter;
        uint32_t flags : 30;
        uint32_t isWidget : 1;
        Private();
    };

    Private d;
    friend class Widget;
};

struct PrimitiveObject::GuardRef {
    int refcnt;
    PrimitiveObject *ptr;
    explicit GuardRef(PrimitiveObject *p);
    GuardRef *ref();
    void deref();
};

struct Object::ShowTree {
    const Object *object;
    explicit ShowTree(const Object *object) : object(object) {}
};

template<class T> bool PrimitiveObject::isInstance() const {
    return isInstance(&T::staticMetaObject);
}

// NOLINTNEXTLINE(readability-*)
template<typename T> GX_NODISCARD T dyn_cast(PrimitiveObject *object) { // NOLINT(readability-*)
    typedef typename utils::remove_pointer<T>::type TT;
    if (object && object->isInstance(&TT::staticMetaObject))
        return static_cast<T>(object);
    return nullptr;
}

// NOLINTNEXTLINE(readability-*)
template<typename T> GX_NODISCARD T dyn_cast(const PrimitiveObject *object) {
    typedef typename utils::remove_pointer<T>::type TT;
    if (object && object->isInstance(&TT::staticMetaObject))
        return static_cast<T>(object);
    return nullptr;
}

// NOLINTNEXTLINE(readability-*)
template<typename T> GX_NODISCARD T dyn_cast(Object *object) {
    return dyn_cast<T>(static_cast<PrimitiveObject *>(object));
}

// NOLINTNEXTLINE(readability-*)
template<typename T> GX_NODISCARD T dyn_cast(const Object *object) {
    return dyn_cast<T>(static_cast<const PrimitiveObject *>(object));
}

inline ObjectList Object::findChildren(const String &name, FindOptions options) const {
    ObjectList result;
    findChildren(result, name, options);
    return result;
}

inline void Object::setFlags(int flags, bool status) {
    if (status)
        d.flags |= flags;
    else
        d.flags &= ~flags;
}

template<typename T> T *Object::findChild(const String &name, FindOptions options) const {
    return static_cast<T *>(findChild(name, options, &T::staticMetaObject));
}

const Logger &operator<<(const Logger &, const Object *);
const Logger &operator<<(const Logger &, const Object::ShowTree &);
} // namespace gx
