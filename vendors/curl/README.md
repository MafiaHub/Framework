### Building
    $ mkdir build

    $ cd build

    $ cmake -G "Visual Studio 17 2022" -A x64 -DBUILD_SHARED_LIBS=OFF -DCURL_STATIC_CRT=ON -DCURL_USE_SCHANNEL=ON -DBUILD_TESTING=OFF -DCMAKE_USE_OPENSSL=OFF -DHTTP_ONLY=ON ..

    $ cmake --build . --config Release
