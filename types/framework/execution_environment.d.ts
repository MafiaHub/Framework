/** Runtime-side flags and local release versions. Available on server and client. */
declare const ExecutionEnvironment: {
    /** True in the sandboxed client scripting runtime. */
    readonly isClient: boolean;
    /** True in the authoritative server scripting runtime. */
    readonly isServer: boolean;
    /** Release version of the Framework running this script, not the remote peer. */
    readonly frameworkVersion: string;
    /**
     * Multiplayer mod version supplied by the local application through
     * InstanceOptions.modVersion. Empty when none was supplied. This is not
     * the underlying game's version or the remote peer's version.
     */
    readonly modVersion: string;
};
