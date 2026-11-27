#pragma once

#include <entt/entity/entity.hpp>

#include <string>
#include <string_view>
#include <vector>

class ComponentRegistry;
class Scene;

// Scenes are JSON, written through ComponentRegistry: every registered component on a
// game object with its saved fields, keyed by component id and field key.
//   { "version": 1,
//     "entities": [ { "id": 1, "parent": 0?, "components": { "Transform": { "position": [0, 1, 0], ... } } } ] }
// Entities are written parents first, siblings in order. Unknown components or fields are
// skipped with a warning; missing fields keep their defaults. Unregistered components
// (WorldTransform, Selected, Dead) are never saved.

// order (optional) receives the entities in file order
std::string saveScene(Scene& scene, const ComponentRegistry& components, std::vector<entt::entity>* order = nullptr);

// Replaces the scene's contents. Leaves the scene untouched if text isn't a valid scene.
// order (optional) receives the created entities in file order.
bool loadScene(Scene& scene, const ComponentRegistry& components, std::string_view text,
               std::string* error = nullptr, std::vector<entt::entity>* order = nullptr);

// Both set scene.path() on success
bool saveSceneFile(Scene& scene, const ComponentRegistry& components, const std::string& path,
                   std::string* error = nullptr);
bool loadSceneFile(Scene& scene, const ComponentRegistry& components, const std::string& path,
                   std::string* error = nullptr);
