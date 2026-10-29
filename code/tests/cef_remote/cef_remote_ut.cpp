/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#define UNIT_MAX_MODULES 8
#include "unit.h"

#include "gui/remote/channel.h"
#include "gui/remote/frame_buffer.h"
#include "gui/remote/host/remote_provider.h"
#include "gui/remote/protocol.h"
#include "gui/remote/resource_server.h"
#include "gui/resources/memory_provider.h"

#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace {
    using namespace Framework::GUI;
    using namespace Framework::GUI::Remote;

    std::wstring UniqueName(const wchar_t *what) {
        static std::atomic<int> counter = 0;
        return std::wstring(what) + L"-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(++counter);
    }

    // Splits an encoded message back into header and payload, as the channel's reader does.
    template <class T>
    bool RoundTrip(const T &in, T &out) {
        const std::vector<uint8_t> bytes = Protocol::Encode(in);
        Protocol::MessageHeader header;
        if (bytes.size() < Protocol::kHeaderBytes || !Protocol::ParseHeader(bytes.data(), header, Protocol::kMaxClientMessageBytes)) {
            return false;
        }
        if (header.type != static_cast<uint16_t>(T::kType) || header.payloadSize != bytes.size() - Protocol::kHeaderBytes) {
            return false;
        }
        return Protocol::Decode(bytes.data() + Protocol::kHeaderBytes, header.payloadSize, out);
    }

    template <class T>
    std::vector<uint8_t> PayloadOf(const T &message) {
        std::vector<uint8_t> bytes = Protocol::Encode(message);
        bytes.erase(bytes.begin(), bytes.begin() + Protocol::kHeaderBytes);
        return bytes;
    }

    void Paint(std::vector<uint8_t> &pixels, uint32_t width, const DirtyRect &rect, uint32_t seed) {
        for (uint32_t y = rect.y; y < rect.y + rect.height; ++y) {
            for (uint32_t x = rect.x; x < rect.x + rect.width; ++x) {
                const std::size_t offset = (static_cast<std::size_t>(y) * width + x) * 4;
                pixels[offset + 0]       = static_cast<uint8_t>(seed + x);
                pixels[offset + 1]       = static_cast<uint8_t>(seed * 3 + y);
                pixels[offset + 2]       = static_cast<uint8_t>(seed * 7);
                pixels[offset + 3]       = 255;
            }
        }
    }

    // What a client texture does with an Access: take the dirty region only.
    void Apply(std::vector<uint8_t> &texture, uint32_t width, const FrameReader::Access &access) {
        for (uint32_t y = access.dirty.y; y < access.dirty.y + access.dirty.height; ++y) {
            const std::size_t offset = static_cast<std::size_t>(y) * access.stride + static_cast<std::size_t>(access.dirty.x) * 4;
            std::memcpy(texture.data() + (static_cast<std::size_t>(y) * width + access.dirty.x) * 4, access.pixels + offset, static_cast<std::size_t>(access.dirty.width) * 4);
        }
    }

    // A listening (client) and a connecting (host) channel on one fresh pipe, each with the limits
    // its side of the real pair has.
    struct ChannelPair {
        Channel client {Protocol::kMaxHostMessageBytes, Protocol::kMaxClientMessageBytes};
        Channel host {Protocol::kMaxClientMessageBytes, Protocol::kMaxHostMessageBytes};

        bool Connect() {
            const std::wstring name = L"\\\\.\\pipe\\" + UniqueName(L"fwcef-ut");
            if (!client.Listen(name)) {
                return false;
            }
            bool connected = false;
            std::thread connector([&] {
                connected = host.Connect(name, 5000);
            });
            Channel::ConnectState state = Channel::ConnectState::Pending;
            for (int i = 0; i < 500 && state == Channel::ConnectState::Pending; ++i) {
                state = client.PollConnect();
                Sleep(10);
            }
            connector.join();
            return connected && state == Channel::ConnectState::Connected;
        }
    };

    std::string Body(std::size_t size) {
        std::string body(size, '\0');
        for (std::size_t i = 0; i < size; ++i) {
            body[i] = static_cast<char>((i * 131 + i / 977) & 0xFF);
        }
        return body;
    }

    // The fw:// path end to end, minus CEF: the host's broker on one side of a pipe, the client's
    // server and a memory root on the other.
    struct ResourcePath {
        ChannelPair pair;
        std::shared_ptr<Resources::MemoryProvider> root = std::make_shared<Resources::MemoryProvider>("ut");
        ResourceServer server {pair.client, [this](const std::string &host) -> std::shared_ptr<Resources::ResourceProvider> {
                                   return host == "ut" ? root : nullptr;
                               }};
        std::shared_ptr<ResourceBroker> broker = std::make_shared<ResourceBroker>(pair.host);
        // Body bytes that reached the host, for every request.
        std::atomic<uint64_t> received = 0;

        bool Start() {
            if (!pair.Connect()) {
                return false;
            }
            server.Start();
            pair.client.Start(
                [this](Protocol::MessageType type, std::vector<uint8_t> payload) {
                    switch (type) {
                    case Protocol::MessageType::ResourceRequest: {
                        Protocol::ResourceRequest request;
                        if (Protocol::Decode(payload, request)) {
                            server.OnRequest(std::move(request));
                        }
                        break;
                    }
                    case Protocol::MessageType::ResourceAck: {
                        Protocol::ResourceAck ack;
                        if (Protocol::Decode(payload, ack)) {
                            server.OnAck(ack);
                        }
                        break;
                    }
                    case Protocol::MessageType::ResourceSeek: {
                        Protocol::ResourceSeek seek;
                        if (Protocol::Decode(payload, seek)) {
                            server.OnSeek(seek);
                        }
                        break;
                    }
                    case Protocol::MessageType::ResourceCancel: {
                        Protocol::ResourceCancel cancel;
                        if (Protocol::Decode(payload, cancel)) {
                            server.OnCancel(cancel);
                        }
                        break;
                    }
                    default: break;
                    }
                },
                {});
            pair.host.Start(
                [this](Protocol::MessageType type, std::vector<uint8_t> payload) {
                    switch (type) {
                    case Protocol::MessageType::ResourceResponse: {
                        Protocol::ResourceResponse response;
                        if (Protocol::Decode(payload, response)) {
                            broker->OnResponse(response);
                        }
                        break;
                    }
                    case Protocol::MessageType::ResourceData: {
                        Protocol::ResourceData data;
                        if (Protocol::Decode(payload, data)) {
                            received += data.bytes.size();
                            broker->OnData(data);
                        }
                        break;
                    }
                    case Protocol::MessageType::ResourceEnd: {
                        Protocol::ResourceEnd end;
                        if (Protocol::Decode(payload, end)) {
                            broker->OnEnd(end);
                        }
                        break;
                    }
                    default: break;
                    }
                },
                [this] {
                    broker->FailAll();
                });
            return true;
        }

        ~ResourcePath() {
            pair.host.Close();
            pair.client.Close();
            server.Stop();
        }
    };

    std::string ReadAll(Resources::ResourceStream &stream) {
        std::string out;
        std::vector<char> buffer(64u << 10);
        for (;;) {
            const std::int64_t read = stream.Read(buffer.data(), buffer.size());
            if (read <= 0) {
                return read == 0 ? out : std::string("<failed>");
            }
            out.append(buffer.data(), static_cast<std::size_t>(read));
        }
    }
} // namespace

