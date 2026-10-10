#include "prx/libc/include/general/VabiMacros.hpp"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <new>
#include <string>
#include <system_error>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/utsname.h>
#endif

namespace {

std::string Hostname() {
#ifdef _WIN32
    DWORD size = 0;
    if (!GetComputerNameExA(ComputerNameDnsHostname, nullptr, &size) && GetLastError() != ERROR_MORE_DATA)
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category());
    std::vector<char> buffer(size);
    if (!GetComputerNameExA(ComputerNameDnsHostname, buffer.data(), &size))
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category());
    return {buffer.data(), size};
#else
    struct utsname host{};
    if (::uname(&host) != 0) throw std::system_error(errno, std::generic_category());
    return host.nodename;
#endif
}

}

extern "C" int APS5_VABI gethostname_nid_postfix(char* name, std::size_t length) {
    const int savedError = errno;
    try {
        const auto host = Hostname();
        if (name == nullptr) { errno = savedError; return 0; }
        const auto count = std::min(length, host.size() + 1);
        if (count != 0) std::memcpy(name, host.c_str(), count);
        if (length <= host.size()) { errno = 63; return -1; }
        errno = savedError;
        return 0;
    } catch (const std::bad_alloc&) {
        errno = 12;
        return -1;
    } catch (const std::system_error& error) {
        const auto condition = error.code().default_error_condition();
        errno = condition == std::errc::permission_denied ? 13 : 5;
        return -1;
    }
}
