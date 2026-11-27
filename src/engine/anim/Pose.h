#pragma once

#include "engine/anim/Clip.h"
#include "engine/anim/Skeleton.h"

#include <cstdint>
#include <span>
#include <vector>

// Local joint transforms, structure of arrays, indexed like the skeleton's joints
struct Pose
{
    std::vector<glm::vec3> t;
    std::vector<glm::quat> r;
    std::vector<glm::vec3> s;

    void setBind(const SkeletonData& skeleton);
};

// Skeleton joint per clip track, -1 where the skeleton has no such joint (warned once)
std::vector<std::int16_t> bindClip(const ClipData& clip, const SkeletonData& skeleton);

// Writes the tracks' joints only; the others keep what pose had
void sampleClip(const ClipData& clip, std::span<const std::int16_t> binding, float time, Pose& pose);

// One pass over the sorted joints
void localToModel(const SkeletonData& skeleton, const Pose& pose, std::span<Affine> model);

// model * inverseBind per joint, three vec4 rows each
void writePalette(const SkeletonData& skeleton, std::span<const Affine> model, std::span<glm::vec4> palette);

// time + dt, wrapped or clamped to [0, duration]
float advanceClipTime(float time, float dt, float duration, bool loop);

// Translation, rotation and scale of an affine matrix with no shear
JointTrs decompose(const Affine& m);
