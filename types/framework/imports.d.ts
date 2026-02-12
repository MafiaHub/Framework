declare namespace Framework {
    const imports: {
        /**
         * Get the exports object from another resource.
         * Throws if the resource is not found or not running.
         */
        get(resourceName: string): Record<string, any>;
    };
}
