declare namespace Framework {
    const Exports: {
        /** Register a named export from the current resource. */
        register(name: string, value: any): boolean;

        /**
         * Get a specific export from another resource.
         * Throws if the resource is not found, not running, or the export
         * does not exist.
         */
        get(resourceName: string, exportName: string): any;
    };
}
