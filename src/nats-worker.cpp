#include "nats-worker.hpp"

#include "nats_raii.hpp"
#include "scheduling.hpp"
#include "status.hpp"
#include <pal/cpu.h>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <future>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include <atomic_queue/atomic_queue.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace detersl::nats {

struct NatsConfig {
  std::string url;
  std::string subject;
  std::string stream;
  std::string durable;
  int batch_size;
  int fetch_timeout_ms;
};

std::string envOr(const char* k, const std::string& def) {
  const char* v = std::getenv(k);
  return (v && *v) ? std::string(v) : def;
}

int envOrInt(const char* k, int def) {
  const char* v = std::getenv(k);
  return (v && *v) ? std::atoi(v) : def;
}

void publish_reply(Connection& conn, const char* reply, const json& body) {
  if (!reply || !*reply) {
    return;
  }
  const std::string payload = body.dump();
  try {
    conn.publish(reply, payload.data(), payload.size());
  } catch (const std::exception& e) {
    std::cerr << "nats reply publish failed: " << e.what() << std::endl;
  }
}

json error_resp(const std::string& msg) {
  return json{{"status", "error"}, {"error", msg}};
}

struct Task {
  enum class Kind { GetResource, Invoke };
  Kind kind;
  json payload;
  detersl::fastjson::InvokeRequest invoke_request;
  std::optional<uint64_t> id;
  std::string reply;
};

