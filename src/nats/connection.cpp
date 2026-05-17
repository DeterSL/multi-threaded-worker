#include "nats/connection.hpp"

#include <cstring>

namespace detersl::nats {

static void throwIf(natsStatus s, const char* what) {
    if (s != NATS_OK) {
        std::string msg = what;
        msg += ": ";
        msg += natsStatus_GetText(s);
        throw NatsError(msg);
    }
}

Connection::Connection(const std::string& url, const std::string& name, int timeout_ms,
                       int max_reconnects, int reconnect_wait_ms) {
    natsOptions* opts = nullptr;
    throwIf(natsOptions_Create(&opts), "natsOptions_Create");
    if (!url.empty()) throwIf(natsOptions_SetURL(opts, url.c_str()), "natsOptions_SetURL");
    throwIf(natsOptions_SetName(opts, name.c_str()), "natsOptions_SetName");
    throwIf(natsOptions_SetTimeout(opts, timeout_ms), "natsOptions_SetTimeout");
    throwIf(natsOptions_SetMaxReconnect(opts, max_reconnects), "natsOptions_SetMaxReconnect");
    throwIf(natsOptions_SetReconnectWait(opts, reconnect_wait_ms), "natsOptions_SetReconnectWait");
    throwIf(natsConnection_Connect(&nc_, opts), "natsConnection_Connect");
    natsOptions_Destroy(opts);

    throwIf(jsOptions_Init(&jsOpts_), "jsOptions_Init");
    throwIf(natsConnection_JetStream(&js_, nc_, &jsOpts_), "natsConnection_JetStream");
}

Connection::~Connection() {
    if (js_) jsCtx_Destroy(js_);
    if (nc_) natsConnection_Destroy(nc_);
}

void Connection::publish(const std::string& subject, const void* data, size_t len) {
    throwIf(natsConnection_Publish(nc_, subject.c_str(), static_cast<const char*>(data), (int)len),
            "natsConnection_Publish");
}

jsCtx* Connection::jetstream() { return js_; }

// ---- StreamManager ----
void StreamManager::ensureStream(jsCtx* js,
                                 const std::string& stream,
                                 const std::vector<std::string>& subjects,
                                 bool workQueuePolicy) {
    jsStreamConfig cfg;
    jsStreamInfo* si = nullptr;

    throwIf(jsStreamConfig_Init(&cfg), "jsStreamConfig_Init");
    cfg.Name = stream.c_str();

    std::vector<const char*> csubs;
    csubs.reserve(subjects.size());
    for (auto& s : subjects) csubs.push_back(s.c_str());
    cfg.Subjects = csubs.data();
    cfg.SubjectsLen = (int) csubs.size();
    cfg.Storage = js_MemoryStorage;
    cfg.Replicas = 1;

    if (workQueuePolicy) {
        // Current NATS C enum uses 'js_' prefix (not 'JS_').
        cfg.Retention = js_WorkQueuePolicy;
    }

    // Try AddStream; if exists, UpdateStream.
    natsStatus s = js_AddStream(&si, js, &cfg, /*jsPubOptions*/ nullptr, /*jsErrCode*/ nullptr);
    if (s != NATS_OK) {
        if (si) { jsStreamInfo_Destroy(si); si = nullptr; }
        s = js_UpdateStream(&si, js, &cfg, /*jsPubOptions*/ nullptr, /*jsErrCode*/ nullptr);
        if (s != NATS_OK) {
            throwIf(s, "ensureStream");
        }
    }
    jsStreamInfo_Destroy(si);
}

// ---- PullSubscriber ----
PullSubscriber::PullSubscriber(jsCtx* js, const std::string& subject,
                               const std::string& stream, const std::string& durable)
: js_(js) {
    throwIf(jsSubOptions_Init(&so_), "jsSubOptions_Init");
    so_.Stream = stream.c_str();

    throwIf(jsOptions_Init(&jsOpts_), "jsOptions_Init");

    // Durable pull subscription (or ephemeral if 'durable' empty)
    throwIf(
        js_PullSubscribe(&sub_, js_, subject.empty() ? nullptr : subject.c_str(),
                         durable.empty() ? nullptr : durable.c_str(),
                         &jsOpts_, &so_, /*jsErrCode*/ nullptr),
        "js_PullSubscribe"
    );
}

PullSubscriber::~PullSubscriber() {
    if (sub_) natsSubscription_Destroy(sub_);
}

std::vector<Msg> PullSubscriber::fetch(int batch, int64_t timeout_ms) {
    natsMsgList list;
    std::memset(&list, 0, sizeof(list));

    natsStatus s = natsSubscription_Fetch(&list, sub_, batch, timeout_ms, /*jsErrCode*/ nullptr);
    if (s == NATS_TIMEOUT) {
        return {};
    }
    if (s != NATS_OK) {
        throwIf(s, "natsSubscription_Fetch");
    }

    std::vector<Msg> out;
    out.reserve((size_t) list.Count);
    for (int i = 0; i < list.Count; ++i) {
        out.emplace_back(list.Msgs[i]);
    }
    // Prevent double free in list destructor (Msg now owns individual messages).
    list.Count = 0;
    natsMsgList_Destroy(&list);
    return out;
}

} // namespace detersl::nats
