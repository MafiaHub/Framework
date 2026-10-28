/**
  zpl - Unit testing framework

Usage:
  #include "unit.h" in EXACTLY one source file, usually the one containing your testing app's entry point.

  There really is no need to include this file multiple times within a project, unless you wish to run
  multiple tests within a single executable or split test cases to multiple compilation units, in such case
  define UNIT_STATIC to ensure the library won't leak symbols outside compilation units.

  and cover your beautiful code already!

GitHub:
  https://github.com/zpl-c/tester

Version History:
  MafiaHub - typed failure values, FW_TEST_FILTER, failure summary, NO_COLOR (see below)
  1.1.1 - ensure memory arena only works with zpl present
  1.1.0 - introduce memory arena for per-module allocations
  1.0.1 - Small tweaks
  1.0.0 - Where it all started... (not really)

License:
  This Software is dual licensed under the following licenses:

  Unlicense
  This is free and unencumbered software released into the public domain.

  Anyone is free to copy, modify, publish, use, compile, sell, or
  distribute this software, either in source code form or as a compiled
  binary, for any purpose, commercial or non-commercial, and by any
  means.

  In jurisdictions that recognize copyright laws, the author or authors
  of this software dedicate any and all copyright interest in the
  software to the public domain. We make this dedication for the benefit
  of the public at large and to the detriment of our heirs and
  successors. We intend this dedication to be an overt act of
  relinquishment in perpetuity of all present and future rights to this
  software under copyright law.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
  IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
  OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
  ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
  OTHER DEALINGS IN THE SOFTWARE.

  For more information, please refer to <http://unlicense.org/>

  BSD 3-Clause
  Copyright (c) 2016-2021 Dominik Madarász. All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

  1. Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.
  2. Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.
  3. Neither the name of the copyright holder nor the names of its contributors
  may be used to endorse or promote products derived from this software without
  specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
  OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/

/* Adjust it to your needs, preferably to the number of modules you wish to test against. */
#ifndef UNIT_MAX_MODULES
#define UNIT_MAX_MODULES 256
#endif

#ifndef UNIT_SKIP_MAGIC
#define UNIT_SKIP_MAGIC 0xFF
#endif

#ifndef UNIT_JOIN2
#define UNIT_JOIN2(a, b) a##b
#endif

/* Isolates test results within compilation units, this allows for running
    multiple test suites per single binary. */
#ifdef UNIT_STATIC
#define UNIT_DEF static
#else
#define UNIT_DEF
#endif

#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#if defined(_WIN32)
#include <io.h>
#define UNIT_ISATTY _isatty
#define UNIT_FILENO _fileno
#else
#include <unistd.h>
#define UNIT_ISATTY isatty
#define UNIT_FILENO fileno
#endif

/* zpl specific */

/* verify if zpl is present */
#if defined(ZPL_H) && !defined(UNIT_ZPL_DISABLE_ARENA)
/* specifies memory limit for per-module memory allocator */
#ifndef UNIT_ARENA_MEM
#define UNIT_ARENA_MEM (512 * 1024)
#endif

#define UNIT_ZPL_INITIALIZE_MEM_ARENA()                                                                                \
    zpl_arena hunk_mem = {0};                                                                                          \
    zpl_arena_init_from_allocator(&hunk_mem, zpl_heap(), UNIT_ARENA_MEM);                                              \
    zpl_allocator mem_alloc = zpl_arena_allocator(&hunk_mem);                                                          \
    zpl_unused(mem_alloc);
#define UNIT_ZPL_DESTROY_MEM_ARENA() zpl_arena_free(&hunk_mem);
#define UNIT_ZPL_CAPTURE_MEMORY()    zpl_temp_arena_memory mem_snapshot = zpl_temp_arena_memory_begin(&hunk_mem);
#define UNIT_ZPL_RESTORE_MEMORY()    zpl_temp_arena_memory_end(mem_snapshot);
#else
#define UNIT_ZPL_INITIALIZE_MEM_ARENA()
#define UNIT_ZPL_DESTROY_MEM_ARENA()
#define UNIT_ZPL_CAPTURE_MEMORY()
#define UNIT_ZPL_RESTORE_MEMORY()
#endif

