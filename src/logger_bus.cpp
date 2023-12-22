#include <server_lib/logger_bus.h>

#include <server_lib/logging_helper.h>

namespace server_lib {

#define FLUSH_PERIOD_SEC 2
#define LOGS_LIMIT_BY_THREAD 100000
#define THROTTLING_TIME_MS 1
#define WARNING_EVERY_LOGS 1000

logger_bus::logger_bus()
    : _act_index(0)
    , _execute(true)
{
    _logs.resize(2);

    _thd = std::thread([this]() {
        while (_execute.load(std::memory_order_acquire))
        {
            std::this_thread::sleep_for(std::chrono::seconds(FLUSH_PERIOD_SEC));
            try
            {
                flush();
            }
            catch (const std::exception& e)
            {
                LOG_ERROR(e.what());
            }
        }
    });
}

logger_bus::~logger_bus()
{
    if (_execute.load(std::memory_order_acquire))
    {
        _execute.store(false, std::memory_order_release);
        if (_thd.joinable())
            _thd.join();
    }
}

void logger_bus::put(server_lib::logger::log_message&& msg)
{
    msg.time = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

    auto thread_id = std::this_thread::get_id();

    _mutex.lock_shared();

    bool thread_met = _logs[_act_index].count(thread_id) == 0;

    _mutex.unlock_shared();

    if (!thread_met)
    {
        _mutex.lock();

        _logs[_act_index][thread_id].push(std::move(msg));

        _mutex.unlock();
    }
    else
        _logs[_act_index][thread_id].push(std::move(msg));

    if (_logs[_act_index][thread_id].size() >= LOGS_LIMIT_BY_THREAD)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(THROTTLING_TIME_MS));

        if (_logs[_act_index][thread_id].size() % WARNING_EVERY_LOGS == 0)
            LOG_WARN("Thread " << _logs[_act_index][thread_id].front().context.thread_info.first << " spam logs");
    }
}

void logger_bus::flush()
{
    _act_index = !_act_index;

    while (true)
    {
        std::pair<std::thread::id /*thread id*/, uint64_t /*time in ms*/> oldest_log = { std::thread::id(), 0 };

        for (const auto& thread_logs : _logs[!_act_index])
        {
            if (thread_logs.second.empty())
                continue;

            if (oldest_log.second == 0 || oldest_log.second > thread_logs.second.front().time)
                oldest_log = { thread_logs.first, thread_logs.second.front().time };
        }

        if (oldest_log.second == 0)
            break;

        server_lib::logger::instance().write(_logs[!_act_index][oldest_log.first].front());
        _logs[!_act_index][oldest_log.first].pop();
    }
}

} // namespace server_lib
