#include "Scene.h"

#include "scene/Transform.h"

Scene::Scene()
{
    m_root = m_registry.create();
    m_registry.emplace<Relationship>(m_root);
    m_registry.on_destroy<Relationship>().connect<&Scene::onRelationshipDestroy>(*this);
}

Scene::~Scene()
{
    m_registry.on_destroy<Relationship>().disconnect<&Scene::onRelationshipDestroy>(*this);
}

void Scene::link(entt::entity e, entt::entity parent)
{
    Relationship& rel = m_registry.get<Relationship>(e);
    Relationship& parentRel = m_registry.get<Relationship>(parent);
    rel.parent = parent;
    rel.prevSibling = entt::null;
    rel.nextSibling = entt::null;

    if (parentRel.firstChild == entt::null) {
        parentRel.firstChild = e;
    } else {
        entt::entity last = parentRel.firstChild;
        while (m_registry.get<Relationship>(last).nextSibling != entt::null)
            last = m_registry.get<Relationship>(last).nextSibling;
        m_registry.get<Relationship>(last).nextSibling = e;
        rel.prevSibling = last;
    }
    ++parentRel.childCount;
}

void Scene::unlink(entt::entity e)
{
    Relationship& rel = m_registry.get<Relationship>(e);
    if (rel.parent == entt::null)
        return;

    Relationship& parentRel = m_registry.get<Relationship>(rel.parent);
    if (rel.prevSibling != entt::null)
        m_registry.get<Relationship>(rel.prevSibling).nextSibling = rel.nextSibling;
    else
        parentRel.firstChild = rel.nextSibling;
    if (rel.nextSibling != entt::null)
        m_registry.get<Relationship>(rel.nextSibling).prevSibling = rel.prevSibling;
    --parentRel.childCount;

    rel.parent = entt::null;
    rel.prevSibling = entt::null;
    rel.nextSibling = entt::null;
}

void Scene::onRelationshipDestroy(entt::registry&, entt::entity e)
{
    if (e == m_root)
        return;
    unlink(e);
    forEachChild(e, [this](entt::entity child) {
        unlink(child);
        link(child, m_root);
    });
}

entt::entity Scene::createGameObject(std::string name, entt::entity parent)
{
    const entt::entity e = m_registry.create();
    m_registry.emplace<Name>(e, std::move(name));
    m_registry.emplace<Transform>(e);
    m_registry.emplace<WorldTransform>(e);
    m_registry.emplace<Relationship>(e);
    link(e, parent == entt::null ? m_root : parent);
    return e;
}

void Scene::destroyRecursive(entt::entity e)
{
    if (e == m_root || !m_registry.valid(e))
        return;
    // Re-fetch every time: destroying a child swaps elements in the Relationship storage
    entt::entity child;
    while ((child = m_registry.get<Relationship>(e).firstChild) != entt::null)
        destroyRecursive(child);
    m_registry.destroy(e);
}

bool Scene::setParent(entt::entity e, entt::entity parent)
{
    if (parent == entt::null)
        parent = m_root;
    if (e == m_root || e == parent || isAncestor(e, parent))
        return false;
    unlink(e);
    link(e, parent);
    return true;
}

bool Scene::isAncestor(entt::entity ancestor, entt::entity e) const
{
    for (entt::entity p = m_registry.get<Relationship>(e).parent; p != entt::null;
         p = m_registry.get<Relationship>(p).parent) {
        if (p == ancestor)
            return true;
    }
    return false;
}

void Scene::updateWorldTransforms()
{
    forEachChild(m_root, [this](entt::entity e) { updateWorld(e, glm::mat4(1.0f)); });
}

void Scene::updateWorld(entt::entity e, const glm::mat4& parentWorld)
{
    glm::mat4 world = parentWorld;
    if (const Transform* t = m_registry.try_get<Transform>(e))
        world = parentWorld * t->getMatrix();
    if (WorldTransform* w = m_registry.try_get<WorldTransform>(e))
        w->matrix = world;
    forEachChild(e, [&](entt::entity child) { updateWorld(child, world); });
}
