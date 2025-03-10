/*
 * MafiaHub OSS license
 * Copyright (c) 2020, CitizenFX
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <string>

#include <logging/logger.h>

//
// Initialization function that will be called after the game is loaded.
//

class HookFunctionBase {
  private:
    HookFunctionBase *_next;

  public:
    ~HookFunctionBase() = default;
    HookFunctionBase() {
        Register();
    }

    virtual void Run() = 0;

    static void RunAll();
    void Register();
};

class InitFunction final: public HookFunctionBase {
  private:
    const char *_name;
    bool _disabled;
    void (*_function)();

  public:
    InitFunction(void (*function)(), const char *name, bool disabled = false): _function(function), _name(name), _disabled(disabled) {
    }

    void Run() override {
        if (!_disabled) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Running module: {}", _name);
            _function();
        }
    }
};

class HookFunction: public HookFunctionBase {
  private:
    void (*_function)();

  public:
    HookFunction(void (*function)()): _function(function) {
    }

    void Run() override {
        _function();
    }
};

class RuntimeHookFunction {
  private:
    void (*_function)();
    std::string _key;

    RuntimeHookFunction *_next;

  public:
    RuntimeHookFunction(const char *key, void (*function)()): _key(key), _function(function) {
        Register();
    }

    static void Run(const char *key);
    void Register();
};
