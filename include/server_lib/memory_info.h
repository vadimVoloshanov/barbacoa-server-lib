#pragma once

#include <server_lib/platform_config.h>

namespace server_lib {

// Get total system memory used info
float get_memory_used_percents_cached(size_t cache_timeout_ms = 1000);
float get_memory_used_percents();

} // namespace server_lib
