/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "gui/view_events.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

// Wire format between a client and the CEF host process (gui/remote/host). Free of CEF and of
// Windows so both processes and the tests compile it. Every message is a MessageHeader followed by
// exactly payloadSize bytes of little-endian fields; strings and blobs are a uint32 length and the
// bytes. The peer is another process and is treated as untrusted: Decode rejects a short, long or
// malformed payload, and the receiver range-checks what the field types cannot express.
namespace Framework::GUI::Remote::Protocol {
    // Bumped on any change to a message below; both sides refuse a peer on another version.
    inline constexpr uint32_t kVersion = 2;

    // Largest payload each side takes in one piece. The client is a 32-bit process reading on its
    // own thread, so what the host sends it stays small; scripts it sends the host may be large.
    inline constexpr uint32_t kMaxHostMessageBytes   = 1u << 20;
    inline constexpr uint32_t kMaxClientMessageBytes = 16u << 20;
    inline constexpr int32_t kMaxViewDimension       = 16384;

    // A fw:// body goes out in chunks, at most a window of them ahead of what the host acknowledged.
    inline constexpr uint32_t kResourceChunkBytes  = 256u << 10;
    inline constexpr uint32_t kResourceWindowBytes = 4 * kResourceChunkBytes;

    enum class MessageType : uint16_t {
        // host -> client
        Hello           = 1,
        Ready           = 2,
        ViewEvent       = 3,
        PageEvent       = 4,
        FrameSurface    = 5,
        ResourceRequest = 6,
        ResourceCancel  = 7,
        ResourceAck     = 8,
        ResourceSeek    = 9,

        // client -> host
        CreateView         = 32,
        DestroyView        = 33,
        ResizeView         = 34,
        LoadUrl            = 35,
        LockOrigin         = 36,
        SetFocus           = 37,
        SetAudioMuted      = 38,
        EvaluateScript     = 39,
        BeginFrame         = 40,
        MouseMove          = 41,
        MouseClick         = 42,
        MouseWheel         = 43,
        KeyEvent           = 44,
        ImeSetComposition  = 45,
        ImeCommitText      = 46,
        ImeFinishComposing = 47,
        RegisterRoot       = 48,
        UnregisterRoot     = 49,
        ResourceResponse   = 50,
        ResourceData       = 51,
        ResourceEnd        = 52,
        Shutdown           = 53,
    };

    struct MessageHeader {
        uint32_t payloadSize = 0;
        uint16_t type        = 0;
        uint16_t reserved    = 0;
    };
    inline constexpr std::size_t kHeaderBytes = 8;

    // False for a header no payload should follow: a length over |maxPayload| or a set reserved field.
    bool ParseHeader(const uint8_t *bytes, MessageHeader &header, uint32_t maxPayload);

    class Writer final {
      private:
        std::vector<uint8_t> &_out;

        void Put(const void *data, std::size_t size) {
            const auto *bytes = static_cast<const uint8_t *>(data);
            _out.insert(_out.end(), bytes, bytes + size);
        }

      public:
        explicit Writer(std::vector<uint8_t> &out): _out(out) {}

        template <class T>
            requires std::is_integral_v<T>
        bool operator()(const T &value) {
            if constexpr (std::is_same_v<T, bool>) {
                const uint8_t byte = value ? 1 : 0;
                Put(&byte, 1);
            }
            else {
                Put(&value, sizeof(T));
            }
            return true;
        }

        bool operator()(const std::string &value) {
            const auto size = static_cast<uint32_t>(value.size());
            Put(&size, sizeof(size));
            Put(value.data(), value.size());
            return true;
        }

        bool operator()(const std::u16string &value) {
            const auto count = static_cast<uint32_t>(value.size());
            Put(&count, sizeof(count));
            Put(value.data(), value.size() * sizeof(char16_t));
            return true;
        }

        template <class E>
            requires std::is_enum_v<E>
        bool Enum(const E &value) {
            static_assert(std::is_same_v<std::underlying_type_t<E>, uint8_t>, "enums go on the wire as one byte");
            return (*this)(static_cast<uint8_t>(value));
        }
    };