MODULE(remote_protocol, {
    IT("round-trips every field of a view event", {
        Protocol::ViewEventMessage in;
        in.viewId           = 42;
        in.data.event       = ViewEvent::ResourceBlocked;
        in.data.url         = "fw://ui/index.html";
        in.data.openerUrl   = "fw://ui/opener.html";
        in.data.origin      = "fw://ui";
        in.data.domain      = "ui";
        in.data.description = "net::ERR_ABORTED";
        in.data.tooltip     = "tip";
        in.data.message     = "console says hi";
        in.data.source      = "app.js";
        in.data.errorCode   = -3;
        in.data.cursorType  = 2;
        in.data.line        = 17;
        in.data.severity    = 3;
        in.data.reason      = ViewBlockReason::ForeignEvent;
        in.data.isMainFrame = true;
        in.data.blocked     = true;
        in.data.focused     = true;

        Protocol::ViewEventMessage out;
        EQUALS(RoundTrip(in, out), true);
        EQUALS(out.viewId, 42);
        EQUALS(out.data.event == ViewEvent::ResourceBlocked, true);
        EQUALS(out.data.url == in.data.url && out.data.openerUrl == in.data.openerUrl && out.data.origin == in.data.origin, true);
        EQUALS(out.data.domain == in.data.domain && out.data.description == in.data.description && out.data.tooltip == in.data.tooltip, true);
        EQUALS(out.data.message == in.data.message && out.data.source == in.data.source, true);
        EQUALS(out.data.errorCode, -3);
        EQUALS(out.data.cursorType, 2);
        EQUALS(out.data.line, 17);
        EQUALS(out.data.severity, 3);
        EQUALS(out.data.reason == ViewBlockReason::ForeignEvent, true);
        EQUALS(out.data.isMainFrame && out.data.blocked && out.data.focused, true);
    });

    IT("round-trips key, IME and binary resource messages", {
        Protocol::KeyEvent key;
        key.viewId               = 3;
        key.type                 = 3;
        key.modifiers            = 0x86;
        key.windowsKeyCode       = 0x41;
        key.nativeKeyCode        = 0x001E0001;
        key.isSystemKey          = true;
        key.character            = 0x00E9;
        key.unmodifiedCharacter  = 0x0065;
        key.focusOnEditableField = true;
        Protocol::KeyEvent keyOut;
        EQUALS(RoundTrip(key, keyOut), true);
        EQUALS(keyOut.type == 3 && keyOut.modifiers == 0x86u && keyOut.windowsKeyCode == 0x41 && keyOut.nativeKeyCode == 0x001E0001, true);
        EQUALS(keyOut.isSystemKey && keyOut.character == 0x00E9 && keyOut.unmodifiedCharacter == 0x0065 && keyOut.focusOnEditableField, true);

        Protocol::ImeSetComposition ime;
        ime.viewId         = 9;
        ime.text           = u"日本語";
        ime.selectionStart = 1;
        ime.selectionEnd   = 2;
        Protocol::ImeSetComposition imeOut;
        EQUALS(RoundTrip(ime, imeOut), true);
        EQUALS(imeOut.text == ime.text && imeOut.selectionStart == 1 && imeOut.selectionEnd == 2, true);

        Protocol::ResourceData data;
        data.requestId = 77;
        data.bytes     = std::string("\0\x01\xFF\0zz", 6);
        Protocol::ResourceData dataOut;
        EQUALS(RoundTrip(data, dataOut), true);
        EQUALS(dataOut.requestId, 77u);
        EQUALS(dataOut.bytes == data.bytes, true);

        Protocol::Shutdown shutdown;
        Protocol::Shutdown shutdownOut;
        EQUALS(RoundTrip(shutdown, shutdownOut), true);
    });

    IT("rejects a truncated payload and trailing bytes", {
        Protocol::CreateView create;
        create.viewId                = 1;
        create.url                   = "fw://ui/index.html";
        create.width                 = 800;
        create.height                = 600;
        std::vector<uint8_t> payload = PayloadOf(create);

        Protocol::CreateView out;
        EQUALS(Protocol::Decode(payload, out), true);

        std::vector<uint8_t> shorter(payload.begin(), payload.end() - 1);
        EQUALS(Protocol::Decode(shorter, out), false);

        std::vector<uint8_t> longer = payload;
        longer.push_back(0);
        EQUALS(Protocol::Decode(longer, out), false);
    });

    IT("rejects a string claiming more bytes than the payload holds", {
        std::vector<uint8_t> payload(8, 0);
        const uint32_t huge = 0xFFFFFFF0u;
        std::memcpy(payload.data() + 4, &huge, sizeof(huge));
        Protocol::LoadUrl out;
        EQUALS(Protocol::Decode(payload, out), false);

        Protocol::ImeCommitText ime;
        std::vector<uint8_t> odd(4, 0);
        const uint32_t count = 3;
        odd.insert(odd.end(), reinterpret_cast<const uint8_t *>(&count), reinterpret_cast<const uint8_t *>(&count) + 4);
        odd.insert(odd.end(), 5, 0x41);
        EQUALS(Protocol::Decode(odd, ime), false);
    });

    IT("rejects an out-of-range enum and a bool that is not 0 or 1", {
        Protocol::ViewEventMessage event;
        std::vector<uint8_t> payload = PayloadOf(event);
        payload[4]                   = 200; // event, right after the view id
        Protocol::ViewEventMessage out;
        EQUALS(Protocol::Decode(payload, out), false);
        payload[4] = static_cast<uint8_t>(ViewEvent::Count);
        EQUALS(Protocol::Decode(payload, out), false);
        payload[4] = static_cast<uint8_t>(ViewEvent::OriginChange);
        EQUALS(Protocol::Decode(payload, out), true);

        Protocol::SetFocus focus;
        focus.focused                 = true;
        std::vector<uint8_t> focusRaw = PayloadOf(focus);
        focusRaw.back()               = 2;
        Protocol::SetFocus focusOut;
        EQUALS(Protocol::Decode(focusRaw, focusOut), false);
    });

    IT("refuses a header with an oversized length or reserved bits", {
        uint8_t raw[Protocol::kHeaderBytes] = {};
        Protocol::MessageHeader header;
        const uint32_t size = Protocol::kMaxHostMessageBytes + 1;
        std::memcpy(raw, &size, 4);
        EQUALS(Protocol::ParseHeader(raw, header, Protocol::kMaxHostMessageBytes), false);
        EQUALS(Protocol::ParseHeader(raw, header, Protocol::kMaxClientMessageBytes), true);

        const uint32_t fine = 16;
        std::memcpy(raw, &fine, 4);
        EQUALS(Protocol::ParseHeader(raw, header, Protocol::kMaxHostMessageBytes), true);
        raw[7] = 1;
        EQUALS(Protocol::ParseHeader(raw, header, Protocol::kMaxHostMessageBytes), false);
    });
});

