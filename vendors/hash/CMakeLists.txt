add_library(Hash STATIC crc32.cpp digest.cpp keccak.cpp md5.cpp sha1.cpp sha3.cpp sha256.cpp)

target_include_directories(Hash PUBLIC ".")