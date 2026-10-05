/** Runtime-side flags. Available on server and client. */
declare const ExecutionEnvironment: {
    /** True in the sandboxed client scripting runtime. */
    readonly isClient: boolean;
    /** True in the authoritative server scripting runtime. */
    readonly isServer: boolean;
    /** Local Framework release version. */
    readonly frameworkVersion: string;
    /** Local mod version from InstanceOptions.modVersion; empty when unset. */
    readonly modVersion: string;
};
