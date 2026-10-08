#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif
#include "prx/libc/include/general/VabiMacros.hpp"
#include "prx/libkernel/File/include/FileFlags.hpp"
#include "prx/libkernel/Socket/include/SocketRuntime.hpp"
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <random>
#include <string>

extern "C" {
int* APS5_VABI __error_nid_postfix();
int APS5_VABI open_nid_postfix(const char* path, int flags, int mode);
int APS5_VABI ioctl_nid_postfix(int descriptor, std::uint64_t request, void* argument);
}

namespace {
constexpr int GuestEbadf = 9;
constexpr int GuestEexist = 17;
constexpr int GuestEinval = 22;
constexpr int GuestEnotty = 25;
constexpr int GuestEnametoolong = 63;
constexpr std::size_t GuestPathMax = 1024;
constexpr std::uint64_t GuestTiocgeta = 0x402c7413;
constexpr std::size_t GuestTermiosSize = 44;
constexpr char TemplatePad[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

int Fail(int error) {
    *__error_nid_postfix() = error;
    return -1;
}

#ifdef _WIN32
extern "C" _invalid_parameter_handler _set_thread_local_invalid_parameter_handler(_invalid_parameter_handler);
void IgnoreInvalidParameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, std::uintptr_t) {}
#endif

int NativeIsTerminal(int descriptor) {
#ifdef _WIN32
    const auto previous = _set_thread_local_invalid_parameter_handler(IgnoreInvalidParameter);
    const auto handle = reinterpret_cast<HANDLE>(::_get_osfhandle(descriptor));
    _set_thread_local_invalid_parameter_handler(previous);
    if (handle == INVALID_HANDLE_VALUE) return Fail(GuestEbadf);
    DWORD mode = 0;
    return ::GetConsoleMode(handle, &mode) ? 1 : Fail(GuestEnotty);
#else
    if (::isatty(descriptor)) return 1;
    return Fail(errno == EBADF ? GuestEbadf : GuestEnotty);
#endif
}
}

extern "C" {

int APS5_VABI isatty_nid_postfix(int descriptor) {
    if (descriptor >= GuestSockets::FirstDescriptor) {
        std::array<unsigned char, GuestTermiosSize> attributes{};
        return ioctl_nid_postfix(descriptor, GuestTiocgeta, attributes.data()) == 0;
    }
    return NativeIsTerminal(descriptor) == 1;
}

int APS5_VABI mkstemp_nid_postfix(char* path) {
    const auto length = std::strlen(path);
    if (length >= GuestPathMax) return Fail(GuestEnametoolong);
    if (length == 0) return Fail(GuestEinval);
    char* const suffix = path + length;
    char* start = suffix;
    std::random_device device;
    std::uniform_int_distribution<std::size_t> pick(0, sizeof(TemplatePad) - 2);
    while (start > path && start[-1] == 'X') *--start = TemplatePad[pick(device)];
    const std::string first(start, suffix);
    for (;;) {
        const int descriptor = open_nid_postfix(path,
            SCE_KERNEL_O_RDWR | SCE_KERNEL_O_CREAT | SCE_KERNEL_O_EXCL, 0600);
        if (descriptor >= 0 || *__error_nid_postfix() != GuestEexist) return descriptor;
        for (char* position = start;; ++position) {
            if (position == suffix) return -1;
            const char* next = std::strchr(TemplatePad, *position) + 1;
            *position = *next == '\0' ? TemplatePad[0] : *next;
            if (*position != first[static_cast<std::size_t>(position - start)]) break;
        }
    }
}

}