MODULE(remote_frames, {
    constexpr uint32_t kWidth  = 96;
    constexpr uint32_t kHeight = 48;

    IT("hands the first frame over whole", {
        const std::wstring name = FrameSurfaceName(UniqueName(L"ut"), 1, 1);
        FrameWriter writer;
        EQUALS(writer.Create(name, kWidth, kHeight), true);
        std::vector<uint8_t> source(kWidth * kHeight * 4, 0);
        Paint(source, kWidth, {0, 0, kWidth, kHeight}, 1);
        writer.Publish(source.data(), {0, 0, kWidth, kHeight});

        FrameReader reader;
        EQUALS(reader.Open(name), true);
        EQUALS(reader.GetWidth(), kWidth);
        FrameReader::Access access;
        EQUALS(reader.Acquire(0, access), true);
        EQUALS(access.full, true);
        EQUALS(access.seq, 1u);
        EQUALS(access.dirty.width == kWidth && access.dirty.height == kHeight, true);
        EQUALS(std::memcmp(access.pixels, source.data(), source.size()), 0);
        reader.Release();

        // Nothing newer than what the caller holds: no claim, nothing to do.
        EQUALS(reader.Acquire(1, access), false);
    });

    IT("then hands over only what changed, and the union for a reader that fell behind", {
        const std::wstring name = FrameSurfaceName(UniqueName(L"ut"), 1, 1);
        FrameWriter writer;
        EQUALS(writer.Create(name, kWidth, kHeight), true);
        std::vector<uint8_t> source(kWidth * kHeight * 4, 0);
        Paint(source, kWidth, {0, 0, kWidth, kHeight}, 1);
        writer.Publish(source.data(), {0, 0, kWidth, kHeight});

        FrameReader reader;
        EQUALS(reader.Open(name), true);

        const DirtyRect first {10, 5, 8, 4};
        Paint(source, kWidth, first, 2);
        writer.Publish(source.data(), first);
        FrameReader::Access access;
        EQUALS(reader.Acquire(1, access), true);
        EQUALS(access.full, false);
        EQUALS(access.dirty.x == 10 && access.dirty.y == 5 && access.dirty.width == 8 && access.dirty.height == 4, true);
        EQUALS(std::memcmp(access.pixels, source.data(), source.size()), 0);
        reader.Release();

        const DirtyRect a {0, 0, 4, 4};
        const DirtyRect b {50, 30, 10, 10};
        Paint(source, kWidth, a, 3);
        writer.Publish(source.data(), a);
        Paint(source, kWidth, b, 4);
        writer.Publish(source.data(), b);
        EQUALS(reader.Acquire(2, access), true);
        EQUALS(access.seq, 4u);
        EQUALS(access.full, false);
        EQUALS(access.dirty.x == 0 && access.dirty.y == 0 && access.dirty.width == 60 && access.dirty.height == 40, true);
        reader.Release();
    });

    IT("falls back to a whole frame past the history it keeps", {
        const std::wstring name = FrameSurfaceName(UniqueName(L"ut"), 1, 1);
        FrameWriter writer;
        EQUALS(writer.Create(name, kWidth, kHeight), true);
        std::vector<uint8_t> source(kWidth * kHeight * 4, 0);
        for (uint32_t i = 0; i < kFrameHistory + 3; ++i) {
            Paint(source, kWidth, {i % kWidth, 0, 1, 1}, i);
            writer.Publish(source.data(), {i % kWidth, 0, 1, 1});
        }
        FrameReader reader;
        EQUALS(reader.Open(name), true);
        FrameReader::Access access;
        EQUALS(reader.Acquire(1, access), true);
        EQUALS(access.full, true);
        EQUALS(std::memcmp(access.pixels, source.data(), source.size()), 0);
        reader.Release();
    });

    IT("never writes the slot a reader holds", {
        const std::wstring name = FrameSurfaceName(UniqueName(L"ut"), 1, 1);
        FrameWriter writer;
        EQUALS(writer.Create(name, kWidth, kHeight), true);
        std::vector<uint8_t> source(kWidth * kHeight * 4, 0);
        Paint(source, kWidth, {0, 0, kWidth, kHeight}, 9);
        writer.Publish(source.data(), {0, 0, kWidth, kHeight});

        FrameReader reader;
        EQUALS(reader.Open(name), true);
        FrameReader::Access held;
        EQUALS(reader.Acquire(0, held), true);
        const std::vector<uint8_t> snapshot(held.pixels, held.pixels + source.size());

        for (uint32_t i = 0; i < 20; ++i) {
            Paint(source, kWidth, {0, 0, kWidth, kHeight}, 100 + i);
            writer.Publish(source.data(), {0, 0, kWidth, kHeight});
        }
        EQUALS(std::memcmp(held.pixels, snapshot.data(), snapshot.size()), 0);
        reader.Release();

        FrameReader::Access latest;
        EQUALS(reader.Acquire(held.seq, latest), true);
        EQUALS(latest.seq, 21u);
        EQUALS(std::memcmp(latest.pixels, source.data(), source.size()), 0);
        reader.Release();
    });

    IT("keeps a dirty-rect texture identical to the page over random paints", {
        const std::wstring name = FrameSurfaceName(UniqueName(L"ut"), 1, 1);
        FrameWriter writer;
        EQUALS(writer.Create(name, kWidth, kHeight), true);
        FrameReader reader;
        EQUALS(reader.Open(name), true);

        std::mt19937 random(1234);
        std::vector<uint8_t> source(kWidth * kHeight * 4, 0);
        std::vector<uint8_t> texture(source.size(), 0xCD);
        uint32_t held = 0;
        bool match    = true;
        for (uint32_t i = 0; i < 400 && match; ++i) {
            const uint32_t x = random() % kWidth;
            const uint32_t y = random() % kHeight;
            const DirtyRect rect {x, y, 1 + random() % (kWidth - x), 1 + random() % (kHeight - y)};
            Paint(source, kWidth, rect, i);
            writer.Publish(source.data(), rect);

            // A client that renders slower than the page paints.
            if (random() % 3 != 0) {
                continue;
            }
            FrameReader::Access access;
            if (reader.Acquire(held, access)) {
                Apply(texture, kWidth, access);
                held = access.seq;
                reader.Release();
                match = std::memcmp(texture.data(), source.data(), source.size()) == 0;
            }
        }
        EQUALS(match, true);
    });

    IT("refuses a section whose header does not fit its mapping", {
        const std::wstring name = UniqueName(L"Local\\fwcef-ut-bad");
        HANDLE mapping          = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 64 * 1024, name.c_str());
        EQUALS(mapping != nullptr, true);
        auto *header        = static_cast<FrameSurfaceHeader *>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0));
        header->magic       = kFrameMagic;
        header->version     = kFrameVersion;
        header->width       = 1024;
        header->height      = 1024;
        header->stride      = 1024 * 4;
        header->slotCount   = kFrameSlots;
        header->pixelOffset = kFramePixelOffset;
        header->slotBytes   = 1024 * 1024 * 4;

        FrameReader reader;
        const bool opened = reader.Open(name);
        header->magic     = 0;
        UnmapViewOfFile(header);
        CloseHandle(mapping);
        EQUALS(opened, false);
    });
});

