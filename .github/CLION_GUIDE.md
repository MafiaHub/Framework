# CLion support

First, make sure to do the steps from [README.md](README.md) so you have a working dev environment!

Drag and drop CMakeLists.txt to CLion project manager to add new cmake-based project, then add necessary profiles (hit the + symbol 3x):
![image](https://user-images.githubusercontent.com/9026786/130327343-8b798668-8cb0-40ff-9a91-a412c283a3c6.png)

In Settings > Plugins, install **EditorConfig** and restart the IDE.

To enable ClangFormat support, install [LLVM bundle](https://llvm.org/builds/) on your system and make sure you can run **clang-format** from terminal. Then, go to *Settings > Editor > Code Style* and check the **Enable ClangFormat** flag.
![image](https://user-images.githubusercontent.com/9026786/130327074-a6dc4439-b86a-4805-a35f-c069c746dca8.png)

Hit **Ctrl+Alt+S** to reformat code or optionally enable reformat on save by going to *Settings > Tools > Actions on Save*:
![image](https://user-images.githubusercontent.com/9026786/130327107-f7961b0c-a9c2-4b3f-9b0b-cd44c730fbb6.png)

Now you have the full power of CLion and CMake at posession.
