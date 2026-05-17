#pragma once
#include "registrar.hpp"
#include "scheduling.hpp"

namespace detersl::nats {

void run_nats_worker(detersl::worker::Scheduling& scheduling,
                     detersl::worker::FunctionRegistrar& function_registrar,
                     detersl::worker::WorkflowRegistrar& workflow_registrar);

} // namespace detersl::nats
