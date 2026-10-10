/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "connection_admission_ut.h"

#include <mafianet/FileList.h>
#include <mafianet/FileListTransfer.h>
#include <mafianet/FileListTransferCBInterface.h>

// MafiaHub/Framework#315: the join refusal disconnects from inside FileListTransfer's completion
// callback, and the peer shutdown frees the receiver and packet pool that DecodeSetHeader and
// RakPeer::Receive still hold.
class DisconnectingDownloadHandler final: public MafiaNet::FileListTransferCBInterface {
  public:
    // Thrown when the shutdown has already freed what the callers hold: unwinding out of
    // RakPeer::Receive is the only way past the freed receiver and packet without touching them.
    struct TornDownUnderCaller {};

    Framework::Networking::NetworkClient *client = nullptr;
    unsigned short setID                         = 0;
    int completions                              = 0;
    int dereferences                             = 0;
    bool receiverSurvived                        = false;

    bool OnFile(OnFileStruct *) override {
        return true;
    }

    void OnFileProgress(FileProgressStruct *) override {}

    bool OnDownloadComplete(DownloadCompleteStruct *) override {
        ++completions;
        (void)client->Disconnect();
        receiverSurvived = client->GetFileListTransfer()->IsHandlerActive(setID);
        if (!receiverSurvived) {
            throw TornDownUnderCaller {};
        }
        // FileListTransfer then dereferences and deletes the receiver it still holds.
        return false;
    }

    void OnDereference() override {
        ++dereferences;
    }
};

MODULE(download_disconnect, {
    using Rig = ConnectionAdmissionRig;

    IT("keeps the download receiver alive when the completion callback disconnects", {
        // Outlives the rig: a receiver left behind by a failed step still points at it.
        DisconnectingDownloadHandler handler;
        Rig rig;
        EQUALS(rig.StartServer(false), true);
        EQUALS(rig.Connect(Rig::IdentityPayload("Downloader", "")), true);
        EQUALS(rig.PumpUntil([&] { return rig.seen.clientConnected && rig.server->GetPlayerCount() == 1u; }), true);

        handler.client = rig.client.get();
        handler.setID  = rig.client->GetFileListTransfer()->SetupReceive(&handler, false, rig.client->GetPeer()->GetSystemAddressFromIndex(0));
        EQUALS(handler.setID != static_cast<unsigned short>(-1), true);

        // An empty set: the header alone completes it, through DecodeSetHeader, which is how a
        // download ends when the client already has every file.
        MafiaNet::FileList nothing;
        MafiaNet::FileListTransfer sender;
        sender.Send(&nothing, rig.server->GetPeer(), rig.server->GetPeer()->GetSystemAddressFromIndex(0), handler.setID, MafiaNet::Priority::High, 0);

        bool tornDown = false;
        try {
            rig.PumpUntil([&] { return handler.completions > 0 && rig.seen.clientDisconnected; });
        }
        catch (const DisconnectingDownloadHandler::TornDownUnderCaller &) {
            tornDown = true;
        }

        EQUALS(handler.completions, 1);
        EQUALS(tornDown, false);
        EQUALS(handler.receiverSurvived, true);
        // Released once, by FileListTransfer, and not again by the shutdown.
        EQUALS(handler.dereferences, 1);
        EQUALS(rig.seen.clientDisconnected, true);
    });
});
