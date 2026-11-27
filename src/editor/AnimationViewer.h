#pragma once

#include "editor/ViewportPanel.h"

#include <entt/entity/entity.hpp>
#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <utility>
#include <vector>

class AssetCache;
class Renderer;
class Scene;
struct ClipData;
struct OverlayContext;
struct SkeletonData;

// Animation Viewer mode: one skinned model in a scene of its own (actor, ground, studio
// light) with a clip list, a timeline, the joint tree and the selected joint's transform.
// It replaces the scene UI while open; the main scene stays loaded and untouched.
// The viewer owns the clock: its Animator is only ever evaluated, never advanced by the
// AnimationSystem. Space plays/pauses, Left/Right step a frame, Home/End jump to in/out,
// F frames the actor, Escape closes. Right drag (or Alt+left) orbits, middle drag pans.
class AnimationViewer
{
private:
    std::unique_ptr<Scene> m_scene;
    entt::entity m_actor = entt::null;
    std::string m_model;
    std::string m_name;  // file name, for titles
    std::shared_ptr<const SkeletonData> m_skeleton;
    std::vector<std::string> m_clips;  // refs
    int m_clip = -1;
    std::shared_ptr<const ClipData> m_clipData;
    std::vector<std::vector<int>> m_children;  // per joint
    float m_actorHeight = 2.0f;                 // after normalizing the actor's size

    // Playback, in seconds and frames of the clip's fixed rate
    bool m_playing = true;
    bool m_loop = true;
    float m_speed = 1.0f;
    float m_time = 0.0f;
    int m_in = 0;
    int m_out = 0;
    enum class Drag
    {
        None,
        Playhead,
        In,
        Out,
    };
    Drag m_timelineDrag = Drag::None;

    int m_joint = -1;  // selected
    bool m_revealJoint = false;  // selected in the viewport: open and scroll the tree to it

    // Orbit camera
    glm::vec3 m_target{0.0f, 1.0f, 0.0f};
    float m_yaw = 0.6f;
    float m_pitch = 0.25f;
    float m_distance = 5.0f;
    int m_orbitButton = -1;  // ImGuiMouseButton held for orbiting, -1 if not
    bool m_panning = false;
    glm::vec2 m_pressPos{0.0f};
    bool m_pressed = false;  // left button went down on the image, not yet a drag

    ViewportPanel m_view{"Animation Viewer", "###AnimationViewerView"};
    struct Marker
    {
        int joint;
        glm::vec2 pos;
        float depth;
    };
    std::vector<Marker> m_markers;
    bool m_closeRequested = false;

    glm::mat4 viewMatrix() const;
    glm::mat4 projection() const;
    void frameActor();
    void selectClip(AssetCache& assets, int index);
    void stepFrame(int delta);
    void advance(float dt);
    void applyTime();
    float frameTime(int frame) const;
    int lastFrame() const;
    int currentFrame() const;

    void drawDockSpace();
    void drawViewport();
    void drawViewportOverlay();
    void handleCamera();
    void drawClips(AssetCache& assets);
    void drawTimeline();
    void drawSkeleton();
    void drawJointTree(int joint);
    void drawJoint();
    void handleShortcuts();

public:
    AnimationViewer();
    ~AnimationViewer();

    AnimationViewer(const AnimationViewer&) = delete;
    AnimationViewer& operator=(const AnimationViewer&) = delete;

    // model: a skinned model ref. clip: one of its clip refs to start on, empty for the first.
    // False (with error) if the model has no skeleton.
    bool open(AssetCache& assets, Renderer& renderer, const std::string& model, const std::string& material,
              const std::string& clip, std::string* error = nullptr);
    void close();
    bool isOpen() const { return m_scene != nullptr; }
    Scene* scene() { return m_scene.get(); }
    const std::string& name() const { return m_name; }

    // All panels; call instead of the scene panels while open
    void draw(const OverlayContext& context);
    // After the viewer's scene is synced: the viewport's image
    void render(const OverlayContext& context);
    void release();

    bool takeCloseRequest() { return std::exchange(m_closeRequested, false); }
};
