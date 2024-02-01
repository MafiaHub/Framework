# JavaScriptCore++
JavaScriptCore++ - A simple C++ 17 wrapper for JavaScriptCore

Build the static library using cmake

# How to use

View the example folder for example code

# Value Lifetime

All classes in this library follow the RAII principle. JavaScriptCore values will be retain for the remainder of the object's lifetime.

When a class (ej. JSValue) is copied, it will keep the same reference to the underlying JavaScriptCore value

All values also store their JSContext, so they can be used elsewhere without mantaining the JSContext around.
