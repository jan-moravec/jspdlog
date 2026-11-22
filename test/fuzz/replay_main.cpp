// main() for builds without libFuzzer: runs LLVMFuzzerTestOneInput on every file in the directories given.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size);

int main(int argc, char **argv)
{
    std::size_t count = 0;
    for (int i = 1; i < argc; ++i)
    {
        const std::filesystem::path dir{argv[i]};
        if (!std::filesystem::is_directory(dir))
        {
            continue; // an empty regressions folder does not exist in a fresh checkout
        }
        std::vector<std::filesystem::path> files;
        for (const auto &entry : std::filesystem::directory_iterator(dir))
        {
            if (entry.is_regular_file())
            {
                files.push_back(entry.path());
            }
        }
        std::sort(files.begin(), files.end()); // a stable order makes a failure easy to repeat
        for (const auto &file : files)
        {
            std::ifstream in(file, std::ios::binary);
            const std::vector<char> bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
            std::printf("replay %s\n", file.string().c_str());
            std::fflush(stdout);
            LLVMFuzzerTestOneInput(
                static_cast<const std::uint8_t *>(static_cast<const void *>(bytes.data())), bytes.size()
            );
            ++count;
        }
    }
    std::printf("replayed %zu inputs\n", count);
    return count == 0 ? 1 : 0; // a target without seeds is a mistake
}
