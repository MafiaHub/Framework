/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <vector>

namespace Framework::Integrations::Client::UI::Nametags {
    // Wire values are shared with Networking::Replication::NametagComponent and with
    // External::ImGUI::Widgets::NameTagComponent; keep the three in sync.
    enum class Component : uint8_t {
        Name   = 1 << 0,
        Health = 1 << 1,
    };

    inline bool HasComponent(uint8_t components, Component component) {
        return (components & static_cast<uint8_t>(component)) != 0;
    }

    // Distance behaviour, in world units.
    struct Layout {
        float drawDistance = 50.0f;
        float fadeStart    = 0.0f; // <=0 derives 70% of drawDistance
        float nearScale    = 1.35f;
        float farScale     = 0.75f;
        float scaleStart   = 10.0f; // the scale ramp runs nearScale -> farScale between these
        float scaleEnd     = 70.0f;
        float shiftStart   = 15.0f;  // distance where the upward lift starts
        float shiftMax     = 0.026f; // lift at drawDistance, as a fraction of screen height
    };

    // Lengths are fractions of the viewport, never pixels, so one scripting contract stays meaningful
    // whether a mod draws in pixels or in normalised space. Defaults match 1920x1080 pixel metrics.
    // The name sits on a drop shadow, not a filled plate, as MTA:SA and GTA V both draw it.
    struct Appearance {
        float fontHeight       = 0.0148f;    // fraction of screen height (16 px)
        float shadowOffset     = 0.0014f;    // drop shadow, fraction of screen height (1.5 px)
        float healthBarWidth   = 0.026f;     // fraction of screen width (50 px)
        float healthBarHeight  = 0.0037f;    // fraction of screen height (4 px)
        float healthBarGap     = 0.0037f;    // fraction of screen height (4 px)
        float healthBarBorder  = 0.0009f;    // dark border around the bar (1 px)
        uint32_t shadowColor   = 0xD0000000; // 0xAARRGGBB
        uint32_t barTrackColor = 0xC0000000;
    };

    // Offered per ped before the expensive work: ranking runs on these, so projection, bone lookups
    // and occlusion rays are paid for survivors alone.
    struct Candidate {
        uint64_t id         = 0;       // stable identity; a retained backend keys its pool off this
        void *user          = nullptr; // opaque mod payload, handed back untouched
        float distance      = 0.0f;
        uint8_t components  = 0;
        uint32_t color      = 0xFFFFFFFF; // 0xAARRGGBB
        float healthPercent = -1.0f;      // <0 draws no bar
        const char *label   = nullptr;    // borrowed for the frame
    };

    // One tag that survived selection, ordered far to near so a nearer tag paints over a farther one.
    struct Resolved {
        uint64_t id         = 0;
        void *user          = nullptr;
        float distance      = 0.0f;
        float distanceAlpha = 1.0f; // occlusion is the mod's business and multiplies on top
        float scale         = 1.0f;
        float shift         = 0.0f; // upward lift, fraction of screen height
        uint8_t components  = 0;
        uint32_t color      = 0xFFFFFFFF;
        float healthPercent = -1.0f;
        const char *label   = nullptr;
    };

    // Returning false drops a tag before it costs anything: per-viewer rules live here.
    using Filter = std::function<bool(const Candidate &)>;

    struct Config {
        Layout layout;
        Appearance appearance;
        int maxVisible  = 20;   // the nearest N win; a retained backend sizes its pool from this
        bool showHealth = true; // the viewer's own switch
        Filter filter;          // null accepts everything
    };

    // Linear fade: opaque up to fadeStart, gone at drawDistance.
    inline float DistanceAlpha(float distance, const Layout &layout) {
        if (layout.drawDistance <= 0.0f || distance >= layout.drawDistance) {
            return 0.0f;
        }
        float fadeStart = layout.fadeStart;
        if (fadeStart <= 0.0f || fadeStart >= layout.drawDistance) {
            fadeStart = layout.drawDistance * 0.7f;
        }
        if (distance <= fadeStart) {
            return 1.0f;
        }
        return 1.0f - (distance - fadeStart) / (layout.drawDistance - fadeStart);
    }

