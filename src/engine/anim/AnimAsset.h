#pragma once

#include "engine/anim/Clip.h"
#include "engine/anim/Skeleton.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Authored animation assets, JSON:
//   .anim  sparse keys per joint channel (what the user made) plus the clip baked to the
//          fixed-rate runtime format, which is all the runtime reads
//   .pose  local transforms of the joints a pose changes
// Both name the skeleton by hash and the model they were made on.

template<typename T>
struct Key
{
    int frame;
    T value;
};

struct AuthoredTrack
{
    std::string joint;
    std::vector<Key<glm::vec3>> t;  // sorted by frame, one key per frame at most
    std::vector<Key<glm::quat>> r;
    std::vector<Key<glm::vec3>> s;

    bool empty() const { return t.empty() && r.empty() && s.empty(); }
};

struct AuthoredClip
{
    std::uint32_t skeletonHash = 0;
    std::string model;  // the model it was authored on
    float rate = ClipData::DefaultRate;
    int frameCount = 31;
    bool loop = true;
    std::vector<AuthoredTrack> tracks;

    AuthoredTrack* track(std::string_view joint);
    AuthoredTrack& addTrack(const std::string& joint);  // the existing one if there is one
};

struct PoseJoint
{
    std::string joint;
    JointTrs local;
};

struct PoseAsset
{
    std::uint32_t skeletonHash = 0;
    std::string model;
    std::vector<PoseJoint> joints;
};

// Identifies a skeleton by its joint names in order
std::uint32_t skeletonHash(const SkeletonData& skeleton);

// Keys at frame replace an existing one
void setKey(std::vector<Key<glm::vec3>>& keys, int frame, const glm::vec3& value);
void setKey(std::vector<Key<glm::quat>>& keys, int frame, const glm::quat& value);
// Linear position / scale, nlerp rotation between keys; held before the first and after the last
glm::vec3 sampleKeys(const std::vector<Key<glm::vec3>>& keys, float frame, const glm::vec3& fallback);
glm::quat sampleKeys(const std::vector<Key<glm::quat>>& keys, float frame, const glm::quat& fallback);

// To the fixed-rate runtime format; channels without keys hold the skeleton's bind pose
ClipData bakeClip(const AuthoredClip& clip, const SkeletonData& skeleton, std::string name);
// Every frame of every animated channel becomes a key (Duplicate as .anim)
AuthoredClip unbakeClip(const ClipData& clip, const SkeletonData& skeleton, const std::string& model);

std::string saveAnimText(const AuthoredClip& clip, const ClipData& baked);
// keys and/or baked may be null. The baked clip is named name.
bool loadAnimText(std::string_view text, AuthoredClip* keys, ClipData* baked, const std::string& name,
                  std::string* error = nullptr);

std::string savePoseText(const PoseAsset& pose);
std::optional<PoseAsset> loadPoseText(std::string_view text, std::string* error = nullptr);

// The skeleton hash and model an .anim or .pose was made on; nullopt if text isn't one
struct AnimHeader
{
    std::uint32_t skeletonHash = 0;
    std::string model;
};
std::optional<AnimHeader> readAnimHeader(std::string_view text);

// The other side's joint by name ("Arm.L" / "Arm.R", "L_Arm", "LeftArm", "arm_l", ...); empty if none
std::string mirroredJointName(std::string_view name);
// A local transform mirrored across the YZ plane, for a joint whose rest frame mirrors from's:
// the change from from's bind pose, mirrored, applied to to's
JointTrs mirrorLocal(const JointTrs& local, const JointTrs& fromBind, const JointTrs& toBind);
