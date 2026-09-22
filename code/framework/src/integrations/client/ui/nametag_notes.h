/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace Framework::Integrations::Client::UI::Nametags {
    // A transient line carried on one entity's nametag: speech, an emote, a status. Purely local --
    // the viewer's own scripts write it, it is never replicated, and two viewers need not agree on it.
    // The replicated half of a tag is NametagState.
    struct Note {
        std::string text;
        uint32_t color = 0; // 0xAARRGGBB; 0 draws it in the tag's own colour
        std::chrono::steady_clock::time_point expiry {};
        bool timed = false;
    };

    // Game thread only, and unlocked because of it: the scripting module is pumped from
    // Instance::Update, so a script writes a note on the thread that reads it -- which is also what
    // lets a nametag pass borrow a note's text for the frame.
    class NoteStore final {
      public:
        // durationMs <= 0 holds until cleared. Empty text clears, so a script need not special-case it.
        void Set(uint64_t id, std::string text, float durationMs, uint32_t color) {
            if (text.empty()) {
                _notes.erase(id);
                return;
            }
            Note note;
            note.text   = std::move(text);
            note.color  = color;
            note.timed  = durationMs > 0.0f;
            note.expiry = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<int64_t>(durationMs));
            _notes[id]  = std::move(note);
        }

        void Clear(uint64_t id) {
            _notes.erase(id);
        }

        void ClearAll() {
            _notes.clear();
        }

        // Once per frame, before the first Find.
        void Expire() {
            if (_notes.empty()) {
                return;
            }
            const auto now = std::chrono::steady_clock::now();
            std::erase_if(_notes, [now](const auto &entry) {
                return entry.second.timed && entry.second.expiry <= now;
            });
        }

        // Borrowed for the frame; null when the entity carries no live note.
        const Note *Find(uint64_t id) const {
            const auto it = _notes.find(id);
            return it != _notes.end() ? &it->second : nullptr;
        }

        bool Empty() const {
            return _notes.empty();
        }

      private:
        std::unordered_map<uint64_t, Note> _notes;
    };

    // The client's one store, shared by the Nametags builtin and the game's nametag pass.
    inline NoteStore &Notes() {
        static NoteStore store;
        return store;
    }
} // namespace Framework::Integrations::Client::UI::Nametags
