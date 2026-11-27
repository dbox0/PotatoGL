#include "AnimationSystem.h"

#include "engine/anim/Pose.h"
#include "engine/assets/AssetCache.h"
#include "engine/render/Renderer.h"
#include "engine/scene/Components.h"
#include "engine/scene/Scene.h"
#include "engine/scene/Transform.h"

static void restBind(SkinnedMeshRenderer& renderer)
{
    renderer.jointModel = renderer.skeleton->bindModel;
    renderer.posed = false;
}

// True if the pose changed
static bool tickAnimator(Animator& animator, SkinnedMeshRenderer& renderer, AssetCache& assets, AnimationTick tick,
                         float dt)
{
    const SkeletonData& skeleton = *renderer.skeleton;
    if (tick == AnimationTick::Rest) {
        const bool changed = renderer.posed;
        animator.started = false;
        animator.playing = false;
        animator.time = 0.0f;
        animator.evaluated = false;
        if (changed)
            restBind(renderer);
        return changed;
    }

    if (!animator.resolved || animator.boundTo != &skeleton) {
        if (!animator.resolved)
            animator.clipData = animator.clip.empty() ? nullptr : assets.clip(animator.clip);
        animator.resolved = true;
        animator.boundTo = &skeleton;
        animator.binding = animator.clipData ? bindClip(*animator.clipData, skeleton) : std::vector<std::int16_t>{};
        animator.pose.setBind(skeleton);
        animator.time = 0.0f;
        animator.evaluated = false;
    }
    if (!animator.clipData) {
        const bool changed = renderer.posed;
        if (changed)
            restBind(renderer);
        return changed;
    }
    if (!animator.started) {
        animator.started = true;
        animator.playing = animator.playOnStart || tick == AnimationTick::Preview;
    }

    bool advanced = false;
    const bool running = tick == AnimationTick::Play || tick == AnimationTick::Preview;
    if (running && animator.playing && dt > 0.0f && animator.speed != 0.0f) {
        animator.time = advanceClipTime(animator.time, dt * animator.speed, animator.clipData->duration, animator.loop);
        advanced = true;
    }
    if (!advanced && animator.evaluated)
        return false;

    sampleClip(*animator.clipData, animator.binding, animator.time, animator.pose);
    renderer.jointModel.resize(skeleton.jointCount());
    localToModel(skeleton, animator.pose, renderer.jointModel);
    animator.evaluated = true;
    renderer.posed = true;
    return true;
}

static void updateSockets(Scene& scene)
{
    entt::registry& registry = scene.registry();
    for (auto [e, socket, transform] : registry.view<BoneSocket, Transform>().each()) {
        const entt::entity parent = registry.get<Relationship>(e).parent;
        const SkinnedMeshRenderer* renderer =
            parent == entt::null ? nullptr : registry.try_get<SkinnedMeshRenderer>(parent);
        if (!renderer || !renderer->skeleton || renderer->jointModel.empty())
            continue;
        if (socket.resolvedFor != renderer->skeleton.get()) {
            socket.jointIndex = renderer->skeleton->findJoint(socket.joint);
            socket.resolvedFor = renderer->skeleton.get();
        }
        if (socket.jointIndex < 0 || static_cast<std::size_t>(socket.jointIndex) >= renderer->jointModel.size())
            continue;
        const JointTrs trs = decompose(renderer->jointModel[static_cast<std::size_t>(socket.jointIndex)]);
        transform.setPos(trs.t);
        transform.setRot(trs.r);
        transform.setScale(trs.s);
    }
}

void updateAnimation(Scene& scene, AssetCache& assets, Renderer& renderer, float dt, AnimationTick tick)
{
    entt::registry& registry = scene.registry();
    for (auto [e, skinned] : registry.view<SkinnedMeshRenderer>().each()) {
        if (skinned.model == InvalidModel && !skinned.mesh.empty())
            skinned.model = assets.model(skinned.mesh);
        if (skinned.materialId == InvalidMaterial && !skinned.material.empty())
            skinned.materialId = assets.material(skinned.material);
        if (skinned.model == InvalidModel || !renderer.modelSkinned(skinned.model)) {
            skinned.skin = InvalidSkin;
            skinned.skeleton.reset();
            skinned.jointModel.clear();
            continue;
        }

        bool write = renderer.acquireSkin(skinned.skin, skinned.model);
        std::shared_ptr<const SkeletonData> skeleton = assets.skeleton(skinned.mesh);
        if (!skeleton)
            continue;
        if (skinned.skeleton != skeleton) {
            skinned.skeleton = std::move(skeleton);
            restBind(skinned);
            write = true;
        }
        if (Animator* animator = registry.try_get<Animator>(e))
            write |= tickAnimator(*animator, skinned, assets, tick, dt);
        else if (skinned.posed) {
            restBind(skinned);
            write = true;
        }
        if (write)
            writePalette(*skinned.skeleton, skinned.jointModel, renderer.writeSkinPalette(skinned.skin));
    }
    updateSockets(scene);
}

void resetAnimation(Scene& scene)
{
    for (auto [e, animator] : scene.registry().view<Animator>().each()) {
        animator.started = false;
        animator.playing = false;
        animator.time = 0.0f;
        animator.evaluated = false;
    }
}
