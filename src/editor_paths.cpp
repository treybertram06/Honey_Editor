#include "hnpch.h"
#include "editor_paths.h"

#include "Honey/core/asset_paths.h"

namespace Honey {

    namespace {
        std::filesystem::path find_resources_root() {
            std::filesystem::path candidates[] = {
                AssetPaths::executable_dir() / "resources",
                AssetPaths::dev_source_root().empty() ? std::filesystem::path{} : AssetPaths::dev_source_root() / "resources",
            };

            for (const auto& candidate : candidates) {
                std::error_code ec;
                if (!candidate.empty() && std::filesystem::is_directory(candidate / "icons", ec)) {
                    HN_INFO("EditorPaths: resources root = {}", candidate.string());
                    return candidate;
                }
            }

            HN_ERROR("EditorPaths: could not find editor resources (looked next to the executable and in the source tree)");
            return {};
        }
    }

    const std::filesystem::path& EditorPaths::resources_root() {
        // Resolved on first use, which is always after AssetPaths::init() in main().
        static const std::filesystem::path s_root = find_resources_root();
        return s_root;
    }

    std::filesystem::path EditorPaths::resource(const std::filesystem::path& relative) {
        return resources_root() / relative;
    }

}