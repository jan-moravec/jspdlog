// Factory helpers: loggers writing to standard output, standard error, a rotating file and through a worker thread of
// their own. They need no spdlog sink header, no registry and no global thread pool.

#include <jspdlog/factories.h>

#include <memory>

int main()
{
    const auto console = jspdlog::make_stdout_logger("app");
    console.info({"port", 8080}, "listening");

    const auto errors = jspdlog::make_stderr_logger("app");
    errors.warn("this line goes to standard error");

    constexpr std::size_t BYTES_PER_MEGABYTE = 1024 * 1024;
    const auto file =
        jspdlog::make_rotating_file_logger("app", "example_12_rotating.log", 10 * BYTES_PER_MEGABYTE, /*max_files=*/5);
    file.info("written to a file");

    // A worker thread owned by this logger writes to the sinks. When the last copy of the logger goes away, here at the
    // end of main, it writes what is still queued and stops.
    jspdlog::async_options async;
    async.overflow = jspdlog::overflow_policy::block;
    const auto background =
        jspdlog::make_async_logger("worker", {std::make_shared<spdlog::sinks::stdout_sink_mt>()}, async);
    background.info({"queued", true}, "hello from the async path");
    return 0;
}
