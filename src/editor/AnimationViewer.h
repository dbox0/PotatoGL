#pragma once

#include "editor/EditHistory.h"
#include "editor/ViewportPanel.h"

#include "engine/anim/AnimAsset.h"
#include "engine/anim/Ik.h"
#include "engine/anim/Skeleton.h"

#include <entt/entity/entity.hpp>
#include <glm/glm.hpp>

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class AssetCache;
struct ImDrawList;
class ComponentRegistry;
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
//
// Poser: in Pose mode a pose is local joint overrides over a base (the clip frame under the
// playhead, or the bind pose), edited with a gizmo on the selected joint (W move, E rotate,
// R scale) or in the Joint panel. On humanoid rigs (detectHumanoid) hands and feet are IK
// effectors: moving one solves its limb, with the knee / elbow aimed at a draggable pole.
// Moving the hips or spine keeps the feet planted; pinned limbs stay where they were pinned. Poses save as .pose; Key Pose (K) writes the posed joints as
// keys into the selected .anim, which is baked and saved at once. Embedded clips are
// read-only: Duplicate makes an .anim of one. Every pose and key edit is a step in the
// viewer's own undo history (Ctrl+Z / Ctrl+Y).
class AnimationViewer
{
public:
    // What one undo step restores: the pose, and the authored clip if a key edit changed it
    struct PoserState
    {
        std::map<int, JointTrs> overrides;
        std::string clipRef;
        std::optional<AuthoredClip> clip;
    };

private:
    std::unique_ptr<Scene> m_scene;
    entt::entity m_actor = entt::null;
    std::string m_model;
    std::string m_name;  // file name, for titles
    std::shared_ptr<const SkeletonData> m_skeleton;
    std::vector<std::string> m_clips;  // refs: the model's own clips, then .anim files for this skeleton
    std::size_t m_embeddedClips = 0;   // how many of m_clips are the model's own
    std::vector<std::string> m_poses;  // .pose files for this skeleton
    int m_clip = -1;
    std::optional<AuthoredClip> m_authored;  // the selected clip's keys, if it's an .anim
    std::uint32_t m_skeletonHash = 0;
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

    // Poser
    bool m_posing = false;
    bool m_baseBind = false;  // pose over the bind pose instead of the clip frame
    std::map<int, JointTrs> m_overrides;
    enum class GizmoOp
    {
        Rotate,
        Move,
        Scale,
    };
    GizmoOp m_gizmoOp = GizmoOp::Rotate;
    bool m_gizmoLocal = true;
    bool m_gizmoDragging = false;
    bool m_gizmoHovered = false;  // last frame, so a click on the gizmo doesn't pick a joint
    PoserState m_dragStart;
    std::optional<PoserState> m_fieldStart;  // a Joint panel field is being dragged
    std::optional<JointTrs> m_jointClipboard;
    std::vector<PoseJoint> m_poseClipboard;
    EditHistory m_history;
    const ComponentRegistry* m_components = nullptr;  // valid during draw()
    AssetCache* m_assets = nullptr;                   // valid during draw()

    // IK
    HumanoidRig m_rig;
    bool m_ik = true;
    bool m_plantFeet = true;  // moving the body keeps the feet where they are
    std::vector<std::optional<glm::vec3>> m_poles;  // per limb, model space; unset: from the pose
    struct Pin
    {
        bool on = false;
        glm::vec3 target{0.0f};  // the effector, model space
        glm::quat rotation = glm::identity<glm::quat>();  // the end's
    };
    std::vector<Pin> m_pins;
    int m_poleLimb = -1;  // a pole handle is selected instead of a joint
    struct Planted
    {
        std::size_t limb;
        glm::vec3 target;
        glm::quat rotation;
        glm::vec3 pole;
    };
    std::vector<Planted> m_planted;  // limbs held in place during the current drag
    glm::quat m_dragEndRotation = glm::identity<glm::quat>();
    glm::vec3 m_dragEffector{0.0f};
    glm::vec3 m_dragPole{0.0f};
    std::string m_dragName;
    mutable std::shared_ptr<const ClipData> m_baseBindingFor;  // held, so a rebaked clip never reuses its address
    mutable std::vector<std::int16_t> m_baseBinding;
    enum class HandleKind
    {
        Effector,
        Hips,
        Pole,
    };
    struct Handle
    {
        HandleKind kind;
        int index;  // limb, or the hips joint
        glm::vec2 pos;
        float depth;
    };
    std::vector<Handle> m_handles;

    // Name prompt for New Clip, Duplicate and Save Pose; confirmation for Delete
    enum class NameAction
    {
        None,
        NewClip,
        DuplicateClip,
        SavePose,
    };
    NameAction m_nameAction = NameAction::None;
    bool m_openNamePopup = false;
    char m_nameBuffer[128] = {};
    std::string m_deleteRef;
    bool m_openDeletePopup = false;
    std::string m_status;
    bool m_assetsChanged = false;

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
    void drawPoser();
    void drawPopups();
    void handleShortcuts();

    // Poser
    friend class PoserCommand;
    PoserState state() const;
    void restore(const PoserState& state);
    void record(std::string name, PoserState before);
    JointTrs displayedLocal(int joint) const;
    Affine jointModel(int joint) const;  // with this frame's override of joint itself
    void drawJointGizmo();

    // IK
    void evaluate(Pose& local, std::vector<Affine>& model) const;  // base + overrides, this frame
    int effectorLimb(int joint) const;
    int effectorJoint(const IkLimb& limb) const { return limb.control >= 0 ? limb.control : limb.end; }
    bool limbContains(const IkLimb& limb, int joint) const;
    glm::vec3 poleFor(std::size_t limb, const std::vector<Affine>& model) const;
    void setJointPosition(int joint, const glm::vec3& modelPosition);
    // Puts the limb's effector at target (model space) and solves the limb
    void moveEffector(std::size_t limb, const glm::vec3& target, const glm::quat& endRotation, const glm::vec3& pole);
    void capturePlanted(int draggedJoint, int draggedLimb);
    void applyPlanted();
    void applyPins();
    void setPin(std::size_t limb, bool on);
    void drawIkHandles(ImDrawList* drawList, const glm::mat4& viewProj, const glm::vec2& origin, const glm::vec2& size);
    void drawIkPanel();
    void keyPose();
    void deleteKey();
    void flipPose();
    void mirrorJoint();
    void applyPoseAsset(const PoseAsset& pose, std::string name);
    void savePose(const std::string& name);
    void createClip(const std::string& name, bool duplicate);
    void deleteClip(const std::string& ref);
    void commitAuthored();  // rebakes the selected .anim, hands it to the AssetCache and saves it
    std::shared_ptr<const ClipData> writeAuthored(const std::string& ref, const AuthoredClip& clip);
    void refreshAssets();
    std::string assetFolder() const;
    std::string clipRef() const { return m_clip >= 0 ? m_clips[static_cast<std::size_t>(m_clip)] : std::string(); }

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
    // Opens an .anim of this skeleton, or applies a .pose to the current one
    void openAsset(AssetCache& assets, const std::string& ref);
    // After the viewer's scene is synced: the viewport's image
    void render(const OverlayContext& context);
    void release();

    bool takeCloseRequest() { return std::exchange(m_closeRequested, false); }
    // Files were created or deleted, so the Project view should refresh
    bool takeAssetsChanged() { return std::exchange(m_assetsChanged, false); }
};
