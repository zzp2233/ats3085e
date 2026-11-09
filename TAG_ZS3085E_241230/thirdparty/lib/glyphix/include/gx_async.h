/*
 * This file is part of Glyphix.
 * Copyright (c) 2023-2024, Glyphix Development Team.
 */
#pragma once

#include "gx_coreapplication.h"
#include "gx_event.h"
#include "gx_jsvm.h"
#include "gx_object.h"

namespace gx {
class Applet;

class AsyncObject : public PrimitiveObject {
    GX_OBJECT

public:
    GX_NODISCARD virtual int objectKinds(int hint) const;

protected:
    AsyncObject() GX_DEFAULT;
};

class AsyncSession : public AsyncObject {
    GX_OBJECT

public:
    /**
     * Consumes and deletes the AsyncSession.
     * @tparam T The consumer type.
     */
    template<class T> struct Consume {
        T value;
        explicit Consume(const T &value) : value(value) {}
        void operator()(AsyncSession *session) {
            value(session);
            delete session;
        }
    };
    struct Terminate {
        void operator()(AsyncSession *session) { delete session; }
    };

    GX_NODISCARD JsValue promise() const { return resolver().isPromise() ? resolver() : JsValue(); }
    GX_NODISCARD const JsValue &resolver() const { return m_resolver; }
    void setResolver(const JsValue &object) { m_resolver = object; }
    virtual bool close() { return false; }
    void terminate() { resolve(Terminate()); }
    template<class T> void resolve(const T &message) {
        CoreApp()->postEvent(new Message<T>(this, message));
    }

protected:
    AsyncSession() GX_DEFAULT;
    virtual ~AsyncSession();

private:
    template<class T> struct Message : public TaskEvent {
        T message;
        AsyncSession *request;
        Message(AsyncSession *req, const T &msg) : message(msg), request(req) {}
        virtual void exec() { message(request); }
    };

    JsValue m_resolver;
};

namespace async {
enum StatusEnums { OK = 0, Error = 1 };

class Status {
public:
    explicit Status(int status = OK) : m_status(status) {}
    GX_NODISCARD bool isOK() const { return m_status == OK; }
    GX_NODISCARD int value() const { return m_status; }

