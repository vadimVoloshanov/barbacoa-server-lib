#include <server_lib/log_accumulator.h>

#include <server_lib/logging_helper.h>

namespace server_lib {

log_accumulator::log_accumulator()
    : _act_index(0)
    , _flush_active(false)
    , _new_set_force_flush(false)
    , _execute(true)
{
    _logs.resize(2);
}

log_accumulator::~log_accumulator()
{
    if (_execute.load(std::memory_order_acquire))
    {
        _execute.store(false, std::memory_order_release);
        if (_thd.joinable())
            _thd.join();
    }
}

void log_accumulator::init(size_t flush_period_ms, size_t limit_by_thread, size_t throttling_time_ms, size_t wait_flush,
                           size_t pre_init_logs_limit)
{
#if defined(_USE_LOG_ACCUMULATOR)
    _flush_period_ms.store(flush_period_ms);
    _limit_by_thread.store(limit_by_thread);
    _throttling_time_ms.store(throttling_time_ms);
    _wait_flush.store(wait_flush);

    if (!_thd.joinable())
    {
        size_t logs_count = 0;

        _mutex.lock_shared();

        for (const auto& thread_logs : _logs[_act_index])
            logs_count += thread_logs.second.size();

        _mutex.unlock_shared();

        if (logs_count > pre_init_logs_limit)
        {
            size_t logs_pop = logs_count - pre_init_logs_limit;

            LOG_ERROR("Before initialization, " << logs_count << " logs were made. We delete the first " << logs_pop << " logs.");

            _mutex.lock();

            for (; logs_pop > 0; --logs_pop)
            {
                auto oldest_log = get_oldest_log();
                _logs[_act_index][oldest_log.first].pop();
            }

            _mutex.unlock();
        }

        _thd = std::thread([this, flush_period_ms]() {
            while (_execute.load(std::memory_order_acquire))
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(_flush_period_ms.load(std::memory_order_relaxed)));
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

    LOG_INFO("Logger Accumulator init. Flus period ms: " << flush_period_ms << ", limit logs by thread before "
             << "throttling: " << limit_by_thread << ", throttling time in ms(for heavily spammy threads): "
             << throttling_time_ms);
#endif
}

void log_accumulator::put(logger::log_message&& msg)
{
    if (logger::instance().get_force_flush())
    {
        if (!_new_set_force_flush)
        {
            _new_set_force_flush = true;

            if (!_flush_active)
                flush();
            else
            {
                while (_flush_active)
                    std::this_thread::sleep_for(std::chrono::milliseconds(_wait_flush.load(std::memory_order_relaxed)));

                flush();
            }
        }

        while (_flush_active)
            std::this_thread::sleep_for(std::chrono::milliseconds(_wait_flush.load(std::memory_order_relaxed)));

        logger::instance().write(msg);
        return;
    }

    _new_set_force_flush = false;

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

    if (_logs[_act_index][thread_id].size() >= _limit_by_thread.load(std::memory_order_relaxed))
        std::this_thread::sleep_for(std::chrono::milliseconds(_throttling_time_ms.load(std::memory_order_relaxed)));
}

void log_accumulator::flush()
{
    if (_flush_active)
        return;

    _flush_active = true;

    _act_index = !_act_index;

    for (const auto& thread_logs : _logs[!_act_index])
    {
        if (thread_logs.second.size() >= _limit_by_thread.load(std::memory_order_relaxed))
            LOG_ERROR("Thread " << thread_logs.second.front().context.thread_info.first << " spam logs");
    }

    while (true)
    {
        auto oldest_log = get_oldest_log();

        if (oldest_log.second == 0)
            break;

        logger::instance().write(_logs[!_act_index][oldest_log.first].front());
        _logs[!_act_index][oldest_log.first].pop();
    }

    _flush_active = false;
}

std::pair<std::thread::id, uint64_t> log_accumulator::get_oldest_log()
{
    std::pair<std::thread::id /*thread id*/, uint64_t /*time in ms*/> oldest_log = { std::thread::id(), 0 };

    for (const auto& thread_logs : _logs[!_act_index])
    {
        if (thread_logs.second.empty())
            continue;

        if (oldest_log.second == 0 || oldest_log.second > thread_logs.second.front().time)
            oldest_log = { thread_logs.first, thread_logs.second.front().time };
    }

    return oldest_log;
}

} // namespace server_lib
