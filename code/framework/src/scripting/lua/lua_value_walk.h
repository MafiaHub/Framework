/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <lua.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

// What the transfer and the JSON codec of Lua share (lua_transfer.cpp, lua_json.cpp): how a table is told an array
// from an object, where a walk is inside a value, and how host code runs what can raise.
//
// Both are called from inside a script call and from host code outside any entry, where a Lua error has nobody to
// catch it. So each runs its work as a C function under lua_pcall and answers through its return value. A table is
// read raw: no metamethod runs, and the work runs no script at all.
namespace Framework::Scripting::LuaValues {
    // Tables nested in one another, as deep as ValueTransfer copies objects.
    inline constexpr size_t kMaxDepth = 128;

    // 2^53: an integral double up to it is an exact integer.
    inline constexpr double kExactIntegerLimit = 9007199254740992.0;
    inline constexpr lua_Integer kExactInteger = lua_Integer {1} << 53;

    // Pushes `number` as Lua holds what crosses from another language: an integer when it is integral and within
    // 2^53, a float otherwise. A negative zero stays the float it is.
    inline void PushNumber(lua_State *L, double number) {
        if (number >= -kExactIntegerLimit && number <= kExactIntegerLimit && std::trunc(number) == number && !(number == 0.0 && std::signbit(number))) {
            lua_pushinteger(L, static_cast<lua_Integer>(number));
        }
        else {
            lua_pushnumber(L, static_cast<lua_Number>(number));
        }
    }

    // Whether `text` is well-formed UTF-8: no stray or missing continuation byte, no overlong form, no surrogate
    // and nothing beyond U+10FFFF.
    inline bool IsValidUtf8(std::string_view text) {
        const auto *bytes = reinterpret_cast<const unsigned char *>(text.data());
        const size_t size = text.size();
        size_t i          = 0;
        while (i < size) {
            const unsigned char lead = bytes[i];
            if (lead < 0x80) {
                ++i;
                continue;
            }
            size_t length         = 0;
            unsigned char minimum = 0x80; // of the second byte
            unsigned char maximum = 0xBF;
            if (lead >= 0xC2 && lead <= 0xDF) {
                length = 2;
            }
            else if (lead >= 0xE0 && lead <= 0xEF) {
                length  = 3;
                minimum = lead == 0xE0 ? 0xA0 : 0x80;
                maximum = lead == 0xED ? 0x9F : 0xBF;
            }
            else if (lead >= 0xF0 && lead <= 0xF4) {
                length  = 4;
                minimum = lead == 0xF0 ? 0x90 : 0x80;
                maximum = lead == 0xF4 ? 0x8F : 0xBF;
            }
            else {
                return false;
            }
            if (size - i < length || bytes[i + 1] < minimum || bytes[i + 1] > maximum) {
                return false;
            }
            for (size_t k = 2; k < length; ++k) {
                if ((bytes[i + k] & 0xC0) != 0x80) {
                    return false;
                }
            }
            i += length;
        }
        return true;
    }

    // The most of a key that an error text spells. A script chooses its keys, and one of megabytes must not end
    // up whole in a message, nor be copied for each step into it.
    inline constexpr size_t kMaxStepBytes = 64;

    // How much of `name` a path spells: all of it, or its first kMaxStepBytes, less what would cut a UTF-8
    // character in two.
    inline size_t StepLength(std::string_view name) {
        if (name.size() <= kMaxStepBytes) {
            return name.size();
        }
        size_t length = kMaxStepBytes;
        for (int back = 0; back < 3 && length > 0 && (static_cast<unsigned char>(name[length]) & 0xC0) == 0x80; ++back) {
            --length;
        }
        return length;
    }

    // Appends `name` as a path spells it: what StepLength keeps of it, and "..." for what it leaves out.
    inline void AppendStepName(std::string &out, std::string_view name) {
        const size_t length = StepLength(name);
        out.append(name.substr(0, length));
        if (length < name.size()) {
            out += "...";
        }
    }

    // Where a walk is inside a value, spelled only when it fails: "value.items[2].name".
    //
    // A step is entered and left by hand and not by a destructor, so that a Lua error or an exception that unwinds
    // the walk leaves the path where it was raised, for whoever catches it to spell. Steps are kept and used
    // again, which keeps a walk from allocating for each property it enters.
    class Path final {
      public:
        void EnterIndex(size_t index) {
            Enter(Step::Index, index);
        }

