# Coding Style

It is important to have a consistent coding style in our codebase, as it helps us to understand and maintain it. This guide will explain how to contribute to the codebase and how to follow the coding style efficiently. We will also discuss some of the best practices to follow.

Our coding style is loosely based on the [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html). This document will state any deviations from the coding style guidelines.

## Formatting

We use our own [ClangFormat](.clang-format) config to format our code. The **ClangFormat** tool ensures we maintain style consistency and keeps it clean. Ensure your editor supports **ClangFormat** and your code gets formatted when you finish your work. You can find more information about **ClangFormat** in the [ClangFormat documentation](https://clang.llvm.org/docs/ClangFormat.html).

## Header Files

### Header Guard

We use `#pragma once` to ensure that header files are included only once. We do not use the #define preprocessor directive.

```cpp
#pragma once

...
```

### Names and Order of Includes

This is fully automated by **ClangFormat**.

## Scoping

### Namespaces

Your code should always be contained within namespaces. The root namespace should be `namespace Framework {` and the closing brace should be `}`. Inline namespaces are enforced and should represent path to the file. Ensure that you use the correct namespace for your code. The ending brace should contain a comment with the namespace declared. The use of `using namespace` is discouraged. The `using namespace` directive is only used to avoid namespace pollution.

```cpp
namespace Framework::Utils {
    class Time final {
      public:
      ...
    };
} // namespace Framework::Utils
```

### Variables

* `_` prefix is used to denote a private variable.
* Variables are always named with camelCase.
* Local variables should benefit from `auto` type declaration.
* `const` variables should be declared with `constexpr` keyword if possible.

### Classes

* Classes should be named with PascalCase.
* Class names should be short and descriptive.
* Classes should be declared in the header file.
* Classes should be marked as final if possible.
* Purely static classes are discouraged, use global functions encapsulated in a namespace instead.
* The following order of declarations is recommended:
    * Private members.
    * Private methods.
    * Public members. (if any)
    * Public methods.
* The use of `using` keyword to declare type aliases is encouraged.
* Class specific data structures should be declared within the class itself.