MODULE(remote_channel, {
    IT("carries messages both ways, large ones included, and knows its peer", {
        ChannelPair pair;
        EQUALS(pair.Connect(), true);
        Channel &server = pair.client;
        Channel &client = pair.host;
        EQUALS(server.GetPeerProcessId(), GetCurrentProcessId());

        std::mutex mutex;
        std::vector<std::pair<Protocol::MessageType, std::vector<uint8_t>>> atServer;
        std::vector<std::pair<Protocol::MessageType, std::vector<uint8_t>>> atClient;
        std::atomic<bool> clientClosed = false;
        server.Start(
            [&](Protocol::MessageType type, std::vector<uint8_t> payload) {
                std::scoped_lock lock(mutex);
                atServer.emplace_back(type, std::move(payload));
            },
            {});
        client.Start(
            [&](Protocol::MessageType type, std::vector<uint8_t> payload) {
                std::scoped_lock lock(mutex);
                atClient.emplace_back(type, std::move(payload));
            },
            [&] {
                clientClosed = true;
            });

        Protocol::EvaluateScript script;
        script.viewId = 5;
        script.script = std::string(3u << 20, 'x');
        EQUALS(server.Send(script), true);
        Protocol::Hello hello;
        EQUALS(client.Send(hello), true);
        for (int i = 0; i < 100; ++i) {
            Protocol::BeginFrame frame;
            frame.viewId = i;
            server.Send(frame);
        }

        // Over what the client takes from the host: refused, and the channel stays up.
        Protocol::PageEvent tooLarge;
        tooLarge.payload = std::string(Protocol::kMaxHostMessageBytes, 'y');
        EQUALS(client.Send(tooLarge), false);
        EQUALS(client.IsOpen(), true);

        bool delivered = false;
        for (int i = 0; i < 500 && !delivered; ++i) {
            {
                std::scoped_lock lock(mutex);
                delivered = atClient.size() == 101 && atServer.size() == 1;
            }
            Sleep(10);
        }
        EQUALS(delivered, true);

        Protocol::EvaluateScript scriptOut;
        EQUALS(atClient[0].first == Protocol::MessageType::EvaluateScript && Protocol::Decode(atClient[0].second, scriptOut), true);
        EQUALS(scriptOut.script.size(), script.script.size());
        Protocol::BeginFrame last;
        EQUALS(Protocol::Decode(atClient[100].second, last), true);
        EQUALS(last.viewId, 99);
        Protocol::Hello helloOut;
        EQUALS(Protocol::Decode(atServer[0].second, helloOut), true);
        EQUALS(helloOut.version, Protocol::kVersion);

        // The peer going away closes the other end.
        server.Close();
        for (int i = 0; i < 300 && !clientClosed; ++i) {
            Sleep(10);
        }
        EQUALS(clientClosed.load(), true);
        EQUALS(client.Send(hello), false);
    });

    IT("ends the channel on a message over the receiver's limit", {
        const std::wstring name = L"\\\\.\\pipe\\" + UniqueName(L"fwcef-ut");
        Channel receiver {1024, 1024};
        Channel sender {1024, 4096};
        EQUALS(receiver.Listen(name), true);
        std::thread connector([&] {
            sender.Connect(name, 5000);
        });
        Channel::ConnectState state = Channel::ConnectState::Pending;
        for (int i = 0; i < 500 && state == Channel::ConnectState::Pending; ++i) {
            state = receiver.PollConnect();
            Sleep(10);
        }
        connector.join();
        EQUALS(state == Channel::ConnectState::Connected, true);

        std::atomic<bool> closed  = false;
        std::atomic<int> messages = 0;
        receiver.Start(
            [&](Protocol::MessageType, std::vector<uint8_t>) {
                ++messages;
            },
            [&] {
                closed = true;
            });
        sender.Start([](Protocol::MessageType, std::vector<uint8_t>) {}, {});

        Protocol::EvaluateScript script;
        script.script = std::string(2048, 'z');
        EQUALS(sender.Send(script), true);
        for (int i = 0; i < 300 && !closed; ++i) {
            Sleep(10);
        }
        EQUALS(closed.load(), true);
        EQUALS(messages.load(), 0);
    });
});

