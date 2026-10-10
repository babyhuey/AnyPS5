#include "prx/libc/include/general/VabiMacros.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

extern "C" {
int APS5_VABI gethostname_nid_postfix(char*, std::size_t);
int* APS5_VABI __error_nid_postfix();
}

static void Require(bool condition, int line) {
    if (!condition) {
        std::fprintf(stderr, "Hostname check failed at line %d\n", line);
        std::abort();
    }
}
#define Check(value) Require((value), __LINE__)

int main() {
    std::array<char, 1024> native{};
#ifdef _WIN32
    DWORD size = native.size();
    Check(GetComputerNameExA(ComputerNameDnsHostname, native.data(), &size));
#else
    Check(::gethostname(native.data(), native.size()) == 0);
#endif
    const std::string expected = native.data();
    Check(!expected.empty());
    for (std::size_t length = 0; length <= expected.size() + 2; ++length) {
        std::vector<char> buffer(length + 2, '#');
        *__error_nid_postfix() = 71;
        const int result = gethostname_nid_postfix(buffer.data() + 1, length);
        Check(buffer.front() == '#' && buffer.back() == '#');
        if (length <= expected.size()) {
            Check(result == -1 && *__error_nid_postfix() == 63);
            Check(std::memcmp(buffer.data() + 1, expected.data(), length) == 0);
        } else {
            Check(result == 0 && *__error_nid_postfix() == 71);
            Check(std::strcmp(buffer.data() + 1, expected.c_str()) == 0);
            for (std::size_t index = expected.size() + 2; index < buffer.size(); ++index) Check(buffer[index] == '#');
        }
    }
    *__error_nid_postfix() = 22;
    Check(gethostname_nid_postfix(nullptr, 0) == 0 && *__error_nid_postfix() == 22);
    Check(gethostname_nid_postfix(nullptr, 1024) == 0 && *__error_nid_postfix() == 22);
    std::thread worker([] {
        char byte = '#';
        *__error_nid_postfix() = 0;
        Check(gethostname_nid_postfix(&byte, 0) == -1 && *__error_nid_postfix() == 63 && byte == '#');
    });
    worker.join();
    Check(*__error_nid_postfix() == 22);
}