    class Reader final {
      private:
        const uint8_t *_data;
        std::size_t _size;
        std::size_t _offset = 0;

        bool Take(void *out, std::size_t size) {
            if (size > _size - _offset) {
                return false;
            }
            std::memcpy(out, _data + _offset, size);
            _offset += size;
            return true;
        }

      public:
        Reader(const uint8_t *data, std::size_t size): _data(data), _size(size) {}

        bool AtEnd() const {
            return _offset == _size;
        }

        template <class T>
            requires std::is_integral_v<T>
        bool operator()(T &value) {
            if constexpr (std::is_same_v<T, bool>) {
                uint8_t byte = 0;
                if (!Take(&byte, 1) || byte > 1) {
                    return false;
                }
                value = byte != 0;
                return true;
            }
            else {
                return Take(&value, sizeof(T));
            }
        }

        bool operator()(std::string &value) {
            uint32_t size = 0;
            if (!Take(&size, sizeof(size)) || size > _size - _offset) {
                return false;
            }
            value.assign(reinterpret_cast<const char *>(_data + _offset), size);
            _offset += size;
            return true;
        }

        bool operator()(std::u16string &value) {
            uint32_t count = 0;
            if (!Take(&count, sizeof(count)) || count > (_size - _offset) / sizeof(char16_t)) {
                return false;
            }
            value.resize(count);
            return Take(value.data(), count * sizeof(char16_t));
        }

        // Below the enum's trailing Count sentinel, or the message is malformed.
        template <class E>
            requires std::is_enum_v<E>
        bool Enum(E &value) {
            static_assert(std::is_same_v<std::underlying_type_t<E>, uint8_t>, "enums go on the wire as one byte");
            uint8_t raw = 0;
            if (!(*this)(raw) || raw >= static_cast<uint8_t>(E::Count)) {
                return false;
            }
            value = static_cast<E>(raw);
            return true;
        }
    };

    // Each message lists its fields once in Visit, which serves both directions: Archive is a Writer
    // with a const Self, or a Reader.
    // Who sent it is the pipe's to say (GetNamedPipeClientProcessId), not the message's.
    struct Hello {
        uint32_t version                   = kVersion;
        static constexpr MessageType kType = MessageType::Hello;
        template <class Archive, class Self>
        static bool Visit(Archive &ar, Self &self) {
            return ar(self.version);
        }
    };

    // Sent once CEF is up (ok) or has failed to come up (!ok, error says why).
    struct Ready {
        bool ok = false;
        std::string error;
        static constexpr MessageType kType = MessageType::Ready;
        template <class Archive, class Self>
        static bool Visit(Archive &ar, Self &self) {
            return ar(self.ok) && ar(self.error);
        }
    };

    struct ViewEventMessage {
        int32_t viewId = 0;
        ViewEventData data;
        static constexpr MessageType kType = MessageType::ViewEvent;
        template <class Archive, class Self>
        static bool Visit(Archive &ar, Self &self) {
            auto &d = self.data;
            return ar(self.viewId) && ar.Enum(d.event) && ar(d.url) && ar(d.openerUrl) && ar(d.origin) && ar(d.domain) && ar(d.description) && ar(d.tooltip) && ar(d.message) && ar(d.source) && ar(d.errorCode) && ar(d.cursorType) && ar(d.line) && ar(d.severity)
                   && ar.Enum(d.reason) && ar(d.isMainFrame) && ar(d.blocked) && ar(d.focused);
        }
    };

    // callEvent(name, payload) from the page, already past the host's origin check.
    struct PageEvent {
        int32_t viewId = 0;
        std::string name;
        std::string payload;
        static constexpr MessageType kType = MessageType::PageEvent;
        template <class Archive, class Self>
        static bool Visit(Archive &ar, Self &self) {
            return ar(self.viewId) && ar(self.name) && ar(self.payload);
        }
    };

