/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

namespace Framework::Networking {
    // RakNet ordering channels by traffic class. A lost ReliableOrdered message holds back every
    // later sequenced message on the same channel until it is retransmitted, so the pose stream gets a
    // channel of its own. RPCs name entities, so they share the construction channel to stay ordered
    // after it.
    //
    // Presentation carries what a game times against a body's drawn pose -- a swing, a block, the
    // ruling on a blow -- so a lost inventory or chat message never holds one back. A project that
    // sends on it keeps every message of one ordered stream there: messages a receiver sequences
    // against each other must never be split across channels.
    enum class Channel : char {
        Transform    = 0,
        State        = 1,
        Assets       = 2,
        Events       = 3,
        Construction = 3,
        VoiceFrames  = 4,
        VoiceControl = 5,
        Presentation = 6,
    };

    constexpr char ToOrderingChannel(Channel channel) {
        return static_cast<char>(channel);
    }

    static_assert(ToOrderingChannel(Channel::Transform) != ToOrderingChannel(Channel::Events));
    static_assert(ToOrderingChannel(Channel::Construction) == ToOrderingChannel(Channel::Events));
    static_assert(ToOrderingChannel(Channel::Transform) != ToOrderingChannel(Channel::VoiceFrames));
    static_assert(ToOrderingChannel(Channel::Transform) != ToOrderingChannel(Channel::Assets));
    static_assert(ToOrderingChannel(Channel::Presentation) != ToOrderingChannel(Channel::Events));
    static_assert(ToOrderingChannel(Channel::Presentation) != ToOrderingChannel(Channel::Transform));
} // namespace Framework::Networking
