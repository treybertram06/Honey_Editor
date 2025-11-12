#pragma once

#include <string>

namespace Honey {

    class ScriptLoader {
    public:
        static ScriptLoader& get();

        bool load_library(const std::string& path);
        void unload_library();
        bool reload_library(const std::string& path);

        bool is_loaded() const { return m_library_handle != nullptr; }

    private:
        ScriptLoader() = default;
        ~ScriptLoader();

        void* m_library_handle = nullptr;
    };

}
