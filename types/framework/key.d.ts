declare namespace Framework {
    /**
     * Client-side keybinding API. **Client only** — not available on the server.
     *
     * Binds a physical key to a handler that fires on key-down, key-up, or both
     * edges. Binds are resource-owned: they are cleared automatically when the
     * resource stops. Handlers only fire while the game is in the foreground and
     * no UI (chat box, menu, focused web view) is capturing input.
     *
     * Key names are case-insensitive. Recognised names:
     * - Letters `a`–`z`, digits `0`–`9`
     * - Function keys `f1`–`f12`
     * - Arrows `up` `down` `left` `right`
     * - Modifiers `shift` `lshift` `rshift`, `ctrl`/`control` `lctrl` `rctrl`,
     *   `alt` `lalt` `ralt`
     * - Editing `space` `enter`/`return` `escape`/`esc` `tab` `backspace`
     *   `insert` `delete` `home` `end` `pageup` `pagedown` `capslock`
     * - Numpad `numpad0`–`numpad9` (aliases `num0`–`num9`)
     * - Mouse `mouse1` (left) `mouse2` (right) `mouse3` (middle) `mouse4` `mouse5`
     *
     * @example
     * // Toggle a HUD on key-down:
     * Framework.Key.bind("f6", "down", () => toggleHud());
     *
     * // Hold-to-act (fires on both edges); state is "down" then "up":
     * Framework.Key.bind("b", "both", (key, state) => setAiming(state === "down"));
     *
     * // State omitted defaults to "down":
     * Framework.Key.bind("h", () => honk());
     */
    const Key: {
        /**
         * Bind a handler to a key.
         * @param key Case-insensitive key name (see the {@link Key} table).
         * @param state `"down"`, `"up"`, or `"both"`. If omitted, defaults to `"down"`.
         * @param handler Called as `handler(key, state)` where `state` is the actual
         *   edge that fired (`"down"` or `"up"`).
         * @returns `true` once bound. Throws on an unknown key name or bad state.
         */
        bind(key: string, state: "down" | "up" | "both", handler: (key: string, state: "down" | "up") => void): boolean;
        bind(key: string, handler: (key: string, state: "down" | "up") => void): boolean;

        /**
         * Remove binds for a key. With no filters, removes every handler on that key.
         * @param state Optional: only remove binds registered with this state.
         * @param handler Optional: only remove this exact handler function.
         * @returns `true` if at least one bind was removed.
         */
        unbind(key: string, state?: "down" | "up" | "both", handler?: (key: string, state: "down" | "up") => void): boolean;

        /**
         * Live physical state of a key. Returns `false` while UI owns input or the
         * game window is in the background (same gate as bind dispatch).
         */
        isDown(key: string): boolean;
    };
}
