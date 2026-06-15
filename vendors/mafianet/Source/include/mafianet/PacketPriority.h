/*
 *  Copyright (c) 2014, Oculus VR, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  RakNet License.txt file in the licenses directory of this source tree. An additional grant
 *  of patent rights can be found in the RakNet Patents.txt file in the same directory.
 *
 */

/// \file
/// \brief Scoped enumerations for packet priority and reliability.
///
/// These were historically two unscoped C enums (PacketPriority /
/// PacketReliability) that leaked their enumerators into the global namespace.
/// They are now scoped MafiaNet::Priority / MafiaNet::Reliability enum classes.
/// The enumerator order is preserved, so the underlying integer values are
/// unchanged and remain wire-compatible (reliability is written as a 3-bit
/// field by the reliability layer).



#ifndef __PACKET_PRIORITY_H
#define __PACKET_PRIORITY_H

namespace MafiaNet
{

/// These enumerations are used to describe when packets are delivered.
enum class Priority
{
	/// The highest possible priority. These message trigger sends immediately, and are generally not buffered or aggregated into a single datagram.
	Immediate,

	/// For every 2 Immediate messages, 1 High will be sent.
	/// Messages at this priority and lower are buffered to be sent in groups at 10 millisecond intervals to reduce UDP overhead and better measure congestion control.
	High,

	/// For every 2 High messages, 1 Medium will be sent.
	/// Messages at this priority and lower are buffered to be sent in groups at 10 millisecond intervals to reduce UDP overhead and better measure congestion control.
	Medium,

	/// For every 2 Medium messages, 1 Low will be sent.
	/// Messages at this priority and lower are buffered to be sent in groups at 10 millisecond intervals to reduce UDP overhead and better measure congestion control.
	Low
};

/// Number of distinct priority levels. (Formerly the NUMBER_OF_PRIORITIES sentinel.)
constexpr unsigned int NUMBER_OF_PRIORITIES = 4;

/// These enumerations are used to describe how packets are delivered.
/// \note  Note to self: I write this with 3 bits in the stream.  If I add more remember to change that
/// \note In ReliabilityLayer::WriteToBitStreamFromInternalPacket I assume there are 5 major types
/// \note Do not reorder, I check on >= UnreliableWithAckReceipt
enum class Reliability
{
	/// Same as regular UDP, except that it will also discard duplicate datagrams.  RakNet adds (6 to 17) + 21 bits of overhead, 16 of which is used to detect duplicate packets and 6 to 17 of which is used for message length.
	Unreliable,

	/// Regular UDP with a sequence counter.  Out of order messages will be discarded.
	/// Sequenced and ordered messages sent on the same channel will arrive in the order sent.
	UnreliableSequenced,

	/// The message is sent reliably, but not necessarily in any order.  Same overhead as Unreliable.
	Reliable,

	/// This message is reliable and will arrive in the order you sent it.  Messages will be delayed while waiting for out of order messages.  Same overhead as UnreliableSequenced.
	/// Sequenced and ordered messages sent on the same channel will arrive in the order sent.
	ReliableOrdered,

	/// This message is reliable and will arrive in the sequence you sent it.  Out or order messages will be dropped.  Same overhead as UnreliableSequenced.
	/// Sequenced and ordered messages sent on the same channel will arrive in the order sent.
	ReliableSequenced,

	/// Same as Unreliable, however the user will get either ID_SND_RECEIPT_ACKED or ID_SND_RECEIPT_LOSS based on the result of sending this message when calling RakPeerInterface::Receive(). Bytes 1-4 will contain the number returned from the Send() function. On disconnect or shutdown, all messages not previously acked should be considered lost.
	UnreliableWithAckReceipt,

	// 05/04/10 You can't have sequenced and ack receipts, because you don't know if the other system discarded the message, meaning you don't know if the message was processed
	// UnreliableSequencedWithAckReceipt,

	/// Same as Reliable. The user will also get ID_SND_RECEIPT_ACKED after the message is delivered when calling RakPeerInterface::Receive(). ID_SND_RECEIPT_ACKED is returned when the message arrives, not necessarily the order when it was sent. Bytes 1-4 will contain the number returned from the Send() function. On disconnect or shutdown, all messages not previously acked should be considered lost. This does not return ID_SND_RECEIPT_LOSS.
	ReliableWithAckReceipt,

	/// Same as ReliableOrdered. The user will also get ID_SND_RECEIPT_ACKED after the message is delivered when calling RakPeerInterface::Receive(). ID_SND_RECEIPT_ACKED is returned when the message arrives, not necessarily the order when it was sent. Bytes 1-4 will contain the number returned from the Send() function. On disconnect or shutdown, all messages not previously acked should be considered lost. This does not return ID_SND_RECEIPT_LOSS.
	ReliableOrderedWithAckReceipt

	// 05/04/10 You can't have sequenced and ack receipts, because you don't know if the other system discarded the message, meaning you don't know if the message was processed
	// ReliableSequencedWithAckReceipt,
};

/// Number of distinct reliability types. (Formerly the NUMBER_OF_RELIABILITIES sentinel.)
constexpr unsigned int NUMBER_OF_RELIABILITIES = 8;

} // namespace MafiaNet

#endif
