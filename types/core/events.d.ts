declare namespace Core {
    /**
     * Global event bus with async support.
     *
     * Reserved events (framework-only, cannot be emitted from JS):
     * `resourceStart`, `resourceStop`, `resourceError`,
     * `playerConnect`, `playerDisconnect`, `playerSpawn`,
     * `serverStart`, `serverStop`
     */
    const Events: {
        /**
         * Register a persistent listener owned by the calling resource.
         * @param eventName Case-sensitive shared event name.
         * @param handler Callback invoked with the emitted arguments; asynchronous results are awaited by emitters.
         * @returns An unsubscribe function.
         */
        on(eventName: string, handler: (...args: any[]) => void): () => void;

        /**
         * Register a listener removed before its first invocation.
         * @param eventName Case-sensitive shared event name.
         * @param handler Resource-owned callback invoked once.
         */
        once(eventName: string, handler: (...args: any[]) => void): void;

        /**
         * Remove a matching listener owned by the calling resource.
         * @param eventName Case-sensitive shared event name.
         * @param handler Exact function passed to `on` or `once`.
         */
        off(eventName: string, handler: (...args: any[]) => void): void;

        /**
         * Emit an event globally. Reserved events cannot be emitted.
         * Resolves when all handlers complete; rejects with
         * `AggregateError` if any handler fails.
         * @param eventName Shared event name; framework-reserved names cannot be emitted by scripts.
         * @param args Arguments delivered to every matching handler.
         */
        emit(eventName: string, ...args: any[]): Promise<void>;

        /**
         * Emit an event targeting listeners owned by one resource.
         * @param resourceName Destination running resource.
         * @param eventName Shared event name.
         * @param args Arguments delivered to matching destination handlers.
         */
        emitTo(resourceName: string, eventName: string, ...args: any[]): Promise<void>;

        /**
         * Register a listener in the calling resource's private event namespace.
         * @param eventName Private event name.
         * @param handler Callback invoked only by the same resource's `emitLocal` calls.
         */
        onLocal(eventName: string, handler: (...args: any[]) => void): void;

        /**
         * Emit within the calling resource's private event namespace.
         * @param eventName Private event name.
         * @param args Arguments delivered to local handlers.
         */
        emitLocal(eventName: string, ...args: any[]): Promise<void>;

        /**
         * Count persistent and one-shot listeners across resources.
         * @param eventName Shared event name to inspect.
         */
        listenerCount(eventName: string): number;
    };
}
