#include "Scene.h"

#include "engine/scene/Camera.h"
#include "engine/scene/Transform.h"

#include <random>

Scene::Scene()
{
    m_root = m_registry.create();
    m_registry.emplace<Relationship>(m_root);
    m_registry.on_destroy<Relationship>().connect<&Scene::onRelationshipDestroy>(*this);
    m_registry.on_destroy<Uuid>().connect<&Scene::onUuidDestroy>(*this);
}

Scene::~Scene()
{
    m_registry.on_destroy<Relationship>().disconnect<&Scene::onRelationshipDestroy>(*this);
    m_registry.on_destroy<Uuid>().disconnect<&Scene::onUuidDestroy>(*this);
}

static std::uint64_t newUuid()
{
    static std::mt19937_64 rng{std::random_device{}()};
    std::uint64_t value;
    do {
        value = rng();
    } while (value == 0);
    return value;
}

void Scene::onUuidDestroy(entt::registry&, entt::entity e)
{
    const auto it = m_byUuid.find(m_registry.get<Uuid>(e).value);
    if (it != m_byUuid.end() && it->second == e)
        m_byUuid.erase(it);
}

std::uint64_t Scene::uuidOf(entt::entity e) const
{
    const Uuid* uuid = m_registry.try_get<Uuid>(e);
    return uuid ? uuid->value : 0;
}

entt::entity Scene::findByUuid(std::uint64_t uuid) const
{
    if (uuid == 0)
        return m_root;
    const auto it = m_byUuid.find(uuid);
    return it == m_byUuid.end() ? entt::null : it->second;
}

std::size_t Scene::siblingIndex(entt::entity e) const
{
    std::size_t index = 0;
    for (entt::entity s = m_registry.get<Relationship>(e).prevSibling; s != entt::null;
         s = m_registry.get<Relationship>(s).prevSibling)
        ++index;
    return index;
}

void Scene::link(entt::entity e, entt::entity parent, std::size_t index)
{
    Relationship& rel = m_registry.get<Relationship>(e);
    Relationship& parentRel = m_registry.get<Relationship>(parent);
    rel.parent = parent;
    rel.prevSibling = entt::null;
    rel.nextSibling = entt::null;

    entt::entity next = entt::null;  // insert before this one; null appends
    if (index < parentRel.childCount) {
        next = parentRel.firstChild;
        for (std::size_t i = 0; i < index; ++i)
            next = m_registry.get<Relationship>(next).nextSibling;
    }

    if (next == entt::null) {
        if (parentRel.lastChild == entt::null)
            parentRel.firstChild = e;
        else
            m_registry.get<Relationship>(parentRel.lastChild).nextSibling = e;
        rel.prevSibling = parentRel.lastChild;
        parentRel.lastChild = e;
    } else {
        Relationship& nextRel = m_registry.get<Relationship>(next);
        rel.prevSibling = nextRel.prevSibling;
        rel.nextSibling = next;
        if (nextRel.prevSibling == entt::null)
            parentRel.firstChild = e;
        else
            m_registry.get<Relationship>(nextRel.prevSibling).nextSibling = e;
        nextRel.prevSibling = e;
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
    else
        parentRel.lastChild = rel.prevSibling;
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

entt::entity Scene::createGameObject(std::string name, entt::entity parent, std::uint64_t uuid, std::size_t index)
{
    const entt::entity e = m_registry.create();
    if (uuid == 0 || m_byUuid.contains(uuid))
        uuid = newUuid();
    m_registry.emplace<Uuid>(e, uuid);
    m_byUuid[uuid] = e;
    m_registry.emplace<Name>(e, std::move(name));
    m_registry.emplace<Transform>(e);
    m_registry.emplace<WorldTransform>(e);
    m_registry.emplace<Relationship>(e);
    link(e, parent == entt::null ? m_root : parent, index);
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

void Scene::clear()
{
    m_deferred.clear();
    entt::entity child;
    while ((child = m_registry.get<Relationship>(m_root).firstChild) != entt::null)
        destroyRecursive(child);
}

bool Scene::setParent(entt::entity e, entt::entity parent, std::size_t index)
{
    if (parent == entt::null)
        parent = m_root;
    if (e == m_root || e == parent || isAncestor(e, parent))
        return false;
    unlink(e);
    link(e, parent, index);
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

entt::entity Scene::gameCamera() const
{
    auto cameras = m_registry.view<CameraComponent, WorldTransform>(entt::exclude<Disabled>);
    return cameras.begin() == cameras.end() ? entt::null : *cameras.begin();
}

std::optional<glm::mat4> Scene::gameCameraViewProj(float aspect) const
{
    const entt::entity e = gameCamera();
    if (e == entt::null)
        return std::nullopt;
    const auto& camera = m_registry.get<CameraComponent>(e);
    return camera.projection(aspect) * viewFromWorld(m_registry.get<WorldTransform>(e).matrix);
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

void Scene::flushDeferred()
{
    // Deferred work may defer more; keep going until the queue settles
    while (!m_deferred.empty()) {
        m_flushing.swap(m_deferred);
        for (Deferred& fn : m_flushing)
            fn(*this);
        m_flushing.clear();
    }
}
