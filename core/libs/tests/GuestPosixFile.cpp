#include "prx/libc/include/general/VabiMacros.hpp"
#include "prx/libkernel/File/include/FileFlags.hpp"
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <source_location>
#include <string>
#include <sys/stat.h>

extern "C" {
int APS5_VABI isatty_nid_postfix(int descriptor);
int APS5_VABI mkstemp_nid_postfix(char* path);
int* APS5_VABI __error_nid_postfix();
int APS5_VABI open_nid_postfix(const char* path, int flags, int mode);
int APS5_VABI close_nid_postfix(int descriptor);
int APS5_VABI pipe_nid_postfix(int* descriptors);
int APS5_VABI socket_nid_postfix(int family, int type, int protocol);
std::int64_t APS5_VABI write_nid_postfix(int descriptor, const char* buffer, std::int64_t size);
}

void Require(bool condition, std::source_location location = std::source_location::current()) {
    if (!condition) {
        std::fprintf(stderr, "Posix file check failed at line %u\n", location.line());
        std::abort();
    }
}

int Error() {
    return *__error_nid_postfix();
}

bool Padded(const std::string& text) {
    for (const char character : text) {
        if (!std::isalnum(static_cast<unsigned char>(character))) return false;
    }
    return true;
}

int main() {
    const auto root = std::filesystem::path("anyps5-posix-file-test-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Require(std::filesystem::create_directory(root));
    const std::string prefix = (root / "tmp.").string();

    std::string first = prefix + "XXXXXX";
    const int firstDescriptor = mkstemp_nid_postfix(first.data());
    Require(firstDescriptor >= 0);
    Require(first.size() == prefix.size() + 6 && first.compare(0, prefix.size(), prefix) == 0);
    Require(Padded(first.substr(prefix.size())));
    Require(std::filesystem::is_regular_file(first));
    Require(write_nid_postfix(firstDescriptor, "abc", 3) == 3);
#ifndef _WIN32
    struct stat status{};
    Require(::stat(first.c_str(), &status) == 0 && (status.st_mode & 0777) == 0600);
#endif
    Require(isatty_nid_postfix(firstDescriptor) == 0 && Error() == 25);
    Require(close_nid_postfix(firstDescriptor) == 0);
    Require(std::filesystem::file_size(first) == 3);

    std::string second = prefix + "XXXXXX";
    const int secondDescriptor = mkstemp_nid_postfix(second.data());
    Require(secondDescriptor >= 0 && second != first);
    Require(close_nid_postfix(secondDescriptor) == 0);

    const std::string single = (root / "single").string();
    for (const char character : std::string("0123456789ABCDEFGHIJKLMNOPRSTUVWXYZabcdefghijklmnoprstuvwxyz")) {
        std::ofstream(single + character).put('x');
    }
    std::string last = single + "X";
    const int lastDescriptor = mkstemp_nid_postfix(last.data());
    Require(lastDescriptor >= 0 && (last == single + "q" || last == single + "Q"));
    Require(close_nid_postfix(lastDescriptor) == 0);
    std::string exhausted = single + "X";
    *__error_nid_postfix() = 0;
    Require(mkstemp_nid_postfix(exhausted.data()) == -1 && Error() == 17);
    Require(exhausted.back() != 'X');

    std::string fixed = first;
    Require(mkstemp_nid_postfix(fixed.data()) == -1 && Error() == 17 && fixed == first);

    std::string missing = (root / "missing" / "tmp.XXXXXX").string();
    Require(mkstemp_nid_postfix(missing.data()) == -1 && Error() == 2);

    std::string empty;
    Require(mkstemp_nid_postfix(empty.data()) == -1 && Error() == 22);

    std::string tooLong(1024, 'X');
    Require(mkstemp_nid_postfix(tooLong.data()) == -1 && Error() == 63);
    Require(tooLong == std::string(1024, 'X'));

    int pipeDescriptors[2]{};
    Require(pipe_nid_postfix(pipeDescriptors) == 0);
    Require(isatty_nid_postfix(pipeDescriptors[0]) == 0 && Error() == 25);
    Require(isatty_nid_postfix(pipeDescriptors[1]) == 0 && Error() == 25);
    Require(close_nid_postfix(pipeDescriptors[0]) == 0 && close_nid_postfix(pipeDescriptors[1]) == 0);
    Require(isatty_nid_postfix(pipeDescriptors[0]) == 0 && Error() == 9);

    const int directory = open_nid_postfix(root.string().c_str(), SCE_KERNEL_O_RDONLY, 0);
    Require(directory >= 0);
    Require(isatty_nid_postfix(directory) == 0 && Error() == 25);
    Require(close_nid_postfix(directory) == 0);

    const int udp = socket_nid_postfix(2, 2, 0);
    Require(udp >= 0x10000000);
    Require(isatty_nid_postfix(udp) == 0 && Error() == 25);
    Require(close_nid_postfix(udp) == 0);
    Require(isatty_nid_postfix(udp) == 0 && Error() == 9);
    Require(isatty_nid_postfix(-1) == 0 && Error() == 9);

    std::filesystem::remove_all(root);
    return 0;
}
