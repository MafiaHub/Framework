#pragma once

#include "networking/build_authentication.h"
#include "networking/network_server.h"

#include <cstring>

MODULE(build_authentication, {
    struct Authentication: Framework::Networking::BuildAuthentication {
        using MafiaNet::TwoWayAuthentication::nonceGenerator;
    };

    IT("keeps challenges for thirty seconds and expires all overdue requests", {
        Framework::Networking::NetworkServer peer;
        Authentication auth;
        auth.SetRakPeerInterface(peer.GetPeer());
        MafiaNet::TwoWayAuthentication::PendingChallenge challenge;
        challenge.identifier   = "build";
        challenge.remoteSystem = MafiaNet::AddressOrGUID(MafiaNet::RakNetGUID(123));
        challenge.time         = 1000;
        challenge.sentHash     = false;
        auth.outgoingChallenges.Push(challenge, _FILE_AND_LINE_);
        auth.outgoingChallenges.Push(challenge, _FILE_AND_LINE_);
        challenge.time = 2000;
        auth.outgoingChallenges.Push(challenge, _FILE_AND_LINE_);
        auth.UpdateAt(30999);
        EQUALS(auth.outgoingChallenges.Size(), 3U);
        auth.UpdateAt(31000);
        EQUALS(auth.outgoingChallenges.Size(), 1U);
        auth.UpdateAt(32000);
        EQUALS(auth.outgoingChallenges.Size(), 0U);
    });

    IT("preserves nonce validity through the challenge window and cleans expired nonces", {
        Authentication auth;
        char nonce[TWO_WAY_AUTHENTICATION_NONCE_LENGTH];
        unsigned short request;
        const MafiaNet::AddressOrGUID remote(MafiaNet::RakNetGUID(123));
        auth.nonceGenerator.GetNonce(nonce, &request, remote);
        auth.nonceGenerator.generatedNonces[0]->whenGenerated = 1000;
        auth.UpdateAt(30999);
        EQUALS(auth.nonceGenerator.generatedNonces.Size(), 1U);
        char received[TWO_WAY_AUTHENTICATION_NONCE_LENGTH];
        EQUALS(auth.nonceGenerator.GetNonceById(received, request, remote, false), true);
        EQUALS(std::memcmp(nonce, received, sizeof(nonce)), 0);
        // A nonce remains tied to the original peer, even during the longer window.
        EQUALS(auth.nonceGenerator.GetNonceById(received, request, MafiaNet::AddressOrGUID(MafiaNet::RakNetGUID(456)), false), false);
        auth.UpdateAt(31000);
        EQUALS(auth.nonceGenerator.generatedNonces.Size(), 0U);
        EQUALS(auth.nonceGenerator.GetNonceById(received, request, remote, false), false);
    });

    IT("retains challenge and nonce cleanup on disconnect and shutdown", {
        Authentication auth;
        const MafiaNet::RakNetGUID guid(123);
        char nonce[TWO_WAY_AUTHENTICATION_NONCE_LENGTH];
        unsigned short request;
        auth.nonceGenerator.GetNonce(nonce, &request, MafiaNet::AddressOrGUID(guid));
        MafiaNet::TwoWayAuthentication::PendingChallenge challenge;
        challenge.remoteSystem = MafiaNet::AddressOrGUID(guid);
        challenge.time         = 1000;
        challenge.sentHash     = false;
        auth.outgoingChallenges.Push(challenge, _FILE_AND_LINE_);
        auth.OnClosedConnection(MafiaNet::UNASSIGNED_SYSTEM_ADDRESS, guid, MafiaNet::LCR_CONNECTION_LOST);
        EQUALS(auth.outgoingChallenges.Size(), 0U);
        EQUALS(auth.nonceGenerator.generatedNonces.Size(), 0U);
        auth.nonceGenerator.GetNonce(nonce, &request, MafiaNet::AddressOrGUID(guid));
        auth.outgoingChallenges.Push(challenge, _FILE_AND_LINE_);
        auth.OnRakPeerShutdown();
        EQUALS(auth.outgoingChallenges.Size(), 0U);
        EQUALS(auth.nonceGenerator.generatedNonces.Size(), 0U);
    });
});
