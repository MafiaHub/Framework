/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "utils/error.h"
#include "utils/result.h"

#include <string>

MODULE(result, {
    using Framework::Error;
    using Framework::Utils::Result;

    // --- Error ---

    IT("default-constructs Error as the success sentinel", {
        Error e;
        EQUALS(e.message.empty(), true);
        EQUALS(e.code, 0);
    });

    IT("stores Error message and code", {
        Error e("boom", 7);
        STREQUALS(e.message.c_str(), "boom");
        EQUALS(e.code, 7);
    });

    IT("compares Error by message and code", {
        Error a("x", 1);
        Error b("x", 1);
        Error sameMsg("x", 2);
        Error sameCode("y", 1);
        EQUALS(a == b, true);
        EQUALS(a != sameMsg, true);
        EQUALS(a != sameCode, true);
    });

    // --- Result<T, ErrorType> with the default uint32_t error type ---

    IT("carries a value on Ok with the default error type", {
        auto r = Result<int>::Ok(5);
        EQUALS(r.IsOk(), true);
        EQUALS(static_cast<bool>(r), true);
        EQUALS(r.GetValue(), 5);
    });

    IT("carries an error code on Err with the default error type", {
        auto r = Result<int>::Err(42u);
        EQUALS(r.IsOk(), false);
        EQUALS(static_cast<bool>(r), false);
        EQUALS(r.GetError(), 42u);
        EQUALS(r.Equals(42u), true);
    });

    // --- Result<T, Framework::Error> ---

    IT("Ok holds the value and reports success", {
        auto r = Result<int, Error>::Ok(21);
        EQUALS(r.IsOk(), true);
        EQUALS(r.GetValue(), 21);
    });

    IT("Err holds the error and reports failure", {
        auto r = Result<int, Error>::Err(Error("nope", 3));
        EQUALS(r.IsOk(), false);
        EQUALS(r.GetError().code, 3);
        STREQUALS(r.GetError().message.c_str(), "nope");
    });

    IT("constructs implicitly from an Error (the Init return path)", {
        auto make = []() -> Result<int, Error> {
            return Error("implicit");
        };
        auto r = make();
        EQUALS(r.IsOk(), false);
        STREQUALS(r.GetError().message.c_str(), "implicit");
    });

    IT("Map transforms the value when Ok", {
        auto r      = Result<int, Error>::Ok(21);
        auto mapped = r.Map([](int v) {
            return v * 2;
        });
        EQUALS(mapped.IsOk(), true);
        EQUALS(mapped.GetValue(), 42);
    });

    IT("Map can change the value type", {
        auto r      = Result<int, Error>::Ok(7);
        auto mapped = r.Map([](int v) {
            return std::string("n=") + std::to_string(v);
        });
        EQUALS(mapped.IsOk(), true);
        STREQUALS(mapped.GetValue().c_str(), "n=7");
    });

    IT("Map propagates the error unchanged when Err", {
        auto r      = Result<int, Error>::Err(Error("boom", 9));
        auto mapped = r.Map([](int v) {
            return v * 2;
        });
        EQUALS(mapped.IsOk(), false);
        EQUALS(mapped.GetError().code, 9);
        STREQUALS(mapped.GetError().message.c_str(), "boom");
    });

    IT("AndThen chains the next step when Ok", {
        auto r       = Result<int, Error>::Ok(10);
        auto chained = r.AndThen([](int v) {
            return Result<int, Error>::Ok(v + 5);
        });
        EQUALS(chained.IsOk(), true);
        EQUALS(chained.GetValue(), 15);
    });

    IT("AndThen short-circuits without running the step when Err", {
        auto r      = Result<int, Error>::Err(Error("orig"));
        bool called = false;
        auto chained = r.AndThen([&called](int v) {
            called = true;
            return Result<int, Error>::Ok(v);
        });
        EQUALS(called, false);
        EQUALS(chained.IsOk(), false);
        STREQUALS(chained.GetError().message.c_str(), "orig");
    });

    // --- Result<void, Framework::Error> ---

    IT("default-constructs the void result as Ok", {
        Result<void, Error> r;
        EQUALS(r.IsOk(), true);
        EQUALS(static_cast<bool>(r), true);
    });

    IT("void result carries an error on Err", {
        auto make = []() -> Result<void, Error> {
            return Error("failed", 4);
        };
        auto r = make();
        EQUALS(r.IsOk(), false);
        EQUALS(r.GetError().code, 4);
        STREQUALS(r.GetError().message.c_str(), "failed");
    });

    IT("void result returns Ok from an empty brace init", {
        auto make = []() -> Result<void, Error> {
            return {};
        };
        EQUALS(make().IsOk(), true);
    });

    IT("void AndThen runs the next step when Ok", {
        Result<void, Error> r;
        bool ran    = false;
        auto chained = r.AndThen([&ran]() {
            ran = true;
            return Result<void, Error>::Ok();
        });
        EQUALS(ran, true);
        EQUALS(chained.IsOk(), true);
    });

    IT("void AndThen short-circuits when Err", {
        Result<void, Error> r = Error("stop");
        bool ran              = false;
        auto chained = r.AndThen([&ran]() {
            ran = true;
            return Result<void, Error>::Ok();
        });
        EQUALS(ran, false);
        EQUALS(chained.IsOk(), false);
        STREQUALS(chained.GetError().message.c_str(), "stop");
    });
});
