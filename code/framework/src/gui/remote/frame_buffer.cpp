/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "frame_buffer.h"

#include <algorithm>
#include <cstring>
#include <new>

namespace Framework::GUI::Remote {
    namespace {
        // Bounds what a header may claim, so a corrupt one cannot make the reader walk off its mapping.
        constexpr uint32_t kMaxSurfaceDimension = 16384;

        void CopyRect(uint8_t *dst, const uint8_t *src, uint32_t stride, const DirtyRect &rect) {
            const std::size_t span = static_cast<std::size_t>(rect.width) * 4;
            for (uint32_t row = 0; row < rect.height; ++row) {
                const std::size_t offset = static_cast<std::size_t>(rect.y + row) * stride + static_cast<std::size_t>(rect.x) * 4;
                std::memcpy(dst + offset, src + offset, span);
            }
        }
    } // namespace

    DirtyRect DirtyRect::Clamped(uint32_t surfaceWidth, uint32_t surfaceHeight) const {
        const uint64_t x0 = (std::min)(static_cast<uint64_t>(x), static_cast<uint64_t>(surfaceWidth));
        const uint64_t y0 = (std::min)(static_cast<uint64_t>(y), static_cast<uint64_t>(surfaceHeight));
        const uint64_t x1 = (std::min)(static_cast<uint64_t>(x) + width, static_cast<uint64_t>(surfaceWidth));
        const uint64_t y1 = (std::min)(static_cast<uint64_t>(y) + height, static_cast<uint64_t>(surfaceHeight));
        if (x1 <= x0 || y1 <= y0) {
            return {};
        }
        return {static_cast<uint32_t>(x0), static_cast<uint32_t>(y0), static_cast<uint32_t>(x1 - x0), static_cast<uint32_t>(y1 - y0)};
    }

    DirtyRect DirtyRect::Union(const DirtyRect &other) const {
        if (IsEmpty()) {
            return other;
        }
        if (other.IsEmpty()) {
            return *this;
        }
        const uint32_t x0 = (std::min)(x, other.x);
        const uint32_t y0 = (std::min)(y, other.y);
        const uint32_t x1 = (std::max)(x + width, other.x + other.width);
        const uint32_t y1 = (std::max)(y + height, other.y + other.height);
        return {x0, y0, x1 - x0, y1 - y0};
    }

    std::wstring FrameSurfaceName(const std::wstring &token, int32_t viewId, uint32_t generation) {
        return L"Local\\fwcef-" + token + L"-" + std::to_wstring(viewId) + L"-" + std::to_wstring(generation);
    }

    FrameWriter::~FrameWriter() {
        Close();
    }

    bool FrameWriter::Create(const std::wstring &name, uint32_t width, uint32_t height) {
        Close();
        if (width == 0 || height == 0 || width > kMaxSurfaceDimension || height > kMaxSurfaceDimension) {
            return false;
        }

        const uint64_t slotBytes = static_cast<uint64_t>(width) * 4 * height;
        const uint64_t total     = kFramePixelOffset + slotBytes * kFrameSlots;
        _mapping                 = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, static_cast<DWORD>(total >> 32), static_cast<DWORD>(total & 0xFFFFFFFFu), name.c_str());
        // An existing section under our name is not ours to write into.
        if (!_mapping || GetLastError() == ERROR_ALREADY_EXISTS) {
            Close();
            return false;
        }
        _base = static_cast<uint8_t *>(MapViewOfFile(_mapping, FILE_MAP_ALL_ACCESS, 0, 0, static_cast<SIZE_T>(total)));
        if (!_base) {
            Close();
            return false;
        }

        _header              = new (_base) FrameSurfaceHeader {};
        _header->magic       = kFrameMagic;
        _header->version     = kFrameVersion;
        _header->width       = width;
        _header->height      = height;
        _header->stride      = width * 4;
        _header->slotCount   = kFrameSlots;
        _header->pixelOffset = kFramePixelOffset;
        _header->slotBytes   = static_cast<uint32_t>(slotBytes);
        _header->latestSlot.store(kNoFrameSlot);
        _header->readerSlot.store(kNoFrameSlot);
        for (auto &slot : _header->slots) {
            slot.seq.store(0);
        }
        for (auto &entry : _header->history) {
            entry.seq.store(0);
        }

