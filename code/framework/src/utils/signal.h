/*
 * LICENSE
 *
 * Copyright (c) 2017, David Daniel (dd), david@daniels.li
 *
 * handler.hpp is free software copyrighted by David Daniel.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * This program comes with ABSOLUTELY NO WARRANTY.
 * This is free software, and you are welcome to redistribute it
 * under certain conditions.
 */

#pragma once

#include    <functional>
#include    <memory>

namespace Framework::Utils {
    class SignalHandler
    {
        public:
            using Sink = std::function<void (int)>;

            struct Handle;

        public:
            template <typename Callback>
            SignalHandler (Callback&&);

            SignalHandler (Sink&&);
            ~SignalHandler ();

            SignalHandler (const SignalHandler&) = delete;
            SignalHandler& operator= (const SignalHandler&) = delete;

            SignalHandler (SignalHandler&&);
            SignalHandler& operator= (SignalHandler&&);

            SignalHandler& addSignal (int);
            SignalHandler& removeSignal (int);
            bool listensOn (int) const;

        private:
            std::unique_ptr<Handle> handle_;
    };

    template <typename Callback>
    inline SignalHandler::SignalHandler (Callback&& callback) : SignalHandler (Sink {std::forward<Callback> (callback)}){}
} // namespace Framework::Utils