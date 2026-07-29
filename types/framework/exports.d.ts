declare const Exports: {
    /**
     * Register a value or function from the calling resource.
     * @param name Export name, preferably declared in the resource manifest.
     * @param value JavaScript value retained for dependent resources.
     * @returns `true` when registration succeeds.
     */
    register(name: string, value: any): boolean;

    /**
     * Get a specific export from another resource.
     * Throws if the resource is not found, not running, or the export
     * does not exist.
     * @param resourceName Name of the running resource that owns the export.
     * @param exportName Registered export name.
     * @returns The exported value with its original JavaScript identity.
     */
    get(resourceName: string, exportName: string): any;
};
