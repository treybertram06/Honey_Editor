#include "script_loader.h"
#include "Honey/core/log.h"

#if defined(HN_PLATFORM_LINUX) || defined(HN_PLATFORM_MACOS)
    #include <dlfcn.h>
#elif defined(HN_PLATFORM_WINDOWS)
    #include <windows.h>
#endif

namespace Honey {

    ScriptLoader& ScriptLoader::get() {
        static ScriptLoader instance;
        return instance;
    }

    bool ScriptLoader::load_library(const std::string& path) {
        if (m_library_handle) {
            HN_CORE_WARN("Script library already loaded. Unloading first...");
            unload_library();
        }

    #if defined(HN_PLATFORM_LINUX) || defined(HN_PLATFORM_MACOS)
        m_library_handle = dlopen(path.c_str(), RTLD_NOW);
        if (!m_library_handle) {
            HN_CORE_ERROR("Failed to load script library: {0}", dlerror());
            return false;
        }
    #elif defined(HN_PLATFORM_WINDOWS)
        m_library_handle = LoadLibraryA(path.c_str());
        if (!m_library_handle) {
            HN_CORE_ERROR("Failed to load script library: {0}", GetLastError());
            return false;
        }
    #endif

        HN_CORE_INFO("Successfully loaded script library: {0}", path);
        return true;
    }

    void ScriptLoader::unload_library() {
        if (!m_library_handle)
            return;

    #if defined(HN_PLATFORM_LINUX) || defined(HN_PLATFORM_MACOS)
        dlclose(m_library_handle);
    #elif defined(HN_PLATFORM_WINDOWS)
        FreeLibrary((HMODULE)m_library_handle);
    #endif

        HN_CORE_INFO("Unloaded script library.");
        m_library_handle = nullptr;
    }

    bool ScriptLoader::reload_library(const std::string& path) {
        unload_library();
        return load_library(path);
    }

    ScriptLoader::~ScriptLoader() {
        unload_library();
    }

}
