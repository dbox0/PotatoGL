#pragma once

#include "engine/anim/Pose.h"
#include "engine/anim/Skeleton.h"

#include <string>
#include <vector>

// Humanoid limbs found from joint names and the hierarchy, for any rig that names its sides
// (Arm.L, LeftArm, thigh_l, "Bip01 L Thigh", leg_joint_L_1, ...) and nests limbs sensibly:
//   - a chain starts at the shallowest leg / arm joint of a side (clavicles and shoulders
//     aren't arms), and runs down through the child with the most joints below it
//   - its middle joint is the knee / elbow by name, else the chain's second joint
//   - its end is the foot / hand by name, else the chain's third joint; when the middle joint
//     branches into several hand parts (palms with fingers, no wrist) the end follows rigidly
//   - a foot / hand joint of that side outside the chain is an IK control the end follows
//     (rigs exported with their IK targets), a "pole" joint of that side aims the knee / elbow
//   - the hips are the joint all limbs hang from
struct IkLimb
{
    enum class Kind
    {
        Arm,
        Leg,
    };
    Kind kind = Kind::Leg;
    bool left = true;
    int root = -1;
    int mid = -1;
    int end = -1;
    int control = -1;              // -1: the end itself is the effector
    glm::vec3 controlOffset{0.0f}; // the end's bind position in the control's space
    int pole = -1;
    // false when the rig has no wrist / ankle joint: the middle joint branches into several
    // hand parts with fingers of their own. The end then moves rigidly with the middle joint
    // instead of keeping its rotation, which would bend it away from its siblings.
    bool keepEndRotation = true;
    std::string label;  // "Left Leg"
};

struct HumanoidRig
{
    int hips = -1;  // -1 if fewer than two limbs were found
    std::vector<IkLimb> limbs;
    // Where the character faces in model space (horizontal): toes point ahead of the ankles,
    // else knees bend forward; +Z if neither tells
    glm::vec3 forward{0.0f, 0.0f, 1.0f};
};

HumanoidRig detectHumanoid(const SkeletonData& skeleton);

// Two-bone IK in model space: rotates root and mid so end reaches target, with mid bending
// towards pole. If endRotation is given, end then gets that model-space rotation. Only the
// rotations of local change; model is recomputed from local.
void solveTwoBoneIk(const SkeletonData& skeleton, Pose& local, std::vector<Affine>& model, int root, int mid, int end,
                    const glm::vec3& target, const glm::vec3& pole, const glm::quat* endRotation = nullptr);

// Rotation part of a model-space matrix
glm::quat modelRotation(const Affine& m);
