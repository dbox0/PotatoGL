#pragma once

class AssetCache;
class Renderer;
class Scene;

enum class AnimationTick
{
    Rest,     // Edit mode without Preview: bind pose, times reset
    Hold,     // paused: poses stay as they are
    Play,     // Play mode: Animators with playOnStart start, playing ones advance
    Preview,  // Edit mode with Preview on: every Animator plays
};

// Before Scene::updateWorldTransforms: poses every SkinnedMeshRenderer, writes changed
// palettes into the renderer and drives BoneSocket transforms. Acquires the skins, so it
// must run every frame for them to stay alive.
void updateAnimation(Scene& scene, AssetCache& assets, Renderer& renderer, float dt, AnimationTick tick);

// Animators start over: playOnStart applies again (entering Play)
void resetAnimation(Scene& scene);
