#pragma once

#include "workflow/registrar.hpp"
#include "workflow/scheduler.hpp"

namespace detersl::nats {

void run_nats_worker(detersl::worker::Scheduling& scheduling,
                     detersl::worker::FunctionRegistrar& function_registrar,
                     detersl::worker::WorkflowRegistrar& workflow_registrar);

} // namespace detersl::nats
