#include <httplib.h>
#include "scheduling.hpp"

#include <dlfcn.h>
#include <fstream>
#include "cpp/cown.h"
#include "resource.hpp"
#include "types.hpp"
#include "wasm-runner.hpp"
#include "thread-safe-queue.hpp"
#include "rust/cxx.h"
#include <chrono>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

#define TIMEOUT_MS 3000

namespace detersl::server {
    
void register_and_schedule_json(){
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
   
        if (detersl::worker::register_wasm_function(j, &error) != 0) {
            res.status = 400;
            res.set_content(std::string("Failed to register WASM function: ") + error + "\n", "text/plain");
            return;
        }

        res.status = 201;
        res.set_content("Registered function\n", "text/plain");
    });

    server.Post("/workflow/register", [&](const httplib::Request& req, httplib::Response& res) {
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
        detersl::types::Workflow workflow;
        try {
            workflow = j.get<detersl::types::Workflow>();
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(std::string("Invalid workflow definition: ") + e.what() + "\n", "text/plain");
            return;
        }

        if (detersl::worker::register_workflow(workflow, &error) != 0) {
            res.status = 400;
            res.set_content(std::string("Failed to register workflow: ") + error + "\n", "text/plain");
            return;
        }

        res.status = 201;
        res.set_content("Workflow registered.\n", "text/plain");
    });

    server.Post("/workflow/invoke", [&](const httplib::Request& req, httplib::Response& res) {
        const int delete_after_n_wf = 3;
        int wf_count = 0;

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
        detersl::types::InvokeDTO invoke;
        try {
            invoke = j.get<detersl::types::InvokeDTO>();
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(std::string("Invalid workflow invocation: ") + e.what() + "\n", "text/plain");
            return;
        }

         if (wf_count >= delete_after_n_wf) {
            wf_count = 0;
            detersl::worker::cleanup_resources();
        }

        if (!detersl::worker::invoke_workflow(invoke, &error)) {
            res.status = 400;
            res.set_content(std::string("Failed to invoke workflow: ") + error + "\n", "text/plain");
            return;
        }
        
        wf_count++;

        res.status = 202;
        res.set_content("Workflow scheduled.\n", "text/plain");
    });

    server.Get("/resource/:res_name", [&](const httplib::Request& req, httplib::Response& res) {
        std::string res_name = req.path_params.at("res_name");
        std::future<rust::Vec<uint8_t>> res_data;
        bool found = detersl::worker::get_resource(res_name, res_data);

        if (!found) {
            res.status = 404;
            res.set_content("Resource not found.\n", "text/plain");
            return;
        }
        const auto status = res_data.wait_for(std::chrono::milliseconds(TIMEOUT_MS));
        if (status == std::future_status::timeout) {
            res.status = 500;
            res.set_content("Resource " + res_name + " could not be retrieved within timeout.\n", "text/plain");
            return;
        }

        rust::Vec<uint8_t> data = res_data.get();
        if(data.empty()) {
            res.status = 204;
            res.set_content("Resource " + res_name + " is empty.\n", "text/plain");
            return;
        }
        res.status = 200;
        res.set_content(reinterpret_cast<const char*>(data.data()), data.size(), "application/octet-stream");
    });

    std::cout << "Listening for WASM configs on http://0.0.0.0:6666\n";
    server.listen("0.0.0.0", 6666);
}

}
