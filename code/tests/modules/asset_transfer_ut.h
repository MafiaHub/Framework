/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "networking/network_client.h"

#include <mafianet/file_list_transfer_cb_interface.h>

namespace AssetTransferTest {
    // Counts how often the transfer lets go of it.
    class CountingHandler final: public MafiaNet::FileListTransferCBInterface {
      public:
        int dereferences = 0;

        bool OnFile(OnFileStruct *) override {
            return true;
        }
        void OnFileProgress(FileProgressStruct *) override {}
        void OnDereference() override {
            ++dereferences;
        }
    };
} // namespace AssetTransferTest

// The client's asset transfer must still drop a sender's receivers when its connection closes, as the
// FileListTransfer it overrides does; otherwise they linger until the peer shuts down.
MODULE(asset_transfer, {
    IT("drops the receivers of a sender whose connection closed", {
        Framework::Networking::AssetFileTransfer transfer;
        AssetTransferTest::CountingHandler handler;
        MafiaNet::SystemAddress sender("127.0.0.1", 27015);

        const unsigned short setId = transfer.SetupReceive(&handler, false, sender);
        EQUALS(transfer.IsHandlerActive(setId), true);

        transfer.OnClosedConnection(sender, MafiaNet::UNASSIGNED_RAKNET_GUID, MafiaNet::LCR_CONNECTION_LOST);

        EQUALS(transfer.IsHandlerActive(setId), false);
        EQUALS(handler.dereferences, 1);
    });

    IT("keeps the receivers of other senders", {
        Framework::Networking::AssetFileTransfer transfer;
        AssetTransferTest::CountingHandler handler;
        MafiaNet::SystemAddress sender("127.0.0.1", 27015);
        MafiaNet::SystemAddress other("127.0.0.1", 27016);

        const unsigned short setId = transfer.SetupReceive(&handler, false, sender);
        transfer.OnClosedConnection(other, MafiaNet::UNASSIGNED_RAKNET_GUID, MafiaNet::LCR_CONNECTION_LOST);

        EQUALS(transfer.IsHandlerActive(setId), true);
        EQUALS(handler.dereferences, 0);

        transfer.CancelReceive(setId);
    });
});
