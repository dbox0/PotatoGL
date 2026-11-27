#pragma once

#include <entt/entity/entity.hpp>
#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class EditHistory;
class Selection;
struct EditContext;

enum class CreateKind
{
    Empty,
    Cube,
    Sphere,
    Capsule,
    Cylinder,
    Plane,
    Quad,
    DirectionalLight,
    PointLight,
    SpotLight,
    Camera,
};

// Object-level edits asked for by menus, shortcuts and the hierarchy. Requests made while
// drawing run together in apply(), once no panel is walking the hierarchy.
// Duplicate and Paste copy whole subtrees, like Unity, with fresh uuids. Models and prefabs
// from the Project view are instantiated through here too.
class SceneActions
{
public:
    // Where an instantiated asset's root goes
    enum class Placement
    {
        Keep,   // where the asset has it (a model: the origin)
        Spawn,  // the spawn point given to apply()
        At,     // a given position
    };

private:
    enum class Type
    {
        Create,
        Duplicate,
        Copy,
        Paste,
        Delete,
        Instantiate,
        AssignMaterial,
    };
    struct Request
    {
        Type type;
        CreateKind kind = CreateKind::Empty;
        entt::entity parent = entt::null;  // AssignMaterial: the target
        std::string asset;
        Placement placement = Placement::Keep;
        glm::vec3 position{0.0f};

        Request(Type t, CreateKind k = CreateKind::Empty, entt::entity p = entt::null, std::string a = {},
                Placement pl = Placement::Keep, const glm::vec3& at = glm::vec3(0.0f))
            : type(t), kind(k), parent(p), asset(std::move(a)), placement(pl), position(at)
        {
        }
    };
    struct Copied
    {
        std::uint64_t parent;  // pasted back under it if it still exists, else at the top level
        std::string tree;
    };

    std::vector<Request> m_requests;
    std::vector<Copied> m_clipboard;
    entt::entity m_topParent = entt::null;
    std::string m_openPrefab;  // can't be instantiated inside itself

    // requested if valid, else where top-level objects go
    entt::entity parentFor(const EditContext& context, entt::entity requested) const;

    void applyCreate(EditContext& context, EditHistory& history, Selection& selection, const Request& request,
                     const glm::vec3& spawn);
    void applyDuplicate(EditContext& context, EditHistory& history, Selection& selection);
    void applyCopy(EditContext& context, const Selection& selection);
    void applyPaste(EditContext& context, EditHistory& history, Selection& selection);
    void applyDelete(EditContext& context, EditHistory& history, Selection& selection);
    void applyInstantiate(EditContext& context, EditHistory& history, Selection& selection, const Request& request,
                          const glm::vec3& spawn);
    void applyAssignMaterial(EditContext& context, EditHistory& history, const Request& request);

public:
    // parent = entt::null: top level, placed at the spawn point given to apply()
    void create(CreateKind kind, entt::entity parent = entt::null) { m_requests.push_back({Type::Create, kind, parent}); }
    void duplicate() { m_requests.push_back({Type::Duplicate}); }
    void copy()      { m_requests.push_back({Type::Copy}); }
    void paste()     { m_requests.push_back({Type::Paste}); }
    void remove()    { m_requests.push_back({Type::Delete}); }
    bool canPaste() const { return !m_clipboard.empty(); }

    // A model (as a MeshRenderer) or a prefab; parent = entt::null: top level
    void instantiate(std::string asset, entt::entity parent = entt::null, Placement placement = Placement::Spawn,
                     const glm::vec3& position = glm::vec3(0.0f))
    {
        m_requests.push_back({Type::Instantiate, CreateKind::Empty, parent, std::move(asset), placement, position});
    }
    // Sets e's MeshRenderer material; e must have one
    void assignMaterial(entt::entity e, std::string material)
    {
        m_requests.push_back({Type::AssignMaterial, CreateKind::Empty, e, std::move(material)});
    }

    // ImGui menu items: one per CreateKind, and Duplicate / Copy / Paste / Delete
    void drawCreateItems(entt::entity parent = entt::null);
    void drawEditItems(bool hasSelection);

    // Objects asked for at the top level go under it instead (the open prefab's root);
    // entt::null: the top level
    // openPrefab: the prefab being edited, refused as an instance inside itself
    void setPrefabMode(entt::entity topParent, std::string openPrefab)
    {
        m_topParent = topParent;
        m_openPrefab = std::move(openPrefab);
    }

    // spawn: world position for new top-level objects
    void apply(EditContext& context, EditHistory& history, Selection& selection, const glm::vec3& spawn);
};

// The selected objects without those whose ancestor is selected too, in selection order
std::vector<entt::entity> selectionRoots(const EditContext& context, const Selection& selection);
