#include <httplib.h>
#include "scheduling.hpp"

#include <dlfcn.h>
#include <fstream>
#include "cpp/cown.h"
#include "resource.hpp"
#include "types.hpp"
#include "wasm-runner.hpp"
#include "rust/cxx.h"
#include <chrono>
#include <vector>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

#define TIMEOUT_MS 3000

namespace detersl::server {
    
void register_and_schedule_json(){
    detersl::worker::Scheduling scheduling;
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
   
        if (!scheduling.register_wasm_function(j, &error)) {
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

        if (!scheduling.register_workflow(workflow, &error)) {
            res.status = 400;
            res.set_content(std::string("Failed to register workflow: ") + error + "\n", "text/plain");
            return;
        }

        res.status = 201;
        res.set_content("Workflow registered.\n", "text/plain");
    });

    server.Post("/workflow/invoke", [&](const httplib::Request& req, httplib::Response& res) {                    
        const int delete_after_n_wf = 100;
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
        std::string request_id;
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
            scheduling.cleanup_resources();
        }

        if (!scheduling.invoke_workflow(invoke, &error, &request_id)) {
            res.status = 400;
            res.set_content(std::string("Failed to invoke workflow: ") + error + "\n", "text/plain");
            return;
        }
        
        wf_count++;

        json body = {
            {"status", "scheduled"},
            {"request_id", request_id},
        };
        res.status = 202;
        res.set_content(body.dump(), "application/json");
    });

    server.Post("/workflow/invoke_batch", [&](const httplib::Request& req, httplib::Response& res) {
        const auto exec_start = std::chrono::steady_clock::now();
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

        std::vector<detersl::types::InvokeDTO> invocations;
        try {
            if (j.is_array()) {
                invocations = j.get<std::vector<detersl::types::InvokeDTO>>();
            } else if (j.contains("invocations")) {
                invocations = j.at("invocations").get<std::vector<detersl::types::InvokeDTO>>();
            } else {
                res.status = 400;
                res.set_content("Invalid batch payload: expected array or {\"invocations\": [...]}.\n", "text/plain");
                return;
            }
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(std::string("Invalid workflow invocations: ") + e.what() + "\n", "text/plain");
            return;
        }

        if (invocations.empty()) {
            res.status = 400;
            res.set_content("Batch is empty.\n", "text/plain");
            return;
        }

        std::vector<std::string> request_ids;
        request_ids.reserve(invocations.size());

        for (size_t i = 0; i < invocations.size(); ++i) {
            std::string error;
            std::string request_id;
            if (!scheduling.invoke_workflow(invocations[i], &error, &request_id)) {
                json body = {
                    {"error", error},
                    {"failed_index", i},
                };
                res.status = 400;
                res.set_content(body.dump(), "application/json");
                return;
            }
            request_ids.push_back(request_id);
        }

        json body = {
            {"status", "scheduled"},
            {"request_ids", request_ids},
        };
        const auto exec_end = std::chrono::steady_clock::now();
        const uint64_t exec_us = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(exec_end - exec_start).count());
        std::cout << "executed " <<  request_ids.size() << " wfs in " << exec_us << " us." << std::endl;
        res.status = 202;
        res.set_content(body.dump(), "application/json");
    });

    server.Get("/workflow/status/:request_id", [&](const httplib::Request& req, httplib::Response& res) {
        const std::string request_id = req.path_params.at("request_id");
        detersl::types::WorkflowStatus status;
        if (!scheduling.get_workflow_status(request_id, &status)) {
            res.status = 404;
            res.set_content("Workflow invocation not found.\n", "text/plain");
            return;
        }

        json body = {
            {"request_id", request_id},
            {"done", status.done},
            {"failed", status.failed},
            {"latency_ms", status.latency_ms},
            {"completed_at", status.completed_at_ms},
        };
        res.status = 200;
        res.set_content(body.dump(), "application/json");
    });

    server.Get("/resource/:res_name", [&](const httplib::Request& req, httplib::Response& res) {
        std::string res_name = req.path_params.at("res_name");
        std::future<rust::Vec<uint8_t>> res_data;
        bool found = scheduling.get_resource(res_name, res_data);

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

    server.Get("/health", [&](const httplib::Request& req, httplib::Response& res) {
        res.status = 200;
        res.set_content("healthy\n", "text/plain");
    });

    std::cout << "Listening for WASM configs on http://0.0.0.0:6666\n";
    server.listen("0.0.0.0", 6666);
}

}
