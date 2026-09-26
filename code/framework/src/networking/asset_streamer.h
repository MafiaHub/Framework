#pragma once

#include <mafianet/DirectoryDeltaTransfer.h>

namespace Framework::Networking {
    // DirectoryDeltaTransfer's stock AddFile stores the entire file as comparison data,
    // whereas the receiver announces a four-byte hash. Such entries never match its cache.
    class AssetStreamer final: public MafiaNet::DirectoryDeltaTransfer {
      public:
        bool AddFile(const char *filePath, const char *fileName);
    };
} // namespace Framework::Networking