        _width  = width;
        _height = height;
        _seq    = 0;
        _slotSeq.fill(0);
        return true;
    }

    void FrameWriter::Close() {
        if (_base) {
            UnmapViewOfFile(_base);
        }
        if (_mapping) {
            CloseHandle(_mapping);
        }
        _base    = nullptr;
        _mapping = nullptr;
        _header  = nullptr;
        _width   = 0;
        _height  = 0;
    }

    void FrameWriter::Publish(const uint8_t *pixels, const DirtyRect &dirty) {
        // Any slot but the newest and the one being read. The client re-checks the newest after
        // claiming, so a slot it claims is never one picked here.
        const uint32_t latest = _header->latestSlot.load();
        const uint32_t reader = _header->readerSlot.load();
        uint32_t slot         = kNoFrameSlot;
        for (uint32_t candidate = 0; candidate < kFrameSlots; ++candidate) {
            if (candidate == latest || candidate == reader) {
                continue;
            }
            // The most recently written one needs the least copied.
            if (slot == kNoFrameSlot || _slotSeq[candidate] > _slotSeq[slot]) {
                slot = candidate;
            }
        }

        const uint32_t seq            = _seq + 1;
        const DirtyRect rect          = dirty.Clamped(_width, _height);
        _history[seq % kFrameHistory] = rect;

        auto &entry = _header->history[seq % kFrameHistory];
        entry.seq.store(0);
        std::atomic_thread_fence(std::memory_order_release);
        entry.rect = rect;
        entry.seq.store(seq);

        const uint32_t stride = _width * 4;
        uint8_t *target       = _base + kFramePixelOffset + static_cast<std::size_t>(slot) * _header->slotBytes;
        const uint32_t from   = _slotSeq[slot];
        if (from == 0 || seq - from > kFrameHistory) {
            std::memcpy(target, pixels, static_cast<std::size_t>(stride) * _height);
        }
        else {
            DirtyRect stale;
            for (uint32_t s = from + 1; s <= seq; ++s) {
                stale = stale.Union(_history[s % kFrameHistory]);
            }
            if (!stale.IsEmpty()) {
                CopyRect(target, pixels, stride, stale);
            }
        }

        _header->slots[slot].seq.store(seq);
        _slotSeq[slot] = seq;
        _header->latestSlot.store(slot);
        _seq = seq;
    }

    FrameReader::~FrameReader() {
        Close();
    }

    bool FrameReader::Open(const std::wstring &name) {
        Close();
        _mapping = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, name.c_str());
        if (!_mapping) {
            return false;
        }
        _base = static_cast<uint8_t *>(MapViewOfFile(_mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, 0));
        if (!_base) {
            Close();
            return false;
        }

        MEMORY_BASIC_INFORMATION info {};
        if (VirtualQuery(_base, &info, sizeof(info)) == 0 || info.RegionSize < sizeof(FrameSurfaceHeader)) {
            Close();
            return false;
        }

        // Copy the geometry out once; the host could rewrite the header afterwards, and only these
        // checked copies are used to address the mapping.
        auto *header          = reinterpret_cast<FrameSurfaceHeader *>(_base);
        const uint32_t width  = header->width;
        const uint32_t height = header->height;
        const uint32_t stride = header->stride;
        const uint32_t slots  = header->slotCount;
        const uint32_t offset = header->pixelOffset;
        const uint32_t bytes  = header->slotBytes;
        const bool valid = header->magic == kFrameMagic && header->version == kFrameVersion && width > 0 && height > 0 && width <= kMaxSurfaceDimension && height <= kMaxSurfaceDimension && stride == width * 4 && slots > 0 && slots <= kFrameSlots && offset == kFramePixelOffset
                           && static_cast<uint64_t>(bytes) == static_cast<uint64_t>(stride) * height && static_cast<uint64_t>(offset) + static_cast<uint64_t>(bytes) * slots <= info.RegionSize;
        if (!valid) {
            Close();
            return false;
        }

        _header    = header;
        _width     = width;
        _height    = height;
        _stride    = stride;
        _slotBytes = bytes;
        _slotCount = slots;
        return true;
    }

    void FrameReader::Close() {
        Release();
        if (_base) {
            UnmapViewOfFile(_base);
        }
        if (_mapping) {
            CloseHandle(_mapping);
        }
        _base      = nullptr;
        _mapping   = nullptr;
        _header    = nullptr;
        _width     = 0;
        _height    = 0;
        _stride    = 0;
        _slotBytes = 0;
        _slotCount = 0;
    }

    bool FrameReader::Acquire(uint32_t sinceSeq, Access &out) {
        if (!_header || _claimed) {
            return false;
        }

        // Claim, then confirm the slot is still the newest: if the host published past it in
        // between, it may already be writing there.
        constexpr int kClaimAttempts = 8;
        uint32_t slot                = kNoFrameSlot;
        for (int attempt = 0; attempt < kClaimAttempts; ++attempt) {
            const uint32_t latest = _header->latestSlot.load();
            if (latest >= _slotCount) {
                return false;
            }
            _header->readerSlot.store(latest);
            if (_header->latestSlot.load() == latest) {
                slot = latest;
                break;
            }
        }
        if (slot == kNoFrameSlot) {
            _header->readerSlot.store(kNoFrameSlot);
            return false;
        }

        const uint32_t seq = _header->slots[slot].seq.load();
        if (seq == 0 || seq == sinceSeq) {
            _header->readerSlot.store(kNoFrameSlot);
            return false;
        }

        bool full = sinceSeq == 0 || seq < sinceSeq || seq - sinceSeq > kFrameHistory;
        DirtyRect dirty;
        for (uint32_t s = sinceSeq + 1; !full && s <= seq; ++s) {
            const auto &entry    = _header->history[s % kFrameHistory];
            const uint32_t first = entry.seq.load();
            const DirtyRect rect = entry.rect;
            std::atomic_thread_fence(std::memory_order_acquire);
            if (first != s || entry.seq.load() != s) {
                full = true;
                break;
            }
            dirty = dirty.Union(rect.Clamped(_width, _height));
        }

        out.pixels = _base + kFramePixelOffset + static_cast<std::size_t>(slot) * _slotBytes;
        out.stride = _stride;
        out.seq    = seq;
        out.full   = full;
        out.dirty  = full ? DirtyRect {0, 0, _width, _height} : dirty;
        _claimed   = true;
        return true;
    }

    void FrameReader::Release() {
        if (_claimed && _header) {
            _header->readerSlot.store(kNoFrameSlot);
        }
        _claimed = false;
    }
} // namespace Framework::GUI::Remote