    // The view's frames now live in the section FrameSurfaceName(token, viewId, generation).
    struct FrameSurface {
        int32_t viewId                     = 0;
        uint32_t generation                = 0;
        int32_t width                      = 0;
        int32_t height                     = 0;
        static constexpr MessageType kType = MessageType::FrameSurface;
        template <class Archive, class Self>
        static bool Visit(Archive &ar, Self &self) {
            return ar(self.viewId) && ar(self.generation) && ar(self.width) && ar(self.height);
        }
    };

    // |path| is what the host's NormalizeResourcePath produced for fw://<host>/...
    struct ResourceRequest {
        uint32_t requestId = 0;
        std::string host;
        std::string path;
        static constexpr MessageType kType = MessageType::ResourceRequest;
        template <class Archive, class Self>
        static bool Visit(Archive &ar, Self &self) {
            return ar(self.requestId) && ar(self.host) && ar(self.path);
        }
    };

    struct ResourceCancel {
        uint32_t requestId                 = 0;
        static constexpr MessageType kType = MessageType::ResourceCancel;
        template <class Archive, class Self>
        static bool Visit(Archive &ar, Self &self) {
            return ar(self.requestId);
        }
    };

    // The host consumed |bytes| more of the body; the client may send that much further.
    struct ResourceAck {
        uint32_t requestId                 = 0;
        uint32_t bytes                     = 0;
        static constexpr MessageType kType = MessageType::ResourceAck;
        template <class Archive, class Self>
        static bool Visit(Archive &ar, Self &self) {
            return ar(self.requestId) && ar(self.bytes);
        }
    };

    // A range request skipped ahead: the body continues at |offset|. Data already sent below it
    // is dropped by the host, so a seek that arrives late is harmless.
    struct ResourceSeek {
        uint32_t requestId                 = 0;
        uint64_t offset                    = 0;
        static constexpr MessageType kType = MessageType::ResourceSeek;
        template <class Archive, class Self>
        static bool Visit(Archive &ar, Self &self) {
            return ar(self.requestId) && ar(self.offset);
        }
    };

    struct CreateView {
        int32_t viewId = 0;
        std::string url;
        int32_t width                      = 0;
        int32_t height                     = 0;
        static constexpr MessageType kType = MessageType::CreateView;
        template <class Archive, class Self>
        static bool Visit(Archive &ar, Self &self) {
            return ar(self.viewId) && ar(self.url) && ar(self.width) && ar(self.height);
        }
    };

    struct DestroyView {
        int32_t viewId                     = 0;
        static constexpr MessageType kType = MessageType::DestroyView;
        template <class Archive, class Self>
        static bool Visit(Archive &ar, Self &self) {
            return ar(self.viewId);
        }
    };

    struct ResizeView {
        int32_t viewId                     = 0;
        int32_t width                      = 0;
        int32_t height                     = 0;
        static constexpr MessageType kType = MessageType::ResizeView;
        template <class Archive, class Self>
        static bool Visit(Archive &ar, Self &self) {
            return ar(self.viewId) && ar(self.width) && ar(self.height);
        }
    };

    struct LoadUrl {
        int32_t viewId = 0;
        std::string url;
        static constexpr MessageType kType = MessageType::LoadUrl;
        template <class Archive, class Self>
        static bool Visit(Archive &ar, Self &self) {
            return ar(self.viewId) && ar(self.url);
        }
    };

    struct LockOrigin {
        int32_t viewId = 0;
        std::string url;
        static constexpr MessageType kType = MessageType::LockOrigin;
        template <class Archive, class Self>
        static bool Visit(Archive &ar, Self &self) {
            return ar(self.viewId) && ar(self.url);
        }
    };

    struct SetFocus {
        int32_t viewId                     = 0;
        bool focused                       = false;
        static constexpr MessageType kType = MessageType::SetFocus;
        template <class Archive, class Self>
        static bool Visit(Archive &ar, Self &self) {
            return ar(self.viewId) && ar(self.focused);
        }
    };

