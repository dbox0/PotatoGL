#pragma once

#include <entt/entity/entity.hpp>
#include <glm/glm.hpp>

#include <cstdint>
#include <string>
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
// Duplicate and Paste copy whole subtrees, like Unity, with fresh uuids.
class SceneActions
{
private:
    enum class Type
    {
        Create,
        Duplicate,
        Copy,
        Paste,
        Delete,
    };
    struct Request
    {
        Type type;
        CreateKind kind = CreateKind::Empty;
        entt::entity parent = entt::null;
    };
    struct Copied
    {
        std::uint64_t parent;  // pasted back under it if it still exists, else at the top level
        std::string tree;
    };

    std::vector<Request> m_requests;
    std::vector<Copied> m_clipboard;

    void applyCreate(EditContext& context, EditHistory& history, Selection& selection, const Request& request,
                     const glm::vec3& spawn);
    void applyDuplicate(EditContext& context, EditHistory& history, Selection& selection);
    void applyCopy(EditContext& context, const Selection& selection);
    void applyPaste(EditContext& context, EditHistory& history, Selection& selection);
    void applyDelete(EditContext& context, EditHistory& history, Selection& selection);

public:
    // parent = entt::null: top level, placed at the spawn point given to apply()
    void create(CreateKind kind, entt::entity parent = entt::null) { m_requests.push_back({Type::Create, kind, parent}); }
    void duplicate() { m_requests.push_back({Type::Duplicate}); }
    void copy()      { m_requests.push_back({Type::Copy}); }
    void paste()     { m_requests.push_back({Type::Paste}); }
    void remove()    { m_requests.push_back({Type::Delete}); }
    bool canPaste() const { return !m_clipboard.empty(); }

    // ImGui menu items: one per CreateKind, and Duplicate / Copy / Paste / Delete
    void drawCreateItems(entt::entity parent = entt::null);
    void drawEditItems(bool hasSelection);

    // spawn: world position for new top-level objects
    void apply(EditContext& context, EditHistory& history, Selection& selection, const glm::vec3& spawn);
};

// The selected objects without those whose ancestor is selected too, in selection order
std::vector<entt::entity> selectionRoots(const EditContext& context, const Selection& selection);