void run_nats_worker(detersl::worker::Scheduling& scheduling,
                     detersl::worker::FunctionRegistrar& function_registrar,
                     detersl::worker::WorkflowRegistrar& workflow_registrar) {

  NatsConfig cfg{
      .url = envOr("NATSURL", NATS_DEFAULT_URL),
      .subject = envOr("SUBJECT", "detersl.worker"),
      .stream = envOr("STREAM", "DETERSL"),
      .durable = envOr("DURABLE", "detersl-mt-worker"),
      .batch_size = std::max(1, envOrInt("BATCH_SIZE", 500)),
      .fetch_timeout_ms = std::max(1, envOrInt("FETCH_TIMEOUT_MS", 3)),
  };

  const std::string js_invoke = cfg.subject + ".invoke";
  const std::string core_subject = envOr("CORE_SUBJECT", cfg.subject + ".core");
  const std::string core_prefix = core_subject + ".";
  const std::string core_wildcard = core_subject + ".*";
  const std::string status_subject =
      envOr("STATUS_SUBJECT", envOr("METRICS_SUBJECT", cfg.subject + ".status"));
  const std::string status_stream =
      envOr("STATUS_STREAM", envOr("METRICS_STREAM", cfg.stream + "_STATUS"));
  const int metrics_batch_max = envOrInt("METRICS_BATCH_MAX", 256);
  const int metrics_flush_ms = envOrInt("METRICS_FLUSH_MS", 15);

  std::cout << "NATS worker config: url=" << cfg.url
            << " subj=" << cfg.subject
            << " stream=" << cfg.stream
            << " durable=" << cfg.durable
            << " batch=" << cfg.batch_size
            << " fetch_timeout_ms=" << cfg.fetch_timeout_ms << std::endl;

  std::cout << "NATS worker JetStream subjects: " << js_invoke << std::endl;
  std::cout << "NATS worker core subjects: " << core_wildcard << std::endl;
  std::cout << "NATS worker status subject: " << status_subject
            << " status stream=" << status_stream
            << " batch=" << metrics_batch_max
            << " flush_ms=" << metrics_flush_ms << std::endl;

  Connection conn(cfg.url, "DeterSL Multi-Threaded Worker", 10000, 10, 1000);

  StreamManager::ensureStream(conn.jetstream(),
                              cfg.stream,
                              {js_invoke},
                              true);
  StreamManager::ensureStream(conn.jetstream(),
                              status_stream,
                              {status_subject},
                              true);

  const unsigned task_queue_capacity = static_cast<unsigned>(
      std::max(cfg.batch_size, envOrInt("INVOKE_QUEUE_CAPACITY", 100000)));
  constexpr unsigned kMetricsQueueCapacity = 50000;
  atomic_queue::AtomicQueueB2<Task> task_queue(task_queue_capacity);
  atomic_queue::AtomicQueueB2<detersl::status::InvocationStatus> metrics_queue(kMetricsQueueCapacity);

  std::cout << "NATS worker queue capacity: task=" << task_queue_capacity
            << " metrics=" << kMetricsQueueCapacity << std::endl;

  auto enqueue_task = [&task_queue](Task&& task) {
    while (!task_queue.try_push(task)) {
      std::this_thread::yield();
    }
  };

  detersl::status::set_completion_callback(
      [&](detersl::status::InvocationStatus* status) {
        while (!metrics_queue.try_push(*status)) {
          std::this_thread::yield();
        }
  });
  const int delete_after_n_wf = 1000;

  std::thread metrics_thread([&]() {

    while (true) {
      detersl::status::InvocationStatus status;

      if (!metrics_queue.try_pop(status)) {
        std::this_thread::yield();
        continue;
      }

      const std::string bytes = ((json)status).dump();
      natsStatus s = js_PublishAsync(conn.jetstream(),
                                     status_subject.c_str(),
                                     bytes.data(),
                                     static_cast<int>(bytes.size()),
                                     nullptr);
      if (s != NATS_OK) {
        std::cerr << "status js publish failed: " << natsStatus_GetText(s) << std::endl;
      }
    }
  });

  std::thread core_thread([&]() {

    natsSubscription* core_sub = nullptr;
    natsConnection* nc = conn.handle();
    if (natsConnection_SubscribeSync(&core_sub, nc, core_wildcard.c_str()) != NATS_OK) {
      std::cerr << "nats core subscribe error" << std::endl;
      return;
    }

    while (true) {
      natsMsg* msg = nullptr;
      natsStatus s = natsSubscription_NextMsg(&msg, core_sub, 100);
      if (s == NATS_TIMEOUT) {
        continue;
      }
      if (s != NATS_OK) {
        std::cerr << "nats core subscription error: " << natsStatus_GetText(s) << std::endl;
        continue;
      }

      json response;
      try {
        const char* subject = natsMsg_GetSubject(msg);
        const std::string subj = subject ? subject : "";
        if (subj.rfind(core_prefix, 0) != 0) {
          response = error_resp("unexpected subject: " + subj);
          publish_reply(conn, natsMsg_GetReply(msg), response);
          natsMsg_Destroy(msg);
          continue;
        }

        const std::string op = subj.substr(core_prefix.size());
        const char* data = natsMsg_GetData(msg);
        const int len = natsMsg_GetDataLength(msg);
        if (!data || len <= 0) {
          response = error_resp("empty message");
          publish_reply(conn, natsMsg_GetReply(msg), response);
          natsMsg_Destroy(msg);
          continue;
        }

        json parsed = nlohmann::json::parse(data, data + len);
        json payload = parsed.contains("payload") ? parsed.at("payload") : parsed;

        Task task{Task::Kind::GetResource, payload, {}, std::nullopt, {}};
        task.reply = natsMsg_GetReply(msg) ? natsMsg_GetReply(msg) : "";

        if (op == "register_wasm") {
          std::string err;
          if (function_registrar.register_wasm_function(payload, &err)) {
            response = json{{"status", "ok"}};
          } else {
            response = error_resp(err);
          }
          publish_reply(conn, natsMsg_GetReply(msg), response);
        } else if (op == "register_workflow") {
          std::string err;
          detersl::types::Workflow workflow = payload.get<detersl::types::Workflow>();
          if (workflow_registrar.register_workflow(workflow, &err)) {
            response = json{{"status", "ok"}};
          } else {
            response = error_resp(err);
          }
          publish_reply(conn, natsMsg_GetReply(msg), response);
        } else if (op == "get_resource") {
          enqueue_task(std::move(task));
        } else {
          response = error_resp("unsupported op: " + op);
          publish_reply(conn, natsMsg_GetReply(msg), response);
          natsMsg_Destroy(msg);
          continue;
        }
      } catch (const std::exception& e) {
        response = error_resp(std::string("handler error: ") + e.what());
        publish_reply(conn, natsMsg_GetReply(msg), response);
      }

      natsMsg_Destroy(msg);
    }
  });

  std::thread invoke_thread([&]() {
    try {
      Connection invoke_conn(cfg.url, "DeterSL JS Puller", 10000, 10, 1000);
      PullSubscriber invoke_sub(invoke_conn.jetstream(), js_invoke, cfg.stream, cfg.durable + "-invoke");

      while (true) {
        std::vector<Msg> js_msgs;
        try {
          js_msgs = invoke_sub.fetch(cfg.batch_size, cfg.fetch_timeout_ms);
        } catch (const std::exception& e) {
          std::cerr << "nats js fetch error: " << e.what() << std::endl;
          continue;
        }

        for (auto& msg : js_msgs) {
          try {
            jsMsgMetaData* meta = nullptr;
            if (natsMsg_GetMetaData(&meta, msg.m) != NATS_OK) {
              std::cerr << "invoke metadata error" << std::endl;
              msg.ack(invoke_sub.jsOptionsPtr());
              continue;
            }

            uint64_t seq = meta->Sequence.Stream;
            jsMsgMetaData_Destroy(meta);

            const char* data = msg.data();
            const size_t len = msg.size();
            if (!data || len == 0) {
              std::cerr << "invoke message empty" << std::endl;
              msg.ack(invoke_sub.jsOptionsPtr());
              continue;
            }

            detersl::fastjson::InvokeRequest request =
                detersl::fastjson::parse_invoke_request(data, len);
            Task task{
                Task::Kind::Invoke,
                {},
                std::move(request),
                seq,
                {}};
            enqueue_task(std::move(task));
            msg.ack(invoke_sub.jsOptionsPtr());
          } catch (const std::exception& e) {
            std::cerr << "invoke handler error: " << e.what() << std::endl;
            msg.ack(invoke_sub.jsOptionsPtr());
          }
        }
      }
    } catch (const std::exception& e) {
      std::cerr << "invoke thread setup error: " << e.what() << std::endl;
    }
  });

  int wf_count = 0;
  while (true) {
    Task task;
    if (!task_queue.try_pop(task)) {
      continue;
    }

    json response;
    try {
      switch (task.kind) {
        case Task::Kind::Invoke: {
          std::string err;

          if (wf_count >= delete_after_n_wf) {
            wf_count = 0;
            scheduling.cleanup_resources();
          }

          if (scheduling.invoke_workflow(std::move(task.invoke_request), *task.id, &err)) {
            wf_count++;
          } else {
            std::cerr << "invoke failed: " << err << std::endl;
          }
          break;
        }

        case Task::Kind::GetResource: {
          auto publish_bytes = [&conn, rep = task.reply](const void* data, size_t len) {
            try {
              conn.publish(rep.c_str(), data, len);
            } catch (const std::exception& e) {
              std::cerr << "nats reply publish failed: " << e.what() << std::endl;
            }
          };

          std::string res_name;
          if (task.payload.is_string()) {
            res_name = task.payload.get<std::string>();
          } else if (task.payload.contains("res_name")) {
            res_name = task.payload.at("res_name").get<std::string>();
          } else {
            publish_bytes(nullptr, 0);
            break;
          }

          bool found = scheduling.get_resource_async(
              res_name,
              [publish_bytes](const rust::Vec<uint8_t>& data) {
                publish_bytes(data.data(), data.size());
              });
          if (!found) {
            publish_bytes(nullptr, 0);
          }
          break;
        }
      }
    } catch (const std::exception& e) {
      response = error_resp(std::string("handler error: ") + e.what());
      publish_reply(conn, task.reply.c_str(), response);
    }
  }
}

} // namespace detersl::nats