/* MafiaHub: failure output for people and agents reading a log.
   - A failed assertion prints the expression and both values, rendered by type (unit__describe).
   - FW_TEST_FILTER=voice,state_bag runs only the modules whose name contains one of the substrings.
   - The run ends with a list of every failure, so the tail of a log is enough to act on.
   - Colors only on a terminal, and never with NO_COLOR set. */

#define UNIT_RED   unit__color("\x1B[31m")
#define UNIT_GREEN unit__color("\x1B[32m")
#define UNIT_AMBER unit__color("\x1B[33m")
#define UNIT_RESET unit__color("\x1B[0m")

#define MODULE(name, ...)                                                                                              \
    int32_t UNIT_JOIN2(module__, name)() {                                                                             \
        printf("--------------------------------------\n");                                                            \
        printf(" module: %s\n", #name);                                                                                \
        printf("--------------------------------------\n");                                                            \
        fflush(stdout);                                                                                                \
        _g_modules++;                                                                                                  \
        const char *_module = #name;                                                                                   \
        int32_t _total      = 0;                                                                                       \
        int32_t _errors     = 0;                                                                                       \
        int32_t _lasterr    = 0;                                                                                       \
        const char *_errstr = 0;                                                                                       \
        UNIT_ZPL_INITIALIZE_MEM_ARENA();                                                                               \
        {__VA_ARGS__};                                                                                                 \
        UNIT_ZPL_DESTROY_MEM_ARENA();                                                                                  \
        fflush(stdout);                                                                                                \
        printf("\n results: %d total, %s%d failed%s, %s%d passed%s\n", _total, _errors > 0 ? UNIT_RED : "", _errors,   \
               UNIT_RESET, _errors == 0 ? UNIT_GREEN : "", _total - _errors, UNIT_RESET);                              \
        _g_total += _total;                                                                                            \
        _g_errors += _errors;                                                                                          \
        if (_errors)                                                                                                   \
            _g_modules_err++;                                                                                          \
        return (_errors);                                                                                              \
    }

#define IT(desc, ...)                                                                                                  \
    _lasterr = 0;                                                                                                      \
    _errstr  = "";                                                                                                     \
    _total += 1;                                                                                                       \
    {                                                                                                                  \
        UNIT_ZPL_CAPTURE_MEMORY();                                                                                     \
        do { __VA_ARGS__ } while (0);                                                                                  \
        UNIT_ZPL_RESTORE_MEMORY();                                                                                     \
    }                                                                                                                  \
    if (_lasterr != UNIT_SKIP_MAGIC)                                                                                   \
        _errors += _lasterr;                                                                                           \
    printf(" * [%s%s%s]: It %s%s\n",                                                                                   \
           (_lasterr == UNIT_SKIP_MAGIC) ? UNIT_AMBER                                                                  \
           : (_lasterr)                  ? UNIT_RED                                                                    \
                                         : UNIT_GREEN,                                                                 \
           (_lasterr == UNIT_SKIP_MAGIC) ? "SKIP"                                                                      \
           : (_lasterr)                  ? "FAIL"                                                                      \
                                         : "PASS",                                                                     \
           UNIT_RESET, desc, _errstr);                                                                                 \
    if (_lasterr == 1)                                                                                                 \
        unit__record_failure(_module, desc, _errstr);

/* TEST CHECKS */
#define UNIT_FAIL_WITH(a, relation, b)                                                                                 \
    {                                                                                                                  \
        _errstr  = unit__failure(#a, unit__describe(a), relation, #b, unit__describe(b), __FILE__, __LINE__);          \
        _lasterr = 1;                                                                                                  \
        break;                                                                                                         \
    }
#define FAIL(a, b)    UNIT_FAIL_WITH(a, "==", b)
#define UFAIL(a, b)   UNIT_FAIL_WITH(a, "==", b)
#define STRFAIL(a, b) UNIT_FAIL_WITH(a, "equals", b)
#define EQUALS(a, b)                                                                                                   \
    if (a != b) {                                                                                                      \
        UNIT_FAIL_WITH(a, "==", b);                                                                                    \
    }
#define UEQUALS(a, b)                                                                                                  \
    if (a != b) {                                                                                                      \
        UNIT_FAIL_WITH(a, "==", b);                                                                                    \
    }
#define STREQUALS(a, b)                                                                                                \
    if (!!strcmp(a, b)) {                                                                                              \
        UNIT_FAIL_WITH(a, "equals", b);                                                                                \
    }
#define STRCEQUALS(a, b, c)                                                                                            \
    if (!!strncmp(a, b, c)) {                                                                                          \
        UNIT_FAIL_WITH(a, "starts like (first " #c " chars)", b);                                                      \
    }
#define STRCNEQUALS(a, b, c)                                                                                           \
    if (!strncmp(a, b, c)) {                                                                                           \
        UNIT_FAIL_WITH(a, "differs from (first " #c " chars)", b);                                                     \
    }
#define STRNEQUALS(a, b)                                                                                               \
    if (!strcmp(a, b)) {                                                                                               \
        UNIT_FAIL_WITH(a, "differs from", b);                                                                          \
    }
#define NEQUALS(a, b)                                                                                                  \
    if (a == b) {                                                                                                      \
        UNIT_FAIL_WITH(a, "!=", b);                                                                                    \
    }
#define LESSER(a, b)                                                                                                   \
    if (a >= b) {                                                                                                      \
        UNIT_FAIL_WITH(a, "<", b);                                                                                     \
    }
#define GREATER(a, b)                                                                                                  \
    if (a <= b) {                                                                                                      \
        UNIT_FAIL_WITH(a, ">", b);                                                                                     \
    }
#define LESSEREQ(a, b)                                                                                                 \
    if (a > b) {                                                                                                       \
        UNIT_FAIL_WITH(a, "<=", b);                                                                                    \
    }
#define GREATEREQ(a, b)                                                                                                \
    if (a < b) {                                                                                                       \
        UNIT_FAIL_WITH(a, ">=", b);                                                                                    \
    }
#define SKIP()                                                                                                         \
    {                                                                                                                  \
        _lasterr = UNIT_SKIP_MAGIC;                                                                                    \
        break;                                                                                                         \
    }

#if defined(__GCC__) || defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wattributes"
#pragma GCC diagnostic ignored "-Wunused-value"
#pragma GCC diagnostic ignored "-Wignored-qualifiers"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wwrite-strings"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#pragma GCC diagnostic ignored "-Wmissing-braces"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wpointer-to-int-cast"
#endif

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4201)
#pragma warning(disable : 4127) // Conditional expression is constant
#endif

typedef int32_t (*unit_case)();

#define UNIT_CREATE(name)                                                                                              \
    const char *unit_name                               = name;                                                        \
    unit_case unit_modules[UNIT_MAX_MODULES]            = {0};                                                         \
    const char *unit_module_names[UNIT_MAX_MODULES]     = {0};                                                         \
    int32_t unit_count                                  = 0;

#define UNIT_MODULE(name)                                                                                              \
    (unit_module_names[unit_count] = #name, unit_modules[unit_count++] = UNIT_JOIN2(module__, name))

#define UNIT_RUN() unit_main(unit_name, unit_modules, unit_module_names, unit_count)

/* INTERNALS */

UNIT_DEF int32_t _g_modules     = 0;
UNIT_DEF int32_t _g_modules_err = 0;
UNIT_DEF int32_t _g_total       = 0;
UNIT_DEF int32_t _g_errors      = 0;
UNIT_DEF std::vector<std::string> _g_failures;

static inline const char *unit__color(const char *code) {
    static const bool enabled = !getenv("NO_COLOR") && UNIT_ISATTY(UNIT_FILENO(stdout));
    return enabled ? code : "";
}

// Renders an assertion operand by its type. Anything without an operator<< still fails readably.
template <typename T>
std::string unit__describe(const T &value) {
    using V = std::remove_cvref_t<T>;
    if constexpr (std::is_same_v<V, bool>) {
        return value ? "true" : "false";
    } else if constexpr (std::is_same_v<V, std::nullptr_t>) {
        return "nullptr";
    } else if constexpr (std::is_enum_v<V>) {
        return std::to_string(static_cast<long long>(value)) + " (enum)";
    } else if constexpr (std::is_integral_v<V> && sizeof(V) == 1) {
        return std::to_string(static_cast<int>(value));
    } else if constexpr (std::is_convertible_v<const T &, const char *>) {
        const char *text = value;
        return text ? "\"" + std::string(text) + "\"" : "nullptr";
    } else if constexpr (std::is_convertible_v<const T &, std::string_view>) {
        return "\"" + std::string(std::string_view(value)) + "\"";
    } else if constexpr (requires(std::ostream &stream) { stream << value; }) {
        std::ostringstream stream;
        stream << std::boolalpha << value;
        return stream.str();
    } else {
        return "<no operator<< for this type>";
    }
}

static inline const char *unit__failure(const char *lhs, const std::string &lhsValue, const char *relation, const char *rhs,
                                        const std::string &rhsValue, const char *file, int line) {
    // An operand that is its own value, such as a literal, gets no value line.
    const auto operand = [](const char *text, const std::string &value) {
        return value == text ? std::string() : std::string("\n\t  ") + text + " = " + value;
    };
    static std::string message;
    message = std::string("\n\texpected: ") + lhs + " " + relation + " " + rhs + operand(lhs, lhsValue) + operand(rhs, rhsValue)
            + "\n\tat " + file + ":" + std::to_string(line) + "\n";
    return message.c_str();
}

static inline void unit__record_failure(const char *module, const char *desc, const char *detail) {
    std::string at = detail;
    const auto position = at.rfind("\tat ");
    at = position == std::string::npos ? "" : " (" + at.substr(position + 4, at.find('\n', position) - position - 4) + ")";
    _g_failures.push_back(std::string(module) + ": It " + desc + at);
}

// FW_TEST_FILTER: comma-separated substrings; a module runs when its name contains any of them.
static inline bool unit__selected(const char *filter, const char *module) {
    if (!filter || !*filter) {
        return true;
    }
    std::string_view rest(filter);
    while (!rest.empty()) {
        const auto comma = rest.find(',');
        const auto term  = rest.substr(0, comma);
        if (!term.empty() && std::string_view(module).find(term) != std::string_view::npos) {
            return true;
        }
        rest = comma == std::string_view::npos ? std::string_view() : rest.substr(comma + 1);
    }
    return false;
}

UNIT_DEF int32_t unit_main(const char *name, unit_case *cases, const char **names, int32_t count) {
    const char *filter = getenv("FW_TEST_FILTER");
    int32_t err = 0, cnt = 0;
    for (int32_t i = 0; i < count; ++i) { cnt += unit__selected(filter, names[i]); }

    printf("> testing suite:\n\n");
    printf("   *   suite: %s\n", name);
    printf("   * modules: %d\n", cnt);
    if (filter && *filter) {
        printf("   *  filter: %s (FW_TEST_FILTER)\n", filter);
    }
    printf("\n");

    if (cnt == 0) {
        printf("FW_TEST_FILTER=%s matches no module. Modules:\n", filter);
        for (int32_t i = 0; i < count; ++i) { printf("  %s\n", names[i]); }
        return 1;
    }

    for (int32_t i = 0; i < count; ++i) {
        if (unit__selected(filter, names[i])) {
            err += cases[i]();
        }
    }

    fflush(stdout);
    printf("--------------------------------------\n");
    printf("> total:\n\n");
    printf(" * modules: %d total, %s%d failed%s, %s%d passed%s\n", _g_modules, _g_modules_err > 0 ? UNIT_RED : "",
           _g_modules_err, UNIT_RESET, _g_modules_err == 0 ? UNIT_GREEN : "", _g_modules - _g_modules_err, UNIT_RESET);
    printf(" * tests:   %d total, %s%d failed%s, %s%d passed%s\n", _g_total, _g_errors > 0 ? UNIT_RED : "", _g_errors,
           UNIT_RESET, _g_errors == 0 ? UNIT_GREEN : "", _g_total - _g_errors, UNIT_RESET);
    if (!_g_failures.empty()) {
        printf("\n> failures:\n\n");
        for (const auto &failure : _g_failures) { printf(" * %s\n", failure.c_str()); }
    }
    printf("\n");

    return -err;
}

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#if defined(__GCC__) || defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
