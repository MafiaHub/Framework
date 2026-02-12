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
         * Register a listener for an event.
         * @returns An unsubscribe function.
         */
        on(eventName: string, handler: (...args: any[]) => void): () => void;

        /** Register a one-time listener. */
        once(eventName: string, handler: (...args: any[]) => void): void;

        /** Remove a specific listener. */
        off(eventName: string, handler: (...args: any[]) => void): void;

        /**
         * Emit an event globally. Reserved events cannot be emitted.
         * Resolves when all handlers complete; rejects with
         * `AggregateError` if any handler fails.
         */
        emit(eventName: string, ...args: any[]): Promise<void>;

        /** Emit an event targeting a specific resource's listeners. */
        emitTo(resourceName: string, eventName: string, ...args: any[]): Promise<void>;

        /** Register a resource-local listener. */
        onLocal(eventName: string, handler: (...args: any[]) => void): void;

        /** Emit a resource-local event. */
        emitLocal(eventName: string, ...args: any[]): Promise<void>;

        /** Get the number of global listeners for an event. */
        listenerCount(eventName: string): number;
    };
}
