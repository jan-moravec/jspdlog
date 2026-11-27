// Loads the module given as the argument, logs through it, unloads it and forks. The module registers a pthread_atfork
// handler for the cached process id, and a handler that outlives its module would crash the child.

#include <sys/wait.h>

#include <dlfcn.h>
#include <iostream>
#include <unistd.h>

namespace
{

// iostream is used on purpose. This program has to need the C++ library itself: if the module were the first to load
// it, the library could bind to a symbol of the module, which would then stay loaded.
int fail(const char *what)
{
    std::cerr << "dlclose_fork: " << what << '\n';
    return 1;
}

// The error of the last dlopen or dlclose.
const char *loader_error()
{
    return ::dlerror(); // NOLINT(concurrency-mt-unsafe): this program has one thread
}

// The exit code that CTest reports as skipped.
constexpr int SKIPPED = 77;

} // namespace

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        return fail("pass the path of the module");
    }
    void *const module = ::dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (module == nullptr)
    {
        return fail(loader_error());
    }
    using log_function = bool (*)();
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): how POSIX hands out a function
    const auto log = reinterpret_cast<log_function>(::dlsym(module, "jspdlog_test_log"));
    if (log == nullptr || !log())
    {
        return fail("the module did not write a line with the process id");
    }
    if (::dlclose(module) != 0)
    {
        return fail(loader_error());
    }

    void *const still_loaded = ::dlopen(argv[1], RTLD_NOW | RTLD_NOLOAD);
    const bool unloaded = still_loaded == nullptr;
    if (!unloaded)
    {
        ::dlclose(still_loaded);
#ifdef __GLIBC__
        return fail("the module stayed loaded, so the test shows nothing");
#else
        // Some C libraries never unload, and then no handler can dangle. The test reports that it was skipped.
        std::cout << "dlclose_fork: the module stayed loaded\n";
#endif
    }
    else
    {
        std::cout << "dlclose_fork: the module was unloaded\n";
    }
    std::cout.flush(); // before fork, so the child has nothing buffered

    const pid_t child = ::fork();
    if (child < 0)
    {
        return fail("fork failed");
    }
    if (child == 0)
    {
        ::_exit(0);
    }
    int status = 0;
    if (::waitpid(child, &status, 0) != child)
    {
        return fail("waitpid failed");
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
    {
        return fail("the child of fork did not exit cleanly");
    }
    return unloaded ? 0 : SKIPPED;
}
