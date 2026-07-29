/** Activity verb shown before the app name. Name or the numeric Discord enum. */
type DiscordActivityType = "playing" | "streaming" | "listening" | "watching" | number;

/** Party join privacy. Name or the numeric Discord enum. */
type DiscordPartyPrivacy = "private" | "public" | number;

/** Options accepted by {@link Discord.setPresence}. Every field is optional. */
interface DiscordPresenceOptions {
    /** Activity verb; defaults to `"playing"`. */
    type?: DiscordActivityType;
    /** Activity name. */
    name?: string;
    /** Top line of the presence. */
    details?: string;
    /** Bottom line of the presence. */
    state?: string;
    /** Elapsed-time anchor, unix seconds. `0` clears it. */
    startTimestamp?: number;
    /** Countdown anchor, unix seconds. `0` clears it. */
    endTimestamp?: number;
    /** Asset key of the large image. */
    largeImage?: string;
    /** Tooltip for the large image. */
    largeText?: string;
    /** Asset key of the small image (overlaps the large one). */
    smallImage?: string;
    /** Tooltip for the small image. */
    smallText?: string;
    /** Party grouping — enables Discord "X of Y" and, with a join secret, invites. */
    party?: {
        id?: string;
        /** `[current, max]` party size. */
        size?: [number, number];
        privacy?: DiscordPartyPrivacy;
    };
    /** Invite/spectate secrets (opaque strings your game round-trips). */
    secrets?: {
        match?: string;
        join?: string;
        spectate?: string;
    };
    /** Whether this is an instanced (session) activity. */
    instance?: boolean;
    /** Supported-platform bitmask (Desktop = 1, Android = 2, iOS = 4). */
    supportedPlatforms?: number;
}

/**
 * Client-side Discord rich presence API. **Client only** — not available on
 * the server.
 *
 * Field setters *stage* changes onto a pending activity; nothing is shown
 * until {@link Discord.update} (or the batch {@link Discord.setPresence}) is
 * called. This mirrors Discord's model — an activity is published as one whole
 * object and updates are rate-limited — so compose, then commit once. All
 * calls no-op (setters silently, commits return `false`) when Discord presence
 * is disabled or unavailable.
 *
 * @example
 * // Batch form — apply several fields and publish in one call:
 * Discord.setPresence({
 *     details: "In the city",
 *     state: "Freeroam",
 *     largeImage: "map-city",
 *     startTimestamp: Math.floor(Date.now() / 1000),
 *     party: { size: [4, 32] },
 * });
 *
 * @example
 * // Granular form — stage, then commit:
 * Discord.setDetails("Racing");
 * Discord.setPartySize(2, 8);
 * Discord.update();
 */
declare const Discord: {
    /** Stage the activity {@link DiscordActivityType type}. */
    setType(type: DiscordActivityType): void;
    /** Stage the activity name. */
    setName(name: string): void;
    /** Stage the top line. */
    setDetails(details: string): void;
    /** Stage the bottom line. */
    setState(state: string): void;

    /** Stage the elapsed-time anchor (unix seconds; `0` clears). */
    setStartTimestamp(seconds: number): void;
    /** Stage the countdown anchor (unix seconds; `0` clears). */
    setEndTimestamp(seconds: number): void;

    /** Stage the large image asset key. */
    setLargeImage(assetKey: string): void;
    /** Stage the large image tooltip. */
    setLargeText(text: string): void;
    /** Stage the small image asset key. */
    setSmallImage(assetKey: string): void;
    /** Stage the small image tooltip. */
    setSmallText(text: string): void;
    /** Stage several asset fields at once. */
    setAssets(assets: { largeImage?: string; largeText?: string; smallImage?: string; smallText?: string }): void;

    /** Stage the party id. */
    setPartyId(id: string): void;
    /** Stage the party size (current of max). */
    setPartySize(current: number, max: number): void;
    /** Stage the party join privacy. */
    setPartyPrivacy(privacy: DiscordPartyPrivacy): void;
    /** Stage several party fields at once. */
    setParty(party: { id?: string; size?: [number, number]; privacy?: DiscordPartyPrivacy }): void;

    /** Stage the match secret. */
    setMatchSecret(secret: string): void;
    /** Stage the join secret (enables Ask to Join). */
    setJoinSecret(secret: string): void;
    /** Stage the spectate secret. */
    setSpectateSecret(secret: string): void;
    /** Stage several secret fields at once. */
    setSecrets(secrets: { match?: string; join?: string; spectate?: string }): void;

    /** Stage the instanced flag. */
    setInstance(instance: boolean): void;
    /** Stage the supported-platform bitmask (Desktop = 1, Android = 2, iOS = 4). */
    setSupportedPlatforms(flags: number): void;

    /**
     * Apply a batch of fields and publish immediately (a merge of the matching
     * setters followed by {@link Discord.update}).
     * @returns `true` if the update was dispatched, `false` if unavailable.
     */
    setPresence(options: DiscordPresenceOptions): boolean;

    /**
     * Publish the currently staged activity.
     * @returns `true` if dispatched, `false` if unavailable.
     */
    update(): boolean;

    /**
     * Clear the activity on Discord and reset the staged state.
     * @returns `true` if dispatched, `false` if unavailable.
     */
    clear(): boolean;

    /** Reset the staged state without publishing. */
    reset(): void;

    /** Snowflake of the signed-in Discord user, or `""` until known. */
    getUserId(): string;

    /** Whether the Discord client is connected and presence can be published. */
    isAvailable(): boolean;
};
