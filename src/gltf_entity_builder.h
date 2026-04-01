#pragma once

#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/matrix_decompose.hpp"

#include "Honey/loaders/gltf_loader.h"
#include "Honey/scene/scene.h"
#include "Honey/scene/entity.h"
#include "Honey/scene/components.h"

namespace Honey {

    inline void spawn_gltf_node(Scene& scene, const GltfNode& node, Entity parent, const std::filesystem::path& source_path) {
        Entity e = scene.create_child_for(parent, node.name);
        auto& tc = e.get_component<TransformComponent>();

        glm::vec3 scale, translation, skew;
        glm::vec4 perspective;
        glm::quat rotation;
        glm::decompose(node.local_transform, scale, rotation, translation, skew, perspective);
        tc.translation = translation;
        tc.rotation    = glm::eulerAngles(rotation);
        tc.scale       = scale;

        if (node.mesh) {
            auto& mrc = e.add_component<MeshRendererComponent>();
            mrc.mesh = node.mesh;
            mrc.gltf_source_path = source_path;
            mrc.gltf_node_name = node.name;
        }

        for (const auto& child : node.children)
            spawn_gltf_node(scene, child, e, source_path);
    }

    inline Entity spawn_gltf_scene_tree(Scene& scene, const GltfSceneTree& tree, const std::filesystem::path& source_path) {
        Entity root = scene.create_entity(tree.name);
        for (const auto& node : tree.roots)
            spawn_gltf_node(scene, node, root, source_path);
        return root;
    }

} // namespace Honey