declare namespace Framework {
    const imports: {
        /**
         * Get the exports object from another resource.
         * Throws if the resource is not found or not running.
         * Importing without a declared dependency produces a warning.
         * @param resourceName Name of the running resource whose exports should be read.
         * @returns An object keyed by export name, or an empty object when none are registered.
         */
        get(resourceName: string): Record<string, any>;
    };
}
