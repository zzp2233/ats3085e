/*
 * This file is part of Glyphix.
 * Copyright (c) 2023-2024, Glyphix Development Team.
 */
#pragma once

#include "gx_async.h"
#include "gx_hashmap.h"
#include "gx_mutex.h"
#include "gx_string.h"

#include <gx_vector.h>
#define GX_FETCH_DEFAULT_TIMEOUT (6 * 1000)

namespace gx {
class AbstractStream;

enum NetworkType {
    NetworkTypeNone = 0,
    NetworkTypeBluetooth,
    NetworkTypeMobile,
    NetworkTypeWifi,
    NetworkTypeEthernet,
    NetworkTypeOthers = 100,
};

enum NetworkEvent { NetworkLinkUp, NetworkLinkDown };

/**
 * \brief An abstract class that implements http fetch.
 */
class HttpFetch {
public:
    enum Method { Get, Post, Put, Delete };
    enum ContentType { TextPlain, ApplicationJson, ApplicationOctetStream };

    struct Response {
        int error;
        int status;
        String content;
        Vector<std::pair<String, String> > headers;
        explicit Response(int error = 0) : error(error), status() {}
        Response(int status, const String &content) : error(), status(status), content(content) {}
    };
    struct Progress {
        virtual ~Progress() GX_DEFAULT;
        virtual void operator()(std::size_t current, std::size_t total, const uint8_t *data,
                                std::size_t size) = 0;
    };

    explicit HttpFetch(const String &url);
    virtual ~HttpFetch();

    const String &url() const { return m_url; }
    Progress *progress() const { return m_progress; }
    void setProgress(Progress *progress) { m_progress = progress; }
    virtual Response fetch(Method method, const String &body, HashMap<String, String> &headers,
                           int timeout) = 0;

private:
    String m_url;
    Progress *m_progress;
};

class NetworkDevice {
public:
    virtual ~NetworkDevice() GX_DEFAULT;

    const String &name() const { return m_name; }
    NetworkType type() const { return NetworkType(m_type); }
    virtual bool isUp() const { return true; }
    virtual bool isLinkUp() const { return true; }
    virtual HttpFetch *fetch(const String &url) = 0;
    virtual AbstractStream *createFetchStream(const String &url) = 0;

protected:
    NetworkDevice(const String &name, int type) : m_name(name), m_type(type) {}

private:
    String m_name;
    uint8_t m_type;
};

struct NetworkStatus {
    NetworkDevice *device;
    NetworkEvent event;
    NetworkStatus(NetworkDevice *device, NetworkEvent event) : device(device), event(event) {}
    operator JsValue() const;
};

class NetworkManager {
public:
    NetworkManager();
    ~NetworkManager();

    NetworkDevice *selectedDevice();
    HttpFetch *fetch(const String &url);
    AbstractStream *createFetchStream(const String &url);

    bool install(NetworkDevice *device);
    bool uninstall(const String &name);
    NetworkDevice *deivce() const;
    bool selectDevice(String name);
    void postNetworkEvent(NetworkDevice *device, NetworkEvent event);

    async::Signal<NetworkStatus> networkEvent;

    static NetworkManager *instance() { return m_instance; }

private:
    void deviceCheck();

    static NetworkManager *m_instance;
    NetworkDevice *m_selectedDevice;
    HashMap<String, NetworkDevice *> m_devices;
    Mutex m_mutex;
};

inline HttpFetch *NetworkManager::fetch(const String &url) {
    return selectedDevice() ? selectedDevice()->fetch(url) : nullptr;
}

inline AbstractStream *NetworkManager::createFetchStream(const String &url) {
    return selectedDevice() ? selectedDevice()->createFetchStream(url) : nullptr;
}

const Logger &operator<<(const Logger &, const NetworkDevice *);
} // namespace gx
