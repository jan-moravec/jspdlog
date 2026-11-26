/**
 * @file
 * @brief Factory functions that make a json_logger with a stdout, stderr, rotating file or asynchronous sink.
 *
 * Each factory creates what the logger needs and returns an ordinary json_logger. Nothing is registered with spdlog and
 * no default logger or global thread pool is used, so everything lives exactly as long as the logger and its copies.
 * make_async_logger gives its logger a worker thread of its own, an spdlog thread pool of one thread with spdlog v1 and
 * an async_sink with spdlog v2. The last copy of that logger writes what is queued and stops the thread.
 *
 * @code
 * #include <jspdlog/factories.h>
 *
 * const auto logger = jspdlog::make_stdout_logger("app");
 * logger.info({"port", 8080}, "listening");
 * @endcode
 */

#pragma once

#include <jspdlog/jspdlog.h>

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_sinks.h>

#if JSPDLOG_SPDLOG_V2
#include <spdlog/sinks/async_sink.h>
#else
#include <spdlog/async_logger.h>
#include <spdlog/details/thread_pool.h>
#endif

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace jspdlog
{

/**
 * @brief A logger writing JSON lines to standard output, through spdlog's thread-safe stdout sink.
 *
 * The sink has no colors, because a JSON line has no range to color.
 */
[[nodiscard]] inline json_logger make_stdout_logger(std::string name, const json_options &options = {})
{
    return {std::move(name), std::make_shared<spdlog::sinks::stdout_sink_mt>(), options};
}

/** @brief A logger writing JSON lines to standard error, through spdlog's thread-safe stderr sink. */
[[nodiscard]] inline json_logger make_stderr_logger(std::string name, const json_options &options = {})
{
    return {std::move(name), std::make_shared<spdlog::sinks::stderr_sink_mt>(), options};
}

/**
 * @brief A logger writing JSON lines to a file that is rotated when it would grow past max_size bytes.
 *
 * The sink is thread-safe and keeps max_files old files besides the current one, numbered from 1, the newest. It
 * creates missing directories and appends to an existing file. Like spdlog's sink it throws spdlog::spdlog_ex when the
 * file cannot be opened or max_size is 0.
 */
[[nodiscard]] inline json_logger make_rotating_file_logger(
    std::string name,
    const spdlog::filename_t &path,
    std::size_t max_size,
    std::size_t max_files,
    const json_options &options = {}
)
{
    return {
        std::move(name), std::make_shared<spdlog::sinks::rotating_file_sink_mt>(path, max_size, max_files), options
    };
}

/** @brief What a logging call does when the queue of an asynchronous logger is full. */
enum class overflow_policy
{
    /** @brief Wait for space, as spdlog does by default. No line is lost. */
    block,
    /** @brief Drop the oldest queued line to make room. */
    overrun_oldest,
    /** @brief Drop the new line. spdlog 1.12 has no such policy, and make_async_logger throws there. */
    discard_new,
};

/** @brief Implementation details. Not part of the public API. */
namespace detail
{
/** @brief The queue size of async_options, the same as spdlog's own default. */
inline constexpr std::size_t DEFAULT_QUEUE_SIZE = 8192;
} // namespace detail

/** @brief Settings of the logger made by make_async_logger. */
struct async_options
{
    /** @brief The number of lines the queue holds, at least 1. With spdlog v2 at most 250000, its limit. */
    std::size_t queue_size = detail::DEFAULT_QUEUE_SIZE;
    /** @brief What a call does when the queue is full. */
    overflow_policy overflow = overflow_policy::block;
    /**
     * @brief Report the logger's errors, from the worker thread and the calling thread, as JSON lines on standard error
     * through make_error_handler, with the keys of the logger's json_options. Otherwise spdlog's plain-text handler
     * stays.
     */
    bool json_errors = true;
};

/** @brief Implementation details. Not part of the public API. */
namespace detail
{

/** @brief Whether an overflow policy enum of spdlog has discard_new, which spdlog 1.12 lacks. */
template <typename Policy, typename = void>
inline constexpr bool has_discard_new_v = false;
template <typename Policy>
inline constexpr bool has_discard_new_v<Policy, std::void_t<decltype(Policy::discard_new)>> = true;

/** @brief The same policy in spdlog's enum. Throws std::invalid_argument when this spdlog does not have it. */
template <typename Policy>
Policy to_spdlog_policy(overflow_policy policy)
{
    if (policy == overflow_policy::overrun_oldest)
    {
        return Policy::overrun_oldest;
    }
    if (policy == overflow_policy::discard_new)
    {
        if constexpr (has_discard_new_v<Policy>)
        {
            return Policy::discard_new;
        }
        else
        {
            throw std::invalid_argument("jspdlog: this spdlog has no discard_new overflow policy");
        }
    }
    return Policy::block;
}

#if !JSPDLOG_SPDLOG_V2
/**
 * @brief Owns an asynchronous logger of spdlog v1 together with its thread pool, which the logger only observes.
 *
 * The logger is declared last, so it is released first. Each queued line also holds the logger, so it lives until the
 * line is written. Then the pool is destroyed: it queues a request to stop behind the waiting lines and joins its
 * thread, so every line that is queued is written first. The queued lines hold the logger but not this object or the
 * pool, so the worker thread never destroys the pool that it belongs to.
 */
struct owned_async_logger
{
    /** @brief The queue and its worker thread. */
    std::shared_ptr<spdlog::details::thread_pool> pool;
    /** @brief The logger that queues into the pool. */
    std::shared_ptr<spdlog::async_logger> logger;
};
#endif

} // namespace detail

/**
 * @brief A logger whose lines are written to the sinks by a worker thread of its own.
 *
 * The worker and its queue live as long as the last copy of the logger, counting its children and the pointers returned
 * by spdlog_logger. When the last one goes away, the queued lines are written to the sinks and the thread is joined, so
 * that destructor can take as long as the sinks need. Let it happen before main returns. A clone of the spdlog::logger
 * does not keep the worker of spdlog v1 alive. No global thread pool or registry is used, so spdlog::shutdown does not
 * affect the logger.
 *
 * Values and format arguments are serialized on the calling thread, so nothing captured by reference reaches the
 * worker. flush only queues a request behind the waiting lines and returns at once. Throws std::invalid_argument if a
 * sink is null, if queue_size is 0 or above the limit of spdlog v2, or if the overflow policy is not available.
 *
 * @code
 * auto sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>("app.log", 10 * 1024 * 1024, 5);
 * jspdlog::async_options async;
 * async.overflow = jspdlog::overflow_policy::overrun_oldest;
 * const auto logger = jspdlog::make_async_logger("app", {sink}, async);
 * @endcode
 */
[[nodiscard]] inline json_logger make_async_logger(
    std::string name,
    std::vector<spdlog::sink_ptr> sinks,
    const async_options &async = {},
    const json_options &options = {}
)
{
    for (const spdlog::sink_ptr &sink : sinks)
    {
        if (!sink)
        {
            throw std::invalid_argument("jspdlog: sink must not be null");
        }
    }
    if (async.queue_size == 0)
    {
        throw std::invalid_argument("jspdlog: the queue of an async logger must hold at least one line");
    }
#if JSPDLOG_SPDLOG_V2
    if (async.queue_size > spdlog::sinks::async_sink::max_queue_size)
    {
        throw std::invalid_argument("jspdlog: the queue of an async logger holds at most 250000 lines with spdlog v2");
    }
#endif
    spdlog::err_handler on_error;
    if (async.json_errors)
    {
        error_report_options report;
        report.format = options;
        on_error = make_error_handler(name, std::move(report));
    }

#if JSPDLOG_SPDLOG_V2
    spdlog::sinks::async_sink::config config;
    config.queue_size = async.queue_size;
    config.policy = detail::to_spdlog_policy<spdlog::sinks::async_sink::overflow_policy>(async.overflow);
    config.sinks = std::move(sinks);
    // Errors of the sinks are raised on the worker thread, which has a handler of its own.
    config.custom_err_handler = on_error;
    json_logger logger(std::move(name), std::make_shared<spdlog::sinks::async_sink>(std::move(config)), options);
    if (on_error)
    {
        // Errors on the calling thread, such as a serializer that throws, go to the logger's handler.
        logger.spdlog_logger()->set_error_handler(std::move(on_error));
    }
    return logger;
#else
    const auto policy = detail::to_spdlog_policy<spdlog::async_overflow_policy>(async.overflow);
    auto owner = std::make_shared<detail::owned_async_logger>();
    owner->pool = std::make_shared<spdlog::details::thread_pool>(async.queue_size, std::size_t{1});
    owner->logger =
        std::make_shared<spdlog::async_logger>(std::move(name), sinks.begin(), sinks.end(), owner->pool, policy);
    if (on_error)
    {
        owner->logger->set_error_handler(std::move(on_error));
    }
    // The aliasing constructor: the pointer is the logger, and holding it keeps the whole owner alive.
    const std::shared_ptr<spdlog::logger> handle(owner, owner->logger.get());
    return json_logger::adopt(handle, options);
#endif
}

} // namespace jspdlog
