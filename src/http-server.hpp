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

        int func_id = 0;
        if (detersl::worker::register_wasm_function(j, &error, &func_id) != 0) {
            res.status = 400;
            res.set_content(std::string("Failed to register WASM function: ") + error + "\n", "text/plain");
            return;
        }

        func_count++;
        res.status = 201;
        res.set_content("Registered func_id: " + std::to_string(func_id) + "\n", "text/plain");
    });

    server.Post("/workflow", [&](const httplib::Request& req, httplib::Response& res) {
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
        detersl::types::WorkflowRequest request;
        try {
            request = j.get<detersl::types::WorkflowRequest>();
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(std::string("Invalid workflow request: ") + e.what() + "\n", "text/plain");
            return;
        }

        if (!detersl::worker::schedule_workflow(request, &error)) {
            res.status = 400;
            res.set_content(std::string("Failed to schedule workflow: ") + error + "\n", "text/plain");
            return;
        }

        res.status = 202;
        res.set_content("Workflow scheduled.\n", "text/plain");
    });

    std::cout << "Listening for WASM configs on http://0.0.0.0:6666\n";
    server.listen("0.0.0.0", 6666);
}

}
