#include "asset_streamer.h"

#include <cstdio>
#include <filesystem>
#include <mafianet/BitStream.h>
#include <mafianet/FileList.h>
#include <mafianet/SuperFastHash.h>

namespace Framework::Networking {
    bool AssetStreamer::AddFile(const char *filePath, const char *fileName) {
        std::error_code error;
        const auto size = std::filesystem::file_size(filePath, error);
        // MafiaNet uses a 32-bit bit count for transferred data.
        if (error || size > UINT32_MAX / 8)
            return false;
        FILE *file = std::fopen(filePath, "rb");
        if (!file)
            return false;
        auto hash         = SuperFastHashFilePtr(file);
        const bool failed = std::ferror(file) != 0;
        std::fclose(file);
        if (failed)
            return false;
        if (MafiaNet::BitStream::DoEndianSwap())
            MafiaNet::BitStream::ReverseBytesInPlace(reinterpret_cast<unsigned char *>(&hash), sizeof(hash));
        availableUploads->AddFile(fileName, filePath, reinterpret_cast<const char *>(&hash), sizeof(hash), static_cast<unsigned>(size), FileListNodeContext(0, 0, 0, 0));
        return true;
    }
} // namespace Framework::Networking
