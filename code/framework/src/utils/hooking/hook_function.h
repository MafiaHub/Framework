/*
 * MafiaHub OSS license
 * Copyright (c) 2020, CitizenFX
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <string>

//
// Initialization function that will be called after the game is loaded.
//

class HookFunctionBase {
  private:
    HookFunctionBase *m_next;

  public:
    HookFunctionBase() {
        Register();
    }

    virtual void Run() = 0;

    static void RunAll();
    void Register();
};

class InitFunction: public HookFunctionBase {
  private:
    void (*m_function)();

  public:
    InitFunction(void (*function)()) {
        m_function = function;
    }

    virtual void Run() {
        m_function();
    }
};

class HookFunction: public HookFunctionBase {
  private:
    void (*m_function)();

  public:
    HookFunction(void (*function)()) {
        m_function = function;
    }

    virtual void Run() {
        m_function();
    }
};

class RuntimeHookFunction {
  private:
    void (*m_function)();
    std::string m_key;

    RuntimeHookFunction *m_next;

  public:
    RuntimeHookFunction(const char *key, void (*function)()) {
        m_key      = key;
        m_function = function;

        Register();
    }

    static void Run(const char *key);
    void Register();
};
