#pragma once

#include <string>

namespace Framework::Launcher {
    class Project {
      private:
        void *_appHandle = nullptr;
        std::string _handleName;

      public:
        Project(std::string &name): _handleName(name) {};
        ~Project() = default;

        bool Init();

        bool DoInnerPreInit();
        bool DoInnerInit();
        bool DoInnerPostInit();
    };
}; // namespace Framework::Launcher