    inline float DistanceScale(float distance, const Layout &layout) {
        const float span = layout.scaleEnd - layout.scaleStart;
        if (span <= 0.0f) {
            return layout.nearScale;
        }
        const float t = std::clamp((distance - layout.scaleStart) / span, 0.0f, 1.0f);
        return layout.nearScale + (layout.farScale - layout.nearScale) * t;
    }

    inline float DistanceShift(float distance, const Layout &layout) {
        const float span = layout.drawDistance - layout.shiftStart;
        if (span <= 0.0f) {
            return 0.0f;
        }
        return std::clamp((distance - layout.shiftStart) / span, 0.0f, 1.0f) * layout.shiftMax;
    }

    inline uint32_t ModulateAlpha(uint32_t color, float alpha) {
        alpha              = std::clamp(alpha, 0.0f, 1.0f);
        const uint32_t out = static_cast<uint32_t>(static_cast<float>((color >> 24) & 0xFF) * alpha);
        return (color & 0x00FFFFFF) | (out << 24);
    }

    // Gathers candidates, drops what cannot draw, ranks by distance and caps the result. Renderer free
    // by construction: it never sees a screen position, a texture or a draw list.
    class List final {
      public:
        // Borrowed, not copied: it holds a std::function and this runs every frame. The caller owns it
        // and must outlive the frame.
        void Begin(const Config &config) {
            _config = &config;
            _entries.clear();
        }

        // False when the candidate cannot draw at all; the caller then skips its expensive work.
        bool Add(const Candidate &candidate) {
            if (!candidate.label || !candidate.label[0] || !HasComponent(candidate.components, Component::Name)) {
                return false;
            }
            const Config &config = GetConfig();
            const float alpha    = DistanceAlpha(candidate.distance, config.layout);
            if (alpha <= 0.0f) {
                return false;
            }
            if (config.filter && !config.filter(candidate)) {
                return false;
            }

            Resolved resolved;
            resolved.id            = candidate.id;
            resolved.user          = candidate.user;
            resolved.distance      = candidate.distance;
            resolved.distanceAlpha = alpha;
            resolved.scale         = DistanceScale(candidate.distance, config.layout);
            resolved.shift         = DistanceShift(candidate.distance, config.layout);
            resolved.components    = candidate.components;
            resolved.color         = candidate.color;
            resolved.healthPercent = config.showHealth && HasComponent(candidate.components, Component::Health) ? candidate.healthPercent : -1.0f;
            resolved.label         = candidate.label;
            _entries.push_back(resolved);
            return true;
        }

        // A tag visible last frame ranks as slightly nearer, so peds at a similar distance cannot swap
        // in and out of the cap every frame.
        const std::vector<Resolved> &Resolve() {
            const int maxVisible = GetConfig().maxVisible;
            const size_t cap     = maxVisible > 0 ? static_cast<size_t>(maxVisible) : _entries.size();
            if (_entries.size() > cap) {
                std::partial_sort(_entries.begin(), _entries.begin() + cap, _entries.end(), [this](const Resolved &lhs, const Resolved &rhs) {
                    return EffectiveDistance(lhs) < EffectiveDistance(rhs);
                });
                _entries.resize(cap);
            }

            // Far to near: a nearer tag is submitted last and therefore paints over a farther one.
            std::sort(_entries.begin(), _entries.end(), [](const Resolved &lhs, const Resolved &rhs) {
                return lhs.distance > rhs.distance;
            });

            _previous.clear();
            _previous.reserve(_entries.size());
            for (const Resolved &entry : _entries) {
                _previous.push_back(entry.id);
            }
            return _entries;
        }

        // Valid only between Begin and the end of the frame; defaults before the first Begin.
        const Config &GetConfig() const {
            static const Config kFallback;
            return _config ? *_config : kFallback;
        }

      private:
        static constexpr float kStickyBonus = 0.9f; // how strongly a visible tag defends its slot

        float EffectiveDistance(const Resolved &entry) const {
            return std::find(_previous.begin(), _previous.end(), entry.id) != _previous.end() ? entry.distance * kStickyBonus : entry.distance;
        }

        const Config *_config = nullptr;
        std::vector<Resolved> _entries;
        std::vector<uint64_t> _previous;
    };
} // namespace Framework::Integrations::Client::UI::Nametags
