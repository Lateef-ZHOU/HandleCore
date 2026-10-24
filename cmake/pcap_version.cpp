#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <cstdio>

// Load the exact runtime selected by CMake, including its adjacent dependencies.
int main(int argc, char **argv)
{
    if (argc != 2) return 1;
    const auto library = LoadLibraryExA(argv[1], nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!library)
    {
        std::printf("LoadLibrary failed: %lu\n", GetLastError());
        return 2;
    }
    const auto version = reinterpret_cast<const char *(*)()>(GetProcAddress(library, "pcap_lib_version"));
    if (version) std::printf("%s\n", version());
    FreeLibrary(library);
    return version ? 0 : 3;
}