    bool operator==(int x) const { return m_status == x; }
    bool operator==(Status x) const { return m_status == x.m_status; }
    bool operator!=(int x) const { return !(*this == x); }
    bool operator!=(Status x) const { return !(*this == x); }

private:
    int m_status;
};

struct ResultHandler {
    static JsValue setup(const JsValue &object);
    static void success(const JsValue &object, const JsValue &result);
    static void fail(const JsValue &object, const String &message, int code);
    static void postJob(Event *job) { CoreApp()->postAsyncEvent(job); }
};

struct ListenHandler {
    static void callback(const JsValue &object, const JsValue &message);
    static void fail(const JsValue &object, const String &message, int code);
};

template<class T = void> struct Result {
    typedef T value_type;
    Result(Status status = Status()) : m_status(status), m_value() {}
    Result(const T &value) : m_status(Status()), m_value(value) {}
    GX_NODISCARD bool isValue() const { return m_status.isOK(); }
    GX_NODISCARD bool isError() const { return !m_status.isOK(); }
    GX_NODISCARD Status error() const { return m_status; }
    GX_NODISCARD const T &value() const { return m_value; }
    GX_NODISCARD JsValue toJsValue() const { return js_cast(value()); }

private:
    Status m_status;
    T m_value;
};

template<> struct Result<void> {
    typedef void value_type;
    Result(Status status = Status()) : m_status(status) {}
    GX_NODISCARD bool isValue() const { return m_status.isOK(); }
    GX_NODISCARD bool isError() const { return !m_status.isOK(); }
    GX_NODISCARD Status error() const { return m_status; }
    GX_NODISCARD JsValue toJsValue() const { return JsValue(); }

private:
    Status m_status;
};

template<class T, class H = ResultHandler> class ResultSession : public AsyncSession {
public:
    explicit ResultSession() {}
    virtual ~ResultSession() GX_DEFAULT;

    GX_NODISCARD T &client() { return m_client; }
    GX_NODISCARD const T &client() const { return m_client; }
    void request(const JsValue &resolver) {
        setResolver(H().setup(resolver));
        H().postJob(new Resolver(this));
    }
    GX_NODISCARD static ResultSession *session(T *client) {
        return reinterpret_cast<ResultSession *>(
            reinterpret_cast<uintptr_t>(client) -
            reinterpret_cast<uintptr_t>(&static_cast<ResultSession *>(nullptr)->m_client));
    }

private:
    template<class R> struct Return {
        R ret;
        explicit Return(const R &ret) : ret(ret) {}
        void operator()(AsyncSession *session) {
            const JsValue &resolver = session->resolver();
            if (ret.isValue())
                H().success(resolver, ret.toJsValue());
            else
                H().fail(resolver, T::errorMessage(ret.error()), ret.error().value());
        }
    };
    struct Resolver : public TaskEvent {
        ResultSession *session;
        explicit Resolver(ResultSession *request) : session(request) {}
        template<class R> void resolve(const Result<R> &ret) {
            typedef Return<Result<R> > Ret;
            session->resolve(Consume<Ret>(Ret(ret)));
        }
        virtual void exec() { resolve(session->client().resolve()); }
    };

private:
    T m_client;
};

template<class T, class H = ListenHandler> class ListenSession : public AsyncSession {
public:
    explicit ListenSession() : m_closed() {}
    virtual ~ListenSession() GX_DEFAULT;

    GX_NODISCARD T &client() { return m_client; }
    GX_NODISCARD const T &client() const { return m_client; }
    GX_NODISCARD bool isClosed() const { return m_closed; }
    virtual bool close() {
        if (m_closed)
            return false;
        m_closed = true;
        m_client.close();
        terminate();
        return true;
    }
    template<class R> void emit(const R &message) { this->resolve(Emit<R>(message)); }
    GX_NODISCARD static ListenSession *session(T *client) {
        return reinterpret_cast<ListenSession *>(
            reinterpret_cast<uintptr_t>(client) -
            reinterpret_cast<uintptr_t>(&static_cast<ListenSession *>(nullptr)->m_client));
    }

private:
    template<class R> struct Emit {
        Result<R> ret;
        explicit Emit(const R &ret) : ret(ret) {}
        void operator()(AsyncSession *session) {
            if (!static_cast<ListenSession *>(session)->isClosed()) {
                const JsValue &resolver = session->resolver();
                if (ret.isValue())
                    H().callback(resolver, ret.toJsValue());
                else
                    H().fail(resolver, T::errorMessage(ret.error()), ret.error().value());
            }
        }
    };

private:
    T m_client;
    bool m_closed;
};

class Slot;

namespace detail {
class SignalBase {
public:
    SignalBase() : m_pointer(-1) {}
    Slot *connect(const JsValue &resolver);
    void disconnect(int id);
    void disconnect(Slot *slot);

protected:
    void notify(const JsValue &value);

private:
    Vector<Slot *> m_pool;
    int m_pointer;
    friend class async::Slot;
};
} // namespace detail

class Slot : public AsyncObject {
    GX_OBJECT

public:
    GX_NODISCARD int id() const { return m_id; }
    virtual ~Slot();

private:
    // NOLINTNEXTLINE
    Slot(detail::SignalBase *signal, const JsValue &resolver, int id)
        : m_signal(signal), m_resolver(resolver), m_id(id) {}

    detail::SignalBase *m_signal;
    JsValue m_resolver;
    int m_id;
    friend class detail::SignalBase;
};

template<class T> class Signal : public detail::SignalBase {
public:
    void operator()(const T &value) { CoreApp()->postEvent(new Emit(this, value)); }

private:
    struct Emit : public TaskEvent {
        Signal *signal;
        T value;
        Emit(Signal *signal, const T &value) : signal(signal), value(value) {}
        virtual void exec() { signal->notify(js_cast(value)); }
    };
};

inline void detail::SignalBase::disconnect(Slot *slot) {
    if (slot)
        disconnect(slot->id());
}
} // namespace async
} // namespace gx
