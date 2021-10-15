#include <server_lib/memory_info.h>
#include <chrono>
#include <mutex>

namespace server_lib {

float get_memory_used_percents_cached(size_t cache_timeout_ms)
{
    static float cached_value = 0.0f;
    static std::mutex cache_mutex;
    static std::chrono::system_clock::time_point cache_timer = {};

    const std::lock_guard<std::mutex> lock(cache_mutex);

    std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    if (now < cache_timer + std::chrono::milliseconds(cache_timeout_ms))
        return cached_value;

    cached_value = get_memory_used_percents();
    cache_timer = now;

    return cached_value;
}

#if defined(SERVER_LIB_PLATFORM_LINUX)

float get_memory_used_percents()
{
    char buff[128];
    FILE* fd = fopen("/proc/meminfo", "r");
    if (!fd)
        return 0.0f;

    unsigned long totalPhysMem, memFree, memBuffers, memCached;

    bool success = (fgets(buff, sizeof(buff), fd) && sscanf(buff, "MemTotal: %lu ", &totalPhysMem) == 1
                    && fgets(buff, sizeof(buff), fd) && sscanf(buff, "MemFree: %lu ", &memFree) == 1
                    && fgets(buff, sizeof(buff), fd)
                    && fgets(buff, sizeof(buff), fd) && sscanf(buff, "Buffers: %lu ", &memBuffers) == 1
                    && fgets(buff, sizeof(buff), fd) && sscanf(buff, "Cached: %lu ", &memCached) == 1);

    fclose(fd);

    if (!success)
        return 0.0f;

    unsigned long physMemUsed = totalPhysMem - (memFree + memCached + memBuffers);
    return (double(physMemUsed) / totalPhysMem) * 100.0f;
}

#elif defined(SERVER_LIB_PLATFORM_WINDOWS)

#include "windows.h"
#include "psapi.h"

float get_memory_used_percents()
{
    PROCESS_MEMORY_COUNTERS_EX pmc;
    GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc));

    DWORDLONG totalPhysMem = memInfo.ullTotalPhys;
    DWORDLONG physMemUsed = memInfo.ullTotalPhys - memInfo.ullAvailPhys;

    if (totalPhysMem == 0)
        return 0.0f;

    return (double(physMemUsed) / totalPhysMem) * 100.0f;
}

#endif //SERVER_LIB_PLATFORM_LINUX

} // namespace server_lib
