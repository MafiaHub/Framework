# How to rename imported DLL library

This is a small tutorial on how to rename imported DLLs. This was once required as we've stumbled upon an issue where our Steam SDK loaded module would clash with game's version.
To ensure both our launcher and the game can co-exist without having to deal with DLL collisions, we rename some DLLs we import ourselves so they can exist within their own space.

Since many DLLs are linked at load-time (The launcher compiles against their import static library), we need to rebuild the library so that it queries for our newly renamed DLL.

1. Dump a DEF file from the original DLL

Load into your developer command prompt and run:
`dumpbin /exports steam_api64.dll > fw_steam_api64.def`

This will dump the entire exports table of our DLL into a new DEF file.

2. Clean up the DEF file

Since this is a dump, we need to strip it of any debug stats or unnecessary information that is not parsable by the lib tool. You can check out [](https://github.com/MafiaHub/Framework/tree/develop/vendors/steamworks/lib/win64/fw_steam_api64.def)
to see how the DEF file should be constructed. Make a note of the LIBRARY entry, this is where we type in the name of our newly renamed DLL.

3. Create the import library

Now that our DEF file is ready, we can ask libtool to generate a new static import library. Run:
`lib /machine:X64 /def:fw_steam_api64.def`

This will generate a pair of .lib and .exp files.

4. Link against the newly generated libs

Self-explanatory, your code should always refer to a new module name now.
