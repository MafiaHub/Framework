declare const Messages: {
    /**
     * Register or replace a handler owned by the calling resource.
     * @param messageType Message type unique within the receiving resource.
     * @param handler Receives `(payload, reply)`. Call `reply(value)`
     *   once to resolve a request; replies are ignored for fire-and-forget messages.
     */
    handle(
        messageType: string,
        handler: (payload: any, reply: (value: any) => void) => void,
    ): void;

    /**
     * Send a request to another resource and await a reply.
     * Rejects if the target resource has no handler or the handler throws.
     * @param resourceName Destination running resource.
     * @param messageType Handler type registered by the destination.
     * @param payload Optional value delivered to the handler.
     * @returns A promise resolved with the value passed to `reply`.
     */
    request(resourceName: string, messageType: string, payload?: any): Promise<any>;

    /**
     * Send a fire-and-forget message to another local resource.
     * @param resourceName Destination running resource.
     * @param messageType Handler type registered by the destination.
     * @param payload Optional value delivered to the handler.
     */
    send(resourceName: string, messageType: string, payload?: any): void;
};
