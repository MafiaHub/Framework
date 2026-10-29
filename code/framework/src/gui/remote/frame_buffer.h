/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <utils/safe_win32.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <string>

// One view's frames, in a named section the CEF host writes and the client reads. The section is a
// FrameSurfaceHeader followed by kFrameSlots full BGRA frames. The host refreshes a slot the client
// is not reading with the rects that changed since that slot was last written, then publishes it;
// the client copies into its texture only the union of the rects since the frame it already has,
// which the header keeps a short history of. A 32-bit client and a 64-bit host map the same
// layout, so it holds only fixed-width fields.
namespace Framework::GUI::Remote {
    struct DirtyRect {
        uint32_t x      = 0;
        uint32_t y      = 0;
        uint32_t width  = 0;
        uint32_t height = 0;

        bool IsEmpty() const {
            return width == 0 || height == 0;
        }

        // Clipped to a width x height surface.
        DirtyRect Clamped(uint32_t surfaceWidth, uint32_t surfaceHeight) const;
        DirtyRect Union(const DirtyRect &other) const;
    };

    inline constexpr uint32_t kFrameMagic   = 0x46435746; // "FWCF"
    inline constexpr uint32_t kFrameVersion = 1;
    inline constexpr uint32_t kFrameSlots   = 3;
    // The host skips the newest slot and the one being read, and still has one to write.
    static_assert(kFrameSlots >= 3);
    // Frames the client may fall behind and still upload only what changed.
    inline constexpr uint32_t kFrameHistory = 32;
    inline constexpr uint32_t kNoFrameSlot  = 0xFFFFFFFFu;

    static_assert(sizeof(std::atomic<uint32_t>) == sizeof(uint32_t) && std::atomic<uint32_t>::is_always_lock_free);

    struct FrameSlotInfo {
        // Frame the slot holds; 0 while it holds none.
        std::atomic<uint32_t> seq;
        uint32_t reserved;
    };

    // Written as a seqlock: seq is zeroed, the rect written, then seq set.
    struct FrameHistoryEntry {
        std::atomic<uint32_t> seq;
        DirtyRect rect;
    };

    struct FrameSurfaceHeader {
        uint32_t magic;
        uint32_t version;
        uint32_t width;
        uint32_t height;
        uint32_t stride;
        uint32_t slotCount;
        uint32_t pixelOffset;
        uint32_t slotBytes;

        // Newest complete frame, or kNoFrameSlot.
        std::atomic<uint32_t> latestSlot;
        // Slot the client is copying from, or kNoFrameSlot. The host never writes it.
        std::atomic<uint32_t> readerSlot;
        uint32_t reserved[2];

        FrameSlotInfo slots[kFrameSlots];
        FrameHistoryEntry history[kFrameHistory];
    };

    // Same size in both processes, or the pixels would not start where the other side expects.
    static_assert(sizeof(FrameSurfaceHeader) == 48 + kFrameSlots * 8 + kFrameHistory * 20);
    inline constexpr uint32_t kFramePixelOffset = (sizeof(FrameSurfaceHeader) + 63u) & ~63u;

    // Local\ section name both processes derive, so a name never crosses the wire.
    std::wstring FrameSurfaceName(const std::wstring &token, int32_t viewId, uint32_t generation);

    // Host side.
    class FrameWriter final {
      private:
        HANDLE _mapping             = nullptr;
        uint8_t *_base              = nullptr;
        FrameSurfaceHeader *_header = nullptr;
        uint32_t _width             = 0;
        uint32_t _height            = 0;
        uint32_t _seq               = 0;
        std::array<uint32_t, kFrameSlots> _slotSeq {};
        std::array<DirtyRect, kFrameHistory> _history {};

      public:
        FrameWriter() = default;
        ~FrameWriter();

        FrameWriter(const FrameWriter &)            = delete;
        FrameWriter &operator=(const FrameWriter &) = delete;

        bool Create(const std::wstring &name, uint32_t width, uint32_t height);
        void Close();

        bool IsOpen() const {
            return _header != nullptr;
        }

        uint32_t GetWidth() const {
            return _width;
        }

        uint32_t GetHeight() const {
            return _height;
        }

        // |pixels| is a full width x height BGRA surface, |dirty| what changed in it since the
        // last Publish. Only on an open surface.
        void Publish(const uint8_t *pixels, const DirtyRect &dirty);
    };

    // Client side.
    class FrameReader final {
      private:
        HANDLE _mapping             = nullptr;
        uint8_t *_base              = nullptr;
        FrameSurfaceHeader *_header = nullptr;
        uint32_t _width             = 0;
        uint32_t _height            = 0;
        uint32_t _stride            = 0;
        uint32_t _slotBytes         = 0;
        uint32_t _slotCount         = 0;
        bool _claimed               = false;

      public:
        struct Access {
            const uint8_t *pixels = nullptr;
            uint32_t stride       = 0;
            uint32_t seq          = 0;
            // Clamped to the surface; the whole surface when full.
            DirtyRect dirty;
            bool full = false;
        };

        FrameReader() = default;
        ~FrameReader();

        FrameReader(const FrameReader &)            = delete;
        FrameReader &operator=(const FrameReader &) = delete;

        // Maps the section and checks its header against the mapping before trusting anything in it.
        bool Open(const std::wstring &name);
        void Close();

        bool IsOpen() const {
            return _header != nullptr;
        }

        uint32_t GetWidth() const {
            return _width;
        }

        uint32_t GetHeight() const {
            return _height;
        }

        // Claims the newest frame when it is not |sinceSeq|, the frame the caller already holds
        // (0 for none). |out| covers what changed since then. Every true return is paired with
        // Release once the copy is done; the host does not touch the slot until then.
        bool Acquire(uint32_t sinceSeq, Access &out);
        void Release();
    };
} // namespace Framework::GUI::Remote
