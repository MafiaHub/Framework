declare namespace Framework {
    const messages: {
        /**
         * Register a handler for incoming messages of a given type.
         * @param handler Receives `(payload, reply)`. Call `reply(value)`
         *   to send a response back to the requester.
         */
        handle(
            messageType: string,
            handler: (payload: any, reply: (value: any) => void) => void,
        ): void;

        /**
         * Send a request to another resource and await a reply.
         * Rejects if the target resource has no handler or the handler throws.
         */
        request(resourceName: string, messageType: string, payload?: any): Promise<any>;

        /** Fire-and-forget message to another resource. */
        send(resourceName: string, messageType: string, payload?: any): void;
    };
}