        // Keeps a copy of what it will spell of `name`, so the name need not outlive the step.
        void EnterProperty(std::string_view name) {
            Entry &entry = Enter(Step::Property, 0);
            entry.name.clear();
            AppendStepName(entry.name, name);
        }

        void EnterMapKey(size_t index) {
            Enter(Step::MapKey, index);
        }

        void EnterMapValue(size_t index) {
            Enter(Step::MapValue, index);
        }

        void EnterMember(size_t index) {
            Enter(Step::Member, index);
        }

        void Leave() {
            --_depth;
        }

        std::string ToString() const {
            std::string out = "value";
            for (size_t i = 0; i < _depth; ++i) {
                const Entry &entry = _entries[i];
                switch (entry.step) {
                case Step::Index: out += "[" + std::to_string(entry.index) + "]"; break;
                case Step::Property: out += "." + entry.name; break;
                case Step::MapKey: out += ".<key " + std::to_string(entry.index) + ">"; break;
                case Step::MapValue: out += ".<value " + std::to_string(entry.index) + ">"; break;
                case Step::Member: out += ".<member " + std::to_string(entry.index) + ">"; break;
                }
            }
            return out;
        }

      private:
        enum class Step : uint8_t {
            Index,
            Property,
            MapKey,
            MapValue,
            Member
        };

        struct Entry final {
            Step step    = Step::Index;
            size_t index = 0;
            std::string name;
        };

        Entry &Enter(Step step, size_t index) {
            if (_depth == _entries.size()) {
                _entries.emplace_back();
            }
            Entry &entry = _entries[_depth++];
            entry.step   = step;
            entry.index  = index;
            return entry;
        }

        std::vector<Entry> _entries;
        size_t _depth = 0;
    };

    // The keys of a table, as the mapping of lua_transfer.h reads them.
    //
    // The keys are copies. A view into the string of a key is only good while the table holds the key, and a weak
    // table (`__mode`, which a script may set) stops holding it in the middle of a walk: the walk allocates, the
    // collector runs, an entry whose value died is cleared, and the next cycle frees the string of its key.
    struct TableShape final {
        // Keys exactly 1..length, which the empty table has with a length of 0: an array.
        bool sequence      = false;
        lua_Integer length = 0;
        // Of any other table: its integer keys ascending, then its string keys sorted by their bytes.
        std::vector<lua_Integer> integers;
        std::vector<std::string> strings;
        // The bytes of the string keys together.
        size_t stringBytes = 0;
        // A key that is neither a string nor an integer was found.
        bool foreignKey = false;
        // The table has more keys, or more bytes of string keys, than the walk may still take.
        bool tooLarge = false;

        // The values the table holds: what a walk counts it as.
        size_t Count() const {
            return sequence ? static_cast<size_t>(length) : integers.size() + strings.size();
        }
    };

    // Reads the keys of the table at the absolute index `index`, which holds at most `maxKeys` of them and
    // `maxStringBytes` of string keys: past either it stops with `tooLarge`, having kept no more than that, so that
    // a table of millions of keys costs the host no more than a table the walk accepts. The caller has room for two
    // values on the stack. Nothing here allocates in the Lua state, so no entry goes while the keys are read.
    inline void ReadShape(lua_State *L, int index, TableShape &shape, size_t maxKeys, size_t maxStringBytes) {
        // Keys are distinct, so `count` positive integer keys of which the largest is `count` are 1..count.
        lua_Integer count   = 0;
        lua_Integer largest = 0;
        bool positive       = true;
        size_t keys         = 0;
        lua_pushnil(L);
        while (lua_next(L, index) != 0) {
            lua_pop(L, 1);
            if (++keys > maxKeys) {
                shape.tooLarge = true;
                lua_pop(L, 1);
                return;
            }
            // The key stays as it is for lua_next: lua_tolstring would turn a number into a string.
            if (lua_isinteger(L, -1) != 0) {
                const lua_Integer key = lua_tointegerx(L, -1, nullptr);
                positive              = positive && key >= 1;
                largest               = std::max(largest, key);
                ++count;
            }
            else if (lua_type(L, -1) == LUA_TSTRING) {
                size_t length    = 0;
                const char *text = lua_tolstring(L, -1, &length);
                if (length > maxStringBytes - shape.stringBytes) {
                    shape.tooLarge = true;
                    lua_pop(L, 1);
                    return;
                }
                shape.stringBytes += length;
                shape.strings.emplace_back(text, length);
            }
            else {
                shape.foreignKey = true;
                lua_pop(L, 1);
                return;
            }
        }

        if (shape.strings.empty() && positive && largest == count) {
            shape.sequence = true;
            shape.length   = count;
            return;
        }

        // Rare enough to walk the table again for: a table with a hole, or one that mixes its keys.
        if (count > 0) {
            shape.integers.reserve(static_cast<size_t>(count));
            lua_pushnil(L);
            while (lua_next(L, index) != 0) {
                lua_pop(L, 1);
                if (lua_isinteger(L, -1) != 0) {
                    shape.integers.push_back(lua_tointegerx(L, -1, nullptr));
                }
            }
            std::sort(shape.integers.begin(), shape.integers.end());
        }
        std::sort(shape.strings.begin(), shape.strings.end());
    }

