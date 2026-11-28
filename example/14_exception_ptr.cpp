// catch (...): log whatever was thrown, a std::exception or not, with its type and message.

#include <jspdlog/factories.h>

#include <exception>
#include <stdexcept>

namespace
{

// Stands for a job that can fail with a standard exception, or with something else.
void run(int job)
{
    if (job == 1)
    {
        throw std::runtime_error("disk full");
    }
    if (job == 2)
    {
        throw job; // NOLINT(bugprone-std-exception-baseclass): not everything thrown is a std::exception
    }
}

void process(const jspdlog::json_logger &logger, int job)
{
    try
    {
        run(job);
        logger.info({"job", job}, "job done");
    }
    catch (...)
    {
        // {...,"error":{"type":"std::runtime_error","message":"disk full"},"job":1,"message":"job failed"}
        // {...,"error":{"type":"unknown"},"job":2,"message":"job failed"}
        logger.error(std::current_exception(), {"job", job}, "job failed");
    }
}

} // namespace

int main()
{
    const auto logger = jspdlog::make_stdout_logger("worker");
    for (int job = 0; job < 3; ++job)
    {
        process(logger, job);
    }
    return 0;
}
