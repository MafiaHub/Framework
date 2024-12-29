# Building
    $ cmake -B build -DSENTRY_BACKEND=crashpad -DBUILD_SHARED_LIBS=OFF -DSENTRY_BUILD_TESTS=OFF

    $ cmake --build build --parallel --config Release
    
    $ cmake --install build --prefix install --config Release
