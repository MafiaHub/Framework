#include <string>

#include <sol/sol.hpp>

#include <hash/crc32.h>
#include <hash/keccak.h>
#include <hash/md5.h>
#include <hash/sha1.h>
#include <hash/sha256.h>
#include <hash/sha3.h>

namespace Framework::Scripting::Builtins {
    class Hash final {
      public:
        static std::string ToCRC32(std::string input) {
            CRC32 stream;
            stream.add(input.data(), input.size());
            return stream.getHash();
        }

        static std::string ToKeccak(std::string input) {
            Keccak stream;
            stream.add(input.data(), input.size());
            return stream.getHash();
        }

        static std::string ToMD5(std::string input) {
            MD5 stream;
            stream.add(input.data(), input.size());
            return stream.getHash();
        }

        static std::string toSHA1(std::string input) {
            MD5 stream;
            stream.add(input.data(), input.size());
            return stream.getHash();
        }

        static std::string ToSHA256(std::string input) {
            SHA256 stream;
            stream.add(input.data(), input.size());
            return stream.getHash();
        }

        static std::string ToSHA3(std::string input) {
            SHA3 stream;
            stream.add(input.data(), input.size());
            return stream.getHash();
        }

        static void Register(sol::state *luaEngine) {
            if (!luaEngine) {
                return;
            }

            sol::usertype<Hash> cls = luaEngine->new_usertype<Hash>("Hash");

            // Register static functions
            cls.set_function("toCRC32", &Hash::ToCRC32);
            cls.set_function("toKeccak", &Hash::ToKeccak);
            cls.set_function("toMD5", &Hash::ToMD5);
            cls.set_function("toSHA1", &Hash::toSHA1);
            cls.set_function("toSHA256", &Hash::ToSHA256);
            cls.set_function("toSHA3", &Hash::ToSHA3);
        }
    };
} // namespace Framework::Scripting::Builtins