MODULE(remote_resources, {
    IT("streams a body many windows long intact", {
        ResourcePath path;
        EQUALS(path.Start(), true);
        const std::string body = Body(Protocol::kResourceWindowBytes * 5 + 12345);
        path.root->Set("big.bin", body, "application/octet-stream");

        Resources::ResourceStat stat;
        auto stream = path.broker->Open("ut", "big.bin", stat);
        EQUALS(stream != nullptr, true);
        EQUALS(stat.size, static_cast<uint64_t>(body.size()));
        EQUALS(stat.mimeType, std::string("application/octet-stream"));
        EQUALS(ReadAll(*stream) == body, true);

        Resources::ResourceStat missingStat;
        EQUALS(path.broker->Open("ut", "missing.bin", missingStat) == nullptr, true);
        EQUALS(path.broker->Open("ut", "../big.bin", missingStat) == nullptr, true);
        EQUALS(path.broker->Open("other", "big.bin", missingStat) == nullptr, true);
    });

    IT("sends no more than a window ahead of what the host has read", {
        ResourcePath path;
        EQUALS(path.Start(), true);
        const std::string body = Body(Protocol::kResourceWindowBytes * 8);
        path.root->Set("big.bin", body);

        Resources::ResourceStat stat;
        auto stream = path.broker->Open("ut", "big.bin", stat);
        EQUALS(stream != nullptr, true);
        Sleep(300);
        EQUALS(path.received.load(), static_cast<uint64_t>(Protocol::kResourceWindowBytes));

        std::vector<char> buffer(1000);
        EQUALS(stream->Read(buffer.data(), buffer.size()), 1000);
        Sleep(300);
        // One chunk taken off the queue: its room is sent again, and no more.
        EQUALS(path.received.load(), static_cast<uint64_t>(Protocol::kResourceWindowBytes + Protocol::kResourceChunkBytes));
    });

    IT("does not let a stalled body hold up another request", {
        ResourcePath path;
        EQUALS(path.Start(), true);
        const std::string large = Body(Protocol::kResourceWindowBytes * 8);
        const std::string small = Body(5000);
        path.root->Set("large.bin", large);
        path.root->Set("small.bin", small);

        std::vector<std::unique_ptr<Resources::ResourceStream>> stalled;
        for (int i = 0; i < 6; ++i) {
            Resources::ResourceStat stat;
            stalled.push_back(path.broker->Open("ut", "large.bin", stat));
            EQUALS(stalled.back() != nullptr, true);
        }
        Resources::ResourceStat stat;
        auto stream = path.broker->Open("ut", "small.bin", stat);
        EQUALS(stream != nullptr, true);
        EQUALS(ReadAll(*stream) == small, true);
    });

    IT("skips by seeking the client instead of reading through", {
        ResourcePath path;
        EQUALS(path.Start(), true);
        const std::string body = Body(Protocol::kResourceWindowBytes * 6);
        path.root->Set("media.bin", body);

        Resources::ResourceStat stat;
        auto stream = path.broker->Open("ut", "media.bin", stat);
        EQUALS(stream != nullptr, true);

        std::vector<char> head(10);
        EQUALS(stream->Read(head.data(), head.size()), 10);
        EQUALS(std::memcmp(head.data(), body.data(), head.size()), 0);

        // Inside the chunk already here, then far past everything sent so far.
        EQUALS(stream->Skip(100), 100);
        EQUALS(stream->Read(head.data(), head.size()), 10);
        EQUALS(std::memcmp(head.data(), body.data() + 110, head.size()), 0);

        const std::int64_t ahead = static_cast<std::int64_t>(Protocol::kResourceWindowBytes) * 4;
        EQUALS(stream->Skip(ahead), ahead);
        const std::size_t at   = static_cast<std::size_t>(120 + ahead);
        const std::string tail = ReadAll(*stream);
        EQUALS(tail == body.substr(at), true);
        EQUALS(path.received.load() < body.size(), true);

        // Past the end stops at the end.
        Resources::ResourceStat again;
        auto clipped = path.broker->Open("ut", "media.bin", again);
        EQUALS(clipped != nullptr, true);
        EQUALS(clipped->Skip(static_cast<std::int64_t>(body.size()) + 100), static_cast<std::int64_t>(body.size()));
        std::vector<char> one(1);
        EQUALS(clipped->Read(one.data(), one.size()), 0);
    });

    IT("fails every waiting request when the channel goes", {
        ResourcePath path;
        EQUALS(path.Start(), true);
        path.root->Set("big.bin", Body(Protocol::kResourceWindowBytes * 4));
        Resources::ResourceStat stat;
        auto stream = path.broker->Open("ut", "big.bin", stat);
        EQUALS(stream != nullptr, true);
        path.pair.client.Close();
        std::vector<char> buffer(Protocol::kResourceWindowBytes * 4);
        std::int64_t read = 1;
        while (read > 0) {
            read = stream->Read(buffer.data(), buffer.size());
        }
        EQUALS(read, -1);
    });
});

int main() {
    UNIT_CREATE("FrameworkCefRemoteTests");
    UNIT_MODULE(remote_protocol);
    UNIT_MODULE(remote_frames);
    UNIT_MODULE(remote_channel);
    UNIT_MODULE(remote_resources);
    return UNIT_RUN();
}
