#pragma once

#include <string>

namespace Framework::Utils {
    std::wstring GetAbsolutePathW(const std::wstring &);
    std::string GetAbsolutePathA(const std::string &);
}
