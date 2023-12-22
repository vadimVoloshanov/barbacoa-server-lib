#pragma once

#include <map>
#include <shared_mutex>
#include <thread>

#include <queue>

#include <server_lib/logger.h>
#include <server_lib/singleton.h>

namespace server_lib {

class logger_bus : public singleton<logger_bus>
{
    using map_logs = std::map<std::thread::id, std::queue<server_lib::logger::log_message>>;

public:
    ~logger_bus();

    void put(server_lib::logger::log_message&& msg);

protected:
    logger_bus();

    friend class singleton<logger_bus>;

private:
    void flush();

    std::vector<map_logs> _logs;

    std::atomic<std::uint16_t> _act_index;

    std::atomic<bool> _execute;
    std::thread _thd;
    std::shared_mutex _mutex;
};

} // namespace server_lib