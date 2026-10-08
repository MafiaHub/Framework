/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "utils/url_protocol.h"

#include <filesystem>
#include <string>

MODULE(url_protocol, {
    const auto parse = [](const char *url) {
        return Framework::Utils::UrlProtocol::Parse("m2o", url);
    };

    IT("splits a host and port", {
        const auto parsed = parse("m2o://127.0.0.1:27015");
        EQUALS(parsed.has_value(), true);
        STREQUALS(parsed->host.c_str(), "127.0.0.1");
        EQUALS(parsed->port.value_or(0), 27015);
        EQUALS(parsed->query.empty(), true);
    });

    IT("leaves the port unset when the URL omits one", {
        const auto parsed = parse("m2o://play.example.com");
        EQUALS(parsed.has_value(), true);
        STREQUALS(parsed->host.c_str(), "play.example.com");
        EQUALS(parsed->port.has_value(), false);
    });

    IT("matches the scheme case-insensitively and drops a trailing slash", {
        const auto parsed = parse("M2O://play.example.com:1234/");
        EQUALS(parsed.has_value(), true);
        STREQUALS(parsed->host.c_str(), "play.example.com");
        EQUALS(parsed->port.value_or(0), 1234);
    });

    IT("rejects a different scheme", {
        EQUALS(parse("http://127.0.0.1:27015").has_value(), false);
        EQUALS(parse("m2o:/127.0.0.1").has_value(), false);
        EQUALS(parse("m2o://").has_value(), false);
    });

    IT("rejects a port outside [1, 65535]", {
        EQUALS(parse("m2o://127.0.0.1:0").has_value(), false);
        EQUALS(parse("m2o://127.0.0.1:65536").has_value(), false);
        EQUALS(parse("m2o://127.0.0.1:99999999").has_value(), false);
        EQUALS(parse("m2o://127.0.0.1:abc").has_value(), false);
        EQUALS(parse("m2o://127.0.0.1:27015x").has_value(), false);
    });

    IT("reads query parameters", {
        const auto parsed = parse("m2o://127.0.0.1:27015?nick=Fernando&password=hunter2");
        EQUALS(parsed.has_value(), true);
        EQUALS(parsed->Query("nick").has_value(), true);
        STREQUALS(std::string(*parsed->Query("nick")).c_str(), "Fernando");
        STREQUALS(std::string(*parsed->Query("password")).c_str(), "hunter2");
        EQUALS(parsed->Query("missing").has_value(), false);
    });

    IT("percent-decodes values and reads '+' as a space", {
        const auto parsed = parse("m2o://127.0.0.1:27015/?nick=John%20Doe&tag=a+b");
        EQUALS(parsed.has_value(), true);
        STREQUALS(std::string(*parsed->Query("nick")).c_str(), "John Doe");
        STREQUALS(std::string(*parsed->Query("tag")).c_str(), "a b");
    });

    IT("drops a fragment and keeps the query before it", {
        const auto parsed = parse("m2o://127.0.0.1:27015?nick=Ann#section");
        EQUALS(parsed.has_value(), true);
        STREQUALS(parsed->host.c_str(), "127.0.0.1");
        STREQUALS(std::string(*parsed->Query("nick")).c_str(), "Ann");
    });

    IT("rejects a malformed percent escape", {
        EQUALS(parse("m2o://127.0.0.1:27015?nick=%zz").has_value(), false);
        EQUALS(parse("m2o://127.0.0.1:27015?nick=%4").has_value(), false);
        EQUALS(parse("m2o://127.0.0.1:27015?nick=%").has_value(), false);
    });

    IT("rejects characters ExtractLaunchUrl would have refused", {
        EQUALS(parse("m2o://127.0.0.1:27015?nick=%00").has_value(), false);
        EQUALS(parse("m2o://127.0.0.1:27015?nick=%0A").has_value(), false);
        EQUALS(parse("m2o://127.0.0.1:27015?nick=%22").has_value(), false);
        EQUALS(parse("m2o://127.0.0.1:27015?nick=%5C").has_value(), false);
    });

    IT("skips a query entry with no value separator", {
        const auto parsed = parse("m2o://127.0.0.1:27015?flag&nick=Ann");
        EQUALS(parsed.has_value(), true);
        EQUALS(parsed->query.size(), 1u);
        STREQUALS(std::string(*parsed->Query("nick")).c_str(), "Ann");
    });

    IT("accepts an empty value", {
        const auto parsed = parse("m2o://127.0.0.1:27015?nick=");
        EQUALS(parsed.has_value(), true);
        EQUALS(parsed->Query("nick").has_value(), true);
        EQUALS(parsed->Query("nick")->empty(), true);
    });

    IT("accepts a scheme the shell can key on", {
        EQUALS(Framework::Utils::UrlProtocol::IsValidScheme(L"m2o"), true);
        EQUALS(Framework::Utils::UrlProtocol::IsValidScheme(L"mafia-mp"), true);
        EQUALS(Framework::Utils::UrlProtocol::IsValidScheme(L"m2o+v2.1"), true);
    });

    IT("refuses a scheme that would reach outside its own registry key", {
        EQUALS(Framework::Utils::UrlProtocol::IsValidScheme(L""), false);
        EQUALS(Framework::Utils::UrlProtocol::IsValidScheme(L"2mp"), false);
        EQUALS(Framework::Utils::UrlProtocol::IsValidScheme(L"m2o\\shell\\open\\command"), false);
        EQUALS(Framework::Utils::UrlProtocol::IsValidScheme(L"m2o/open"), false);
        EQUALS(Framework::Utils::UrlProtocol::IsValidScheme(L"m2o://"), false);
        EQUALS(Framework::Utils::UrlProtocol::IsValidScheme(L"m2 o"), false);
    });

    // The registered command and the extractor are one contract: what the shell runs has to be
    // something ExtractLaunchUrl can read the URL back out of.
    IT("builds an open command the launcher can read its URL back from", {
        const std::wstring command = Framework::Utils::UrlProtocol::BuildOpenCommand(std::filesystem::path(L"C:\\Games\\M2O\\M2OLauncher.exe"));
        EQUALS(command == L"\"C:\\Games\\M2O\\M2OLauncher.exe\" \"%1\"", true);

        std::wstring invoked = command;
        invoked.replace(invoked.find(L"%1"), 2, L"m2o://127.0.0.1:27015?nick=Fernando");

        const auto url = Framework::Utils::UrlProtocol::ExtractLaunchUrl(L"m2o", invoked);
        EQUALS(url.has_value(), true);
        EQUALS(*url == L"m2o://127.0.0.1:27015?nick=Fernando", true);
    });
});
