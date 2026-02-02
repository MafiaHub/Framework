/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "utils/states/machine.h"
#include <atomic>
#include <chrono>

// Test states
class InitialState: public Framework::Utils::States::IState {
public:
    const char *GetName() const override {
        return "Initial";
    }
    int32_t GetId() const override { return 1; }
    bool OnEnter(Framework::Utils::States::Machine* machine) override { return true; }
    bool OnUpdate(Framework::Utils::States::Machine* machine) override { return false; }
    bool OnExit(Framework::Utils::States::Machine* machine) override { return true; }
};

class ProcessingState: public Framework::Utils::States::IState {
private:
    static std::atomic<int> _counter;
public:
    static void ResetCounter() { _counter = 0; }
    static int GetCounter() { return _counter.load(); }

   const char *GetName() const override {
        return "Processing";
    }
    int32_t GetId() const override { return 2; }
    bool OnEnter(Framework::Utils::States::Machine* machine) override { 
        _counter++; 
        return true; 
    }
    bool OnUpdate(Framework::Utils::States::Machine* machine) override { return false; }
    bool OnExit(Framework::Utils::States::Machine* machine) override { return true; }
};

class FailingState: public Framework::Utils::States::IState {
private:
    static std::atomic<int> _failures;
public:
    static void ResetFailures() { _failures = 0; }
    static int GetFailures() { return _failures.load(); }

    const char *GetName() const override {
        return "Failing";
    }
    int32_t GetId() const override { return 3; }
    bool OnEnter(Framework::Utils::States::Machine* machine) override { return true; }
    bool OnUpdate(Framework::Utils::States::Machine* machine) override { 
        _failures++;
        return true; 
    }
    bool OnExit(Framework::Utils::States::Machine* machine) override { return true; }
};

// Initialize static members
std::atomic<int> ProcessingState::_counter(0);
std::atomic<int> FailingState::_failures(0);

MODULE(state_machine, {
    using namespace Framework::Utils::States;

    IT("can register and transition between states", {
        auto machine = std::make_unique<Machine>();
        machine->RegisterState<InitialState>();

        EQUALS(machine->GetCurrentState(), nullptr);

        bool nextStateResult = machine->RequestNextState(1);
        EQUALS(nextStateResult, true);
        machine->Update();

        auto state = machine->GetCurrentState();
        EQUALS(state != nullptr, true);
        EQUALS(state->GetId(), 1);
        STREQUALS(state->GetName(), "Initial");
    });

    IT("handles invalid state transitions gracefully", {
        auto machine = std::make_unique<Machine>();
        machine->RegisterState<InitialState>();

        bool nextStateResult = machine->RequestNextState(999);
        EQUALS(nextStateResult, false);

        nextStateResult = machine->RequestNextState(1);
        EQUALS(nextStateResult, true);

        nextStateResult = machine->RequestNextState(1);
        EQUALS(nextStateResult, false);
    });

    IT("executes state lifecycle correctly", {
        auto machine = std::make_unique<Machine>();
        ProcessingState::ResetCounter();

        machine->RegisterState<InitialState>();
        machine->RegisterState<ProcessingState>();

        bool nextStateResult = machine->RequestNextState(1);
        EQUALS(nextStateResult, true);
        machine->Update();
        EQUALS(machine->GetCurrentState()->GetId(), 1);

        nextStateResult = machine->RequestNextState(2);
        EQUALS(nextStateResult, true);
        machine->Update();
        machine->Update();
        machine->Update();

        EQUALS(ProcessingState::GetCounter(), 1);
        EQUALS(machine->GetCurrentState()->GetId(), 2);
    });

    IT("handles failing states properly", {
        auto machine = std::make_unique<Machine>();
        FailingState::ResetFailures();

        machine->RegisterState<InitialState>();
        machine->RegisterState<FailingState>();

        bool nextStateResult = machine->RequestNextState(1);
        EQUALS(nextStateResult, true);
        machine->Update();
        machine->Update();

        nextStateResult = machine->RequestNextState(3);
        EQUALS(nextStateResult, true);
        machine->Update();
        machine->Update();
        machine->Update();
        machine->Update();

        EQUALS(FailingState::GetFailures(), 1);
    });

    IT("can handle rapid state transitions", {
        auto machine = std::make_unique<Machine>();
        ProcessingState::ResetCounter();

        machine->RegisterState<InitialState>();
        machine->RegisterState<ProcessingState>();

        for (int i = 0; i < 1000; i++) {
            bool nextStateResult = machine->RequestNextState(1);
            EQUALS(nextStateResult, true);
            machine->Update();
            machine->Update();

            nextStateResult = machine->RequestNextState(2);
            EQUALS(nextStateResult, true);
            machine->Update();
            machine->Update();
            machine->Update();
        }

        EQUALS(ProcessingState::GetCounter(), 1000);
    });

    IT("maintains thread safety under concurrent access", {
        auto machine = std::make_unique<Machine>();
        ProcessingState::ResetCounter();
        std::atomic<bool> running = true;
        std::atomic<int> successfulRequests = 0;

        machine->RegisterState<InitialState>();
        machine->RegisterState<ProcessingState>();

        std::thread updater([&]() {
            while (running) {
                machine->Update();
                std::this_thread::yield();
            }
        });

        std::thread requester([&]() {
            for (int i = 0; i < 100; i++) {
                // In concurrent scenarios, requests may fail if a transition is pending
                // This is expected behavior - we're testing that it doesn't crash
                if (machine->RequestNextState(1)) {
                    successfulRequests++;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));

                if (machine->RequestNextState(2)) {
                    successfulRequests++;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });

        requester.join();
        running = false;
        updater.join();

        // Verify thread safety: some requests succeeded and ProcessingState was entered
        EQUALS(successfulRequests.load() > 0, true);
        EQUALS(ProcessingState::GetCounter() > 0, true);
    });

    IT("handles recursive state updates safely", {
        auto machine = std::make_unique<Machine>();
        ProcessingState::ResetCounter();

        class RecursiveState: public Framework::Utils::States::IState {
          public:
            const char *GetName() const override {
                return "Recursive";
            }
            int32_t GetId() const override {
                return 4;
            }
            bool OnEnter(Machine *machine) override {
                machine->Update();
                return true;
            }
            bool OnUpdate(Machine *machine) override {
                return true;
            }
            bool OnExit(Machine *machine) override {
                return true;
            }
        };

        machine->RegisterState<ProcessingState>();
        machine->RegisterState<RecursiveState>();

        bool nextStateResult = machine->RequestNextState(4);
        EQUALS(nextStateResult, true);
        machine->Update();
        machine->Update();
    });
});

