#pragma once
#include "scheduling.hpp"

namespace detersl::nats {

void run_nats_worker(detersl::worker::Scheduling& scheduling);

} // namespace detersl::nats