    // Whether the table spells the integer key whose decimal text is `text` as a string key too. The string key is
    // then the one that counts, as it is for Bind::Value::Get.
    inline bool HasStringKey(const TableShape &shape, std::string_view text) {
        return std::binary_search(shape.strings.begin(), shape.strings.end(), text, [](std::string_view left, std::string_view right) {
            return left < right;
        });
    }

    // Takes the error a failed lua_pcall left on top of the stack, as text. Raises nothing: an error that is not a
    // string is named, not converted.
    inline std::string PopError(lua_State *L) {
        std::string error;
        if (lua_type(L, -1) == LUA_TSTRING) {
            size_t length    = 0;
            const char *text = lua_tolstring(L, -1, &length);
            error.assign(text, length);
        }
        else {
            error = std::string("a Lua error that is a ") + lua_typename(L, lua_type(L, -1)) + " value";
        }
        lua_pop(L, 1);
        return error;
    }

    // What a protected call is handed: the work, whether it was done, and what host code threw while doing it.
    template <typename Run>
    struct Job final {
        Run *run  = nullptr;
        bool done = false;
        std::string thrown;
    };

    // Runs the job at index 1. An exception of host code is kept and not raised: nothing is raised from a catch
    // block, and the protected call around this would swallow an exception that is not Lua's own.
    template <typename Run>
    int RunJob(lua_State *L) {
        auto *job = static_cast<Job<Run> *>(lua_touserdata(L, 1));
        try {
            job->done = (*job->run)();
        }
        catch (const std::exception &exception) {
            job->thrown = exception.what();
            if (job->thrown.empty()) {
                job->thrown = "an exception without a message";
            }
        }
        return job->done ? 1 : 0;
    }

    // Runs `run` as a C function under a protected call on `L`. The function finds the value at the absolute
    // index `argument` at its index 2, when `argument` is not 0, and has LUA_MINSTACK free slots.
    //
    // True when `run` returned true: the value it left on top of its stack is then on top of the stack of `L` when
    // `leavesValue`. False otherwise, with the stack of `L` as it was: `unwound` is the Lua error or the exception
    // that unwound `run`, and is left empty when `run` returned false by itself.
    template <typename Run>
    bool Protected(lua_State *L, Run &&run, int argument, bool leavesValue, std::string &unwound) {
        using Work = std::remove_reference_t<Run>;
        if (lua_checkstack(L, 3) == 0) {
            unwound = "the Lua stack cannot grow";
            return false;
        }
        Job<Work> job {&run};
        lua_pushcfunction(L, &RunJob<Work>);
        lua_pushlightuserdata(L, &job);
        if (argument != 0) {
            lua_pushvalue(L, argument);
        }
        if (lua_pcall(L, argument != 0 ? 2 : 1, leavesValue ? 1 : 0, 0) != LUA_OK) {
            unwound = PopError(L);
            return false;
        }
        if (job.done) {
            return true;
        }
        if (leavesValue) {
            lua_pop(L, 1);
        }
        unwound = std::move(job.thrown);
        return false;
    }
} // namespace Framework::Scripting::LuaValues
