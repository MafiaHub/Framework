#pragma once

namespace Framework::GUI {
    template <typename TDevice, typename TContext>
    class Backend {
      protected:
        TDevice _device;
        TContext _context;

      public:
        Backend() = default;

        virtual bool Init(TDevice device, TContext context) = 0;
        virtual bool Shutdown()     = 0;

        bool IsInitialized() const {
            return _device != nullptr;
        }

        TDevice GetDevice() const {
            return _device;
        }

        void SetDevice(TDevice device) {
            _device = device;
        }

        TContext GetContext() const {
            return _context;
        }

        void SetContext(TContext ctx) {
            _context = ctx;
        }
    };
} // namespace Framework::GUI