    struct SetAudioMuted {
        int32_t viewId                     = 0;
        bool muted                         = false;
        static constexpr MessageType kType = MessageType::SetAudioMuted;
        template <class Archive, class Self>
        static bool Visit(Archive &ar, Self &self) {
            return ar(self.viewId) && ar(self.muted);
        }
    };

    struct EvaluateScript {
        int32_t viewId = 0;
        std::string script;
        static constexpr MessageType kType = MessageType::EvaluateScript;
        template <class Archive, class Self>
        static bool Visit(Archive &ar, Self &self) {
            return ar(self.viewId) && ar(self.script);
        }
    };

    // One per on-screen view per client tick, so the page paints at the game's cadence.
    struct BeginFrame {
        int32_t viewId                     = 0;
        static constexpr MessageType kType = MessageType::BeginFrame;
        template <class Archive, class Self>
        static bool Visit(Archive &ar, Self &self) {
            return ar(self.viewId);
        }
    };

    // Coordinates are view-relative, modifiers are CEF event flags.
    struct MouseMove {
        int32_t viewId                     = 0;
        int32_t x                          = 0;
        int32_t y                          = 0;
        uint32_t modifiers                 = 0;
        bool mouseLeave                    = false;
        static constexpr MessageType kType = MessageType::MouseMove;
        template <class Archive, class Self>
        static bool Visit(Archive &ar, Self &self) {
            return ar(self.viewId) && ar(self.x) && ar(self.y) && ar(self.modifiers) && ar(self.mouseLeave);
        }
    };

    struct MouseClick {
        int32_t viewId                     = 0;
        int32_t x                          = 0;
        int32_t y                          = 0;
        uint32_t modifiers                 = 0;
        uint8_t button                     = 0; // cef_mouse_button_type_t
        bool mouseUp                       = false;
        int32_t clickCount                 = 1;
        static constexpr MessageType kType = MessageType::MouseClick;
        template <class Archive, class Self>
        static bool Visit(Archive &ar, Self &self) {
            return ar(self.viewId) && ar(self.x) && ar(self.y) && ar(self.modifiers) && ar(self.button) && ar(self.mouseUp) && ar(self.clickCount);
        }
    };

    struct MouseWheel {
        int32_t viewId                     = 0;
        int32_t x                          = 0;
        int32_t y                          = 0;
        uint32_t modifiers                 = 0;
        int32_t deltaX                     = 0;
        int32_t deltaY                     = 0;
        static constexpr MessageType kType = MessageType::MouseWheel;
        template <class Archive, class Self>
        static bool Visit(Archive &ar, Self &self) {
            return ar(self.viewId) && ar(self.x) && ar(self.y) && ar(self.modifiers) && ar(self.deltaX) && ar(self.deltaY);
        }
    };

    // The CefKeyEvent fields View::ProcessKeyboardEvent fills, built in the client where the
    // window, keyboard layout and modifier state are.
    struct KeyEvent {
        int32_t viewId                     = 0;
        uint8_t type                       = 0; // cef_key_event_type_t
        uint32_t modifiers                 = 0;
        int32_t windowsKeyCode             = 0;
        int32_t nativeKeyCode              = 0;
        bool isSystemKey                   = false;
        uint16_t character                 = 0;
        uint16_t unmodifiedCharacter       = 0;
        bool focusOnEditableField          = false;
        static constexpr MessageType kType = MessageType::KeyEvent;
        template <class Archive, class Self>
        static bool Visit(Archive &ar, Self &self) {
            return ar(self.viewId) && ar(self.type) && ar(self.modifiers) && ar(self.windowsKeyCode) && ar(self.nativeKeyCode) && ar(self.isSystemKey) && ar(self.character) && ar(self.unmodifiedCharacter) && ar(self.focusOnEditableField);
        }
    };

    struct ImeSetComposition {
        int32_t viewId = 0;
        std::u16string text;
        int32_t selectionStart             = 0;
        int32_t selectionEnd               = 0;
        static constexpr MessageType kType = MessageType::ImeSetComposition;
        template <class Archive, class Self>
        static bool Visit(Archive &ar, Self &self) {
            return ar(self.viewId) && ar(self.text) && ar(self.selectionStart) && ar(self.selectionEnd);
        }
    };

