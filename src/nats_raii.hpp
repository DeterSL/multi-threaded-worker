// src/nats_raii.hpp
#pragma once
#include <string>
#include <stdexcept>
#include <vector>
#include <memory>
#include <chrono>

extern "C" {
#include <nats/nats.h>
}

namespace detersl::nats {

struct NatsError : public std::runtime_error {
    using std::runtime_error::runtime_error;
};

class Connection {
public:
    Connection(const std::string& url, const std::string& name, int timeout_ms = 5000,
               int max_reconnects = -1, int reconnect_wait_ms = 1000);
    ~Connection();

    natsConnection* handle() const { return nc_; }

    // Core publish (non-jetstream)
    void publish(const std::string& subject, const void* data, size_t len);

    // JetStream context
    jsCtx* jetstream();

private:
    natsConnection* nc_{nullptr};
    jsCtx* js_{nullptr};
    jsOptions jsOpts_{0};
};

struct Msg {
    natsMsg* m{nullptr};
    Msg() = default;
    explicit Msg(natsMsg* in): m(in) {}
    ~Msg() { if (m) natsMsg_Destroy(m); }
    Msg(Msg&& o) noexcept : m(o.m) { o.m = nullptr; }
    Msg& operator=(Msg&& o) noexcept {
        if (this != &o) { if (m) natsMsg_Destroy(m); m = o.m; o.m = nullptr; }
        return *this;
    }
    Msg(const Msg&) = delete;
    Msg& operator=(const Msg&) = delete;

    const char* data() const { return m ? natsMsg_GetData(m) : nullptr; }
    size_t size() const { return m ? natsMsg_GetDataLength(m) : 0; }

    // Ack/Nak (see docs)
    void ack(jsOptions* opts = nullptr) { if (m) (void) natsMsg_Ack(m, opts); }
    void ackSync(jsOptions* opts = nullptr) { if (m) (void) natsMsg_AckSync(m, opts, nullptr); }
    void nak(jsOptions* opts = nullptr) { if (m) (void) natsMsg_Nak(m, opts); }
    void nakWithDelay(int64_t delay_ms, jsOptions* opts = nullptr) { if (m) (void) natsMsg_NakWithDelay(m, delay_ms, opts); }
};

class PullSubscriber {
public:
    PullSubscriber(jsCtx* js, const std::string& subject, const std::string& stream,
                   const std::string& durable);
    ~PullSubscriber();

    // Fetch up to 'batch' messages with a timeout (ms). Returns possibly empty vector on timeout.
    std::vector<Msg> fetch(int batch, int64_t timeout_ms);

    jsOptions* jsOptionsPtr() { return &jsOpts_; }

private:
    natsSubscription* sub_{nullptr};
    jsCtx* js_{nullptr};
    jsSubOptions so_{0};
    jsOptions jsOpts_{0};
};

class StreamManager {
public:
    static void ensureStream(jsCtx* js,
                             const std::string& stream,
                             const std::vector<std::string>& subjects,
                             bool workQueuePolicy);
};

} // namespace detersl::nats
