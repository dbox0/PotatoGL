#pragma once

#include <entt/entity/entity.hpp>

#include <cstddef>
#include <cstdint>
#include <string>

class ComponentRegistry;
class Scene;

// A prefab is a saved object tree (.prefab, in the scene format). Its instances are copies
// whose root carries PrefabInstance, rebuilt from the file when it changes. Only the
// instance root's Name, Transform and RectTransform are the instance's own; anything else edited on an
// instance is replaced by the prefab's contents on the next rebuild.
// Each instance object's uuid comes from the instance root's uuid and the prefab object's,
// so a rebuild keeps the uuids (selection, undo, references) of objects still in the prefab.
// Prefabs nested in a prefab are rebuilt from their own files too.

// ref: the .prefab's path relative to the resource dir. rootUuid = 0: a fresh one.
// Returns the instance's root, entt::null if the file can't be read.
entt::entity instantiatePrefab(Scene& scene, const ComponentRegistry& components, const std::string& ref,
                               entt::entity parent = entt::null, std::size_t index = SIZE_MAX,
                               std::uint64_t rootUuid = 0);

// Rebuilds the instances of ref from its file; ref empty: every instance. Instances whose
// file can't be read are left alone. Entity fields pointing into an instance follow the
// rebuild. Returns how many instances changed.
int refreshPrefabInstances(Scene& scene, const ComponentRegistry& components, const std::string& ref = {});