    struct ImeCommitText {
        int32_t viewId = 0;
        std::u16string text;
        static constexpr MessageType kType = MessageType::ImeCommitText;
        template <class Archive, class Self>
        static bool Visit(Archive &ar, Self &self) {
            return ar(self.viewId) && ar(self.text);
        }
    };

    struct ImeFinishComposing {
        int32_t viewId                     = 0;
        bool keepSelection                 = false;
        static constexpr MessageType kType = MessageType::ImeFinishComposing;
        template <class Archive, class Self>
        static bool Visit(Archive &ar, Self &self) {
            return ar(self.viewId) && ar(self.keepSelection);
        }
    };

    // fw://<host>/ is served by the client; the host forwards each request for it.
    struct RegisterRoot {
        std::string host;
        static constexpr MessageType kType = MessageType::RegisterRoot;
        template <class Archive, class Self>
        static bool Visit(Archive &ar, Self &self) {
            return ar(self.host);
        }
    };

    struct UnregisterRoot {
        std::string host;
        static constexpr MessageType kType = MessageType::UnregisterRoot;
        template <class Archive, class Self>
        static bool Visit(Archive &ar, Self &self) {
            return ar(self.host);
        }
    };

    struct ResourceResponse {
        uint32_t requestId = 0;
        bool found         = false;
        uint64_t size      = 0;
        bool immutable     = false;
        std::string mimeType;
        static constexpr MessageType kType = MessageType::ResourceResponse;
        template <class Archive, class Self>
        static bool Visit(Archive &ar, Self &self) {
            return ar(self.requestId) && ar(self.found) && ar(self.size) && ar(self.immutable) && ar(self.mimeType);
        }
    };

    // |offset| is where |bytes| sit in the body.
    struct ResourceData {
        uint32_t requestId = 0;
        uint64_t offset    = 0;
        std::string bytes;
        static constexpr MessageType kType = MessageType::ResourceData;
        template <class Archive, class Self>
        static bool Visit(Archive &ar, Self &self) {
            return ar(self.requestId) && ar(self.offset) && ar(self.bytes);
        }
    };

    struct ResourceEnd {
        uint32_t requestId                 = 0;
        bool ok                            = false;
        static constexpr MessageType kType = MessageType::ResourceEnd;
        template <class Archive, class Self>
        static bool Visit(Archive &ar, Self &self) {
            return ar(self.requestId) && ar(self.ok);
        }
    };

    struct Shutdown {
        static constexpr MessageType kType = MessageType::Shutdown;
        template <class Archive, class Self>
        static bool Visit(Archive &ar, Self &self) {
            (void)ar;
            (void)self;
            return true;
        }
    };

    // The whole message, header included, ready for Channel::Send.
    template <class T>
    std::vector<uint8_t> Encode(const T &message) {
        std::vector<uint8_t> out(kHeaderBytes);
        Writer writer(out);
        T::Visit(writer, message);

        MessageHeader header;
        header.payloadSize = static_cast<uint32_t>(out.size() - kHeaderBytes);
        header.type        = static_cast<uint16_t>(T::kType);
        std::memcpy(out.data(), &header.payloadSize, sizeof(header.payloadSize));
        std::memcpy(out.data() + 4, &header.type, sizeof(header.type));
        std::memcpy(out.data() + 6, &header.reserved, sizeof(header.reserved));
        return out;
    }

    // The payload must hold exactly the message: a short one and one with trailing bytes both fail.
    template <class T>
    bool Decode(const uint8_t *payload, std::size_t size, T &message) {
        Reader reader(payload, size);
        return T::Visit(reader, message) && reader.AtEnd();
    }

    template <class T>
    bool Decode(const std::vector<uint8_t> &payload, T &message) {
        return Decode(payload.data(), payload.size(), message);
    }
} // namespace Framework::GUI::Remote::Protocol
