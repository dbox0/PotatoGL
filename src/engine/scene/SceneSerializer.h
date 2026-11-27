#pragma once

#include <entt/entity/entity.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

class ComponentRegistry;
class Scene;
struct ComponentInfo;

// Scenes are JSON, written through ComponentRegistry: every registered component on a
// game object with its saved fields, keyed by component id and field key.
//   { "version": 2,
//     "entities": [ { "uuid": "9f3c...", "parent": "51ab...", "components": { "Transform": { ... } } } ] }
// Entities are written parents first, siblings in order; "parent" is omitted at the top
// level. Entity fields are stored as uuids. Version 1 files (integer "id"/"parent") still
// load. Unknown components or fields are skipped with a warning; missing fields keep
// their defaults. Unregistered components (WorldTransform, Selected, Dead) are never saved.

// order (optional) receives the entities in file order
std::string saveScene(Scene& scene, const ComponentRegistry& components, std::vector<entt::entity>* order = nullptr);

// Replaces the scene's contents, keeping saved uuids. Leaves the scene untouched if text
// isn't a valid scene. order (optional) receives the created entities in file order.
bool loadScene(Scene& scene, const ComponentRegistry& components, std::string_view text,
               std::string* error = nullptr, std::vector<entt::entity>* order = nullptr);

// Both set scene.path() on success
bool saveSceneFile(Scene& scene, const ComponentRegistry& components, const std::string& path,
                   std::string* error = nullptr);
bool loadSceneFile(Scene& scene, const ComponentRegistry& components, const std::string& path,
                   std::string* error = nullptr);

// One object and its descendants, in the scene format (undo, copy/paste, later prefabs)
std::string saveEntityTree(Scene& scene, const ComponentRegistry& components, entt::entity root);
// Adds a saved tree under parent (entt::null = top level) at a sibling index (SIZE_MAX = last).
// Saved uuids are kept unless already in use, or freshUuids is set (copies). References
// inside the tree follow it either way. Returns the tree's root, entt::null on failure.
entt::entity loadEntityTree(Scene& scene, const ComponentRegistry& components, std::string_view text,
                            entt::entity parent = entt::null, std::size_t index = SIZE_MAX,
                            bool freshUuids = false);

// One component's saved fields as a JSON object
std::string saveComponent(const ComponentInfo& info, Scene& scene, entt::entity e);
// Adds the component filled from text, or overwrites the fields of the existing one
bool loadComponent(const ComponentInfo& info, Scene& scene, entt::entity e, std::string_view text);
