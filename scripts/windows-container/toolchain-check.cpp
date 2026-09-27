#include <filesystem>
#include <iostream>
#include <windows.h>

#if !defined(_MSC_VER) || defined(__clang__)
#error This check requires the Microsoft compiler.
#endif
#ifndef _DLL
#error This check requires the DLL runtime used by the framework.
#endif

static bool CheckStructuredExceptions() {
    __try {
        RaiseException(0xE0420001, 0, 0, nullptr);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return true;
    }
    return false;
}

int main() {
    static_assert(sizeof(void *) == 8, "The Windows container builds x64 targets.");
    std::cout << "MSVC " << _MSC_VER << ", pointer size " << sizeof(void *) << '\n';
    const auto directory = std::filesystem::current_path();
    std::cout << "Windows filesystem: " << directory.string() << '\n';
    return std::filesystem::is_directory(directory) && CheckStructuredExceptions() ? 0 : 1;
}
