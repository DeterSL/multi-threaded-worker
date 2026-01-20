#include <httplib.h>
#include "scheduling.hpp"

#include <dlfcn.h>
#include <fstream>
#include "cpp-func.hpp"
#include "cpp-runner.hpp"
#include "cpp/cown.h"
#include "resource.hpp"
#include "types.hpp"
#include "wasm-runner.hpp"
#include "thread-safe-queue.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace detersl::server {
    
bool parse_and_schedule_json(const nlohmann::json& j, std::string& error)
{
  try {
    detersl::func::WasmFuncInfo f = detersl::func::WasmFuncInfo::from_json(j);
    detersl::func::WasmFunc func(f);
    detersl::worker::schedule_function(f, func);
    return true;
  } catch (const std::exception& e) {
    error = e.what();
    return false;
  }
}

void register_and_schedule_json(){
    const int delete_after_n_func = 3;
    int func_count = 0;

    httplib::Server server;
    server.new_task_queue = [] { return new httplib::ThreadPool(1); };

    server.Post("/wasm", [&](const httplib::Request& req, httplib::Response& res) {
        if (req.body.empty()) {
            res.status = 400;
            res.set_content("Missing JSON body.\n", "text/plain");
            return;
        }

        nlohmann::json j;
        try {
            j = nlohmann::json::parse(req.body);
        } catch (const nlohmann::json::parse_error& e) {
            res.status = 400;
            res.set_content(std::string("JSON parse error: ") + e.what() + "\n", "text/plain");
            return;
        }

        std::string error;
        if (func_count >= delete_after_n_func) {
            func_count = 0;
            detersl::worker::cleanup_resources();
        }

        bool scheduled = parse_and_schedule_json(j, error);
        if (scheduled) {
            func_count++;
        }

        if (!scheduled) {
            res.status = 400;
            res.set_content(std::string("Failed to schedule WASM function: ") + error + "\n", "text/plain");
            return;
        }

        res.status = 202;
        res.set_content("Scheduled.\n", "text/plain");
    });

    std::cout << "Listening for WASM configs on http://0.0.0.0:6666\n";
    server.listen("0.0.0.0", 6666);
}

}