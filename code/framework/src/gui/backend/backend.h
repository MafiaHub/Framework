#pragma once

namespace Framework::GUI {
    template <typename T>
    class Backend {
      protected:
        T _device;

      public:
        Backend() = default;

        virtual bool Init(T device) = 0;
        virtual bool Shutdown()     = 0;

        bool IsInitialized() const {
            return _device != nullptr;
        }

        T GetDevice() const {
            return _device;
        }

        void SetDevice(T device) {
            _device = device;
        }
    };
} // namespace Framework::GUI
