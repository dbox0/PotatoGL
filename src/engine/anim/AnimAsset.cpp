#include "AnimAsset.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <cinttypes>
#include <cmath>
#include <cstdio>

using json = nlohmann::ordered_json;

static constexpr int AnimVersion = 1;

AuthoredTrack* AuthoredClip::track(std::string_view joint)
{
    for (AuthoredTrack& t : tracks) {
        if (t.joint == joint)
            return &t;
    }
    return nullptr;
}

AuthoredTrack& AuthoredClip::addTrack(const std::string& joint)
{
    if (AuthoredTrack* existing = track(joint))
        return *existing;
    tracks.push_back({joint, {}, {}, {}});
    return tracks.back();
}

std::uint32_t skeletonHash(const SkeletonData& skeleton)
{
    std::string all;
    for (const std::string& name : skeleton.names) {
        all += name;
        all += '\n';
    }
    return jointNameHash(all);
}

template<typename T>
static void setKeyImpl(std::vector<Key<T>>& keys, int frame, const T& value)
{
    const auto it = std::lower_bound(keys.begin(), keys.end(), frame,
                                     [](const Key<T>& k, int f) { return k.frame < f; });
    if (it != keys.end() && it->frame == frame)
        it->value = value;
    else
        keys.insert(it, {frame, value});
}

void setKey(std::vector<Key<glm::vec3>>& keys, int frame, const glm::vec3& value)
{
    setKeyImpl(keys, frame, value);
}

void setKey(std::vector<Key<glm::quat>>& keys, int frame, const glm::quat& value)
{
    setKeyImpl(keys, frame, value);
}

// The keys around frame and how far between them it is
template<typename T>
static bool bracket(const std::vector<Key<T>>& keys, float frame, std::size_t& a, std::size_t& b, float& u)
{
    if (keys.empty())
        return false;
    if (frame <= static_cast<float>(keys.front().frame)) {
        a = b = 0;
        u = 0.0f;
        return true;
    }
    if (frame >= static_cast<float>(keys.back().frame)) {
        a = b = keys.size() - 1;
        u = 0.0f;
        return true;
    }
    const auto it = std::upper_bound(keys.begin(), keys.end(), frame,
                                     [](float f, const Key<T>& k) { return f < static_cast<float>(k.frame); });
    b = static_cast<std::size_t>(it - keys.begin());
    a = b - 1;
    u = (frame - static_cast<float>(keys[a].frame)) / static_cast<float>(keys[b].frame - keys[a].frame);
    return true;
}

glm::vec3 sampleKeys(const std::vector<Key<glm::vec3>>& keys, float frame, const glm::vec3& fallback)
{
    std::size_t a, b;
    float u;
    if (!bracket(keys, frame, a, b, u))
        return fallback;
    return glm::mix(keys[a].value, keys[b].value, u);
}

glm::quat sampleKeys(const std::vector<Key<glm::quat>>& keys, float frame, const glm::quat& fallback)
{
    std::size_t a, b;
    float u;
    if (!bracket(keys, frame, a, b, u))
        return fallback;
    glm::quat qb = keys[b].value;
    if (glm::dot(keys[a].value, qb) < 0.0f)
        qb = -qb;
    const glm::quat q = keys[a].value * (1.0f - u) + qb * u;
    const float len = glm::length(q);
    return len > 1e-8f ? q / len : glm::identity<glm::quat>();
}

ClipData bakeClip(const AuthoredClip& authored, const SkeletonData& skeleton, std::string name)
{
    ClipData clip;
    clip.name = std::move(name);
    clip.rate = authored.rate > 0.0f ? authored.rate : ClipData::DefaultRate;
    clip.frameCount = static_cast<std::uint32_t>(std::max(authored.frameCount, 1));
    clip.duration = static_cast<float>(clip.frameCount - 1) / clip.rate;
    clip.loop = authored.loop;
    const std::uint32_t frames = clip.frameCount;

    struct Built
    {
        ClipData::Track track;
        std::vector<float> channel[3];
    };
    std::vector<Built> built;
    static constexpr int Components[3] = {3, 4, 3};
    for (const AuthoredTrack& source : authored.tracks) {
        const int joint = skeleton.findJoint(source.joint);
        if (joint < 0 || source.empty())
            continue;
        const JointTrs& bind = skeleton.bindLocal[static_cast<std::size_t>(joint)];
        Built b;
        b.track.jointHash = skeleton.nameHashes[static_cast<std::size_t>(joint)];
        b.track.constant = bind;
        const std::size_t keyCounts[3] = {source.t.size(), source.r.size(), source.s.size()};
        for (int slot = 0; slot < 3; ++slot) {
            if (keyCounts[slot] == 0)
                continue;
            if (keyCounts[slot] == 1) {
                if (slot == 0)
                    b.track.constant.t = source.t.front().value;
                else if (slot == 1)
                    b.track.constant.r = glm::normalize(source.r.front().value);
                else
                    b.track.constant.s = source.s.front().value;
                continue;
            }
            const int n = Components[slot];
            std::vector<float> values(static_cast<std::size_t>(frames) * n);
            for (std::uint32_t f = 0; f < frames; ++f) {
                float* out = values.data() + static_cast<std::size_t>(f) * n;
                const auto frame = static_cast<float>(f);
                if (slot == 1) {
                    glm::quat q = sampleKeys(source.r, frame, bind.r);
                    if (f > 0) {
                        const float* prev = out - 4;
                        if (q.x * prev[0] + q.y * prev[1] + q.z * prev[2] + q.w * prev[3] < 0.0f)
                            q = -q;
                    }
                    out[0] = q.x;
                    out[1] = q.y;
                    out[2] = q.z;
                    out[3] = q.w;
                } else {
                    const glm::vec3 v = sampleKeys(slot == 0 ? source.t : source.s, frame, slot == 0 ? bind.t : bind.s);
                    out[0] = v.x;
                    out[1] = v.y;
                    out[2] = v.z;
                }
            }
            b.track.animated |= static_cast<std::uint8_t>(1 << slot);
            b.channel[slot] = std::move(values);
        }
        built.push_back(std::move(b));
    }

    for (Built& b : built) {
        b.track.offset = clip.frameStride;
        for (int slot = 0; slot < 3; ++slot) {
            if (b.track.animated & (1 << slot))
                clip.frameStride += static_cast<std::uint32_t>(Components[slot]);
        }
        clip.tracks.push_back(b.track);
    }
    clip.frames.resize(static_cast<std::size_t>(frames) * clip.frameStride);
    for (const Built& b : built) {
        std::uint32_t offset = b.track.offset;
        for (int slot = 0; slot < 3; ++slot) {
            if (!(b.track.animated & (1 << slot)))
                continue;
            const int n = Components[slot];
            for (std::uint32_t f = 0; f < frames; ++f)
                std::copy_n(b.channel[slot].data() + static_cast<std::size_t>(f) * n, n,
                            clip.frames.data() + static_cast<std::size_t>(f) * clip.frameStride + offset);
            offset += static_cast<std::uint32_t>(n);
        }
    }
    return clip;
}

static bool nearlyEqual(const JointTrs& a, const JointTrs& b, std::uint8_t channel)
{
    constexpr float Epsilon = 1e-5f;
    if (channel == ClipData::Translation)
        return glm::length(a.t - b.t) <= Epsilon;
    if (channel == ClipData::Rotation)
        return std::abs(std::abs(glm::dot(a.r, b.r)) - 1.0f) <= Epsilon;
    return glm::length(a.s - b.s) <= Epsilon;
}

AuthoredClip unbakeClip(const ClipData& clip, const SkeletonData& skeleton, const std::string& model)
{
    AuthoredClip out;
    out.skeletonHash = skeletonHash(skeleton);
    out.model = model;
    out.rate = clip.rate;
    out.frameCount = static_cast<int>(std::max<std::uint32_t>(clip.frameCount, 1));
    out.loop = clip.loop;
    for (const ClipData::Track& track : clip.tracks) {
        const auto it = std::find(skeleton.nameHashes.begin(), skeleton.nameHashes.end(), track.jointHash);
        if (it == skeleton.nameHashes.end())
            continue;
        const auto joint = static_cast<std::size_t>(it - skeleton.nameHashes.begin());
        const JointTrs& bind = skeleton.bindLocal[joint];
        AuthoredTrack authored{skeleton.names[joint], {}, {}, {}};
        std::uint32_t offset = track.offset;
        if (track.animated & ClipData::Translation) {
            for (std::uint32_t f = 0; f < clip.frameCount; ++f) {
                const float* v = clip.frames.data() + static_cast<std::size_t>(f) * clip.frameStride + offset;
                authored.t.push_back({static_cast<int>(f), glm::vec3(v[0], v[1], v[2])});
            }
            offset += 3;
        } else if (!nearlyEqual(track.constant, bind, ClipData::Translation)) {
            authored.t.push_back({0, track.constant.t});
        }
        if (track.animated & ClipData::Rotation) {
            for (std::uint32_t f = 0; f < clip.frameCount; ++f) {
                const float* v = clip.frames.data() + static_cast<std::size_t>(f) * clip.frameStride + offset;
                authored.r.push_back({static_cast<int>(f), glm::quat(v[3], v[0], v[1], v[2])});
            }
            offset += 4;
        } else if (!nearlyEqual(track.constant, bind, ClipData::Rotation)) {
            authored.r.push_back({0, track.constant.r});
        }
        if (track.animated & ClipData::Scale) {
            for (std::uint32_t f = 0; f < clip.frameCount; ++f) {
                const float* v = clip.frames.data() + static_cast<std::size_t>(f) * clip.frameStride + offset;
                authored.s.push_back({static_cast<int>(f), glm::vec3(v[0], v[1], v[2])});
            }
        } else if (!nearlyEqual(track.constant, bind, ClipData::Scale)) {
            authored.s.push_back({0, track.constant.s});
        }
        if (!authored.empty())
            out.tracks.push_back(std::move(authored));
    }
    return out;
}

// Shortest decimal that round-trips the float
static double jsonFloat(float f)
{
    if (!std::isfinite(f))
        return 0.0;
    char buffer[32];
    const auto end = std::to_chars(buffer, buffer + sizeof(buffer), f).ptr;
    double d = 0.0;
    std::from_chars(buffer, end, d);
    return d;
}

static std::string hashText(std::uint32_t hash)
{
    char buffer[9];
    std::snprintf(buffer, sizeof(buffer), "%08" PRIx32, hash);
    return buffer;
}

static std::uint32_t parseHash(const json& j)
{
    if (!j.is_string())
        return 0;
    const std::string& text = j.get_ref<const std::string&>();
    std::uint32_t value = 0;
    std::from_chars(text.data(), text.data() + text.size(), value, 16);
    return value;
}

static json vec3Json(const glm::vec3& v)
{
    return json::array({jsonFloat(v.x), jsonFloat(v.y), jsonFloat(v.z)});
}

static json quatJson(const glm::quat& q)
{
    return json::array({jsonFloat(q.x), jsonFloat(q.y), jsonFloat(q.z), jsonFloat(q.w)});
}

static bool floats(const json& j, float* out, std::size_t count, std::size_t first = 0)
{
    if (!j.is_array() || j.size() != first + count)
        return false;
    for (std::size_t i = 0; i < count; ++i) {
        if (!j[first + i].is_number())
            return false;
        out[i] = j[first + i].get<float>();
    }
    return true;
}

static json trsJson(const JointTrs& trs)
{
    return json{{"t", vec3Json(trs.t)}, {"r", quatJson(trs.r)}, {"s", vec3Json(trs.s)}};
}

static JointTrs trsFrom(const json& j)
{
    JointTrs trs;
    float v[4];
    if (const auto it = j.find("t"); it != j.end() && floats(*it, v, 3))
        trs.t = glm::vec3(v[0], v[1], v[2]);
    if (const auto it = j.find("r"); it != j.end() && floats(*it, v, 4))
        trs.r = glm::normalize(glm::quat(v[3], v[0], v[1], v[2]));
    if (const auto it = j.find("s"); it != j.end() && floats(*it, v, 3))
        trs.s = glm::vec3(v[0], v[1], v[2]);
    return trs;
}

std::string saveAnimText(const AuthoredClip& clip, const ClipData& baked)
{
    json root;
    root["version"] = AnimVersion;
    root["type"] = "anim";
    root["skeleton"] = hashText(clip.skeletonHash);
    root["model"] = clip.model;
    root["rate"] = jsonFloat(clip.rate);
    root["frameCount"] = clip.frameCount;
    root["loop"] = clip.loop;

    json tracks = json::array();
    for (const AuthoredTrack& track : clip.tracks) {
        if (track.empty())
            continue;
        json t = json::array(), r = json::array(), s = json::array();
        for (const auto& k : track.t)
            t.push_back(json::array({k.frame, jsonFloat(k.value.x), jsonFloat(k.value.y), jsonFloat(k.value.z)}));
        for (const auto& k : track.r)
            r.push_back(json::array({k.frame, jsonFloat(k.value.x), jsonFloat(k.value.y), jsonFloat(k.value.z),
                                     jsonFloat(k.value.w)}));
        for (const auto& k : track.s)
            s.push_back(json::array({k.frame, jsonFloat(k.value.x), jsonFloat(k.value.y), jsonFloat(k.value.z)}));
        json jt{{"joint", track.joint}};
        if (!t.empty())
            jt["t"] = std::move(t);
        if (!r.empty())
            jt["r"] = std::move(r);
        if (!s.empty())
            jt["s"] = std::move(s);
        tracks.push_back(std::move(jt));
    }
    root["tracks"] = std::move(tracks);

    json bakedJson;
    bakedJson["frameCount"] = baked.frameCount;
    bakedJson["duration"] = jsonFloat(baked.duration);
    bakedJson["frameStride"] = baked.frameStride;
    json bakedTracks = json::array();
    for (const ClipData::Track& track : baked.tracks) {
        json jt = trsJson(track.constant);
        jt["jointHash"] = hashText(track.jointHash);
        jt["animated"] = track.animated;
        jt["offset"] = track.offset;
        bakedTracks.push_back(std::move(jt));
    }
    bakedJson["tracks"] = std::move(bakedTracks);
    json frames = json::array();
    for (const float f : baked.frames)
        frames.push_back(jsonFloat(f));
    bakedJson["frames"] = std::move(frames);
    root["baked"] = std::move(bakedJson);
    return root.dump(2);
}

static bool fail(std::string* error, const std::string& message)
{
    if (error)
        *error = message;
    return false;
}

template<typename T, int N>
static bool readKeys(const json& j, std::vector<Key<T>>& keys)
{
    if (!j.is_array())
        return false;
    for (const json& k : j) {
        float v[N];
        if (!k.is_array() || k.empty() || !k[0].is_number_integer() || !floats(k, v, N, 1))
            return false;
        T value;
        if constexpr (N == 4)
            value = glm::normalize(glm::quat(v[3], v[0], v[1], v[2]));
        else
            value = T(v[0], v[1], v[2]);
        setKey(keys, k[0].get<int>(), value);
    }
    return true;
}

bool loadAnimText(std::string_view text, AuthoredClip* keys, ClipData* baked, const std::string& name,
                  std::string* error)
{
    const json root = json::parse(text, nullptr, false);
    if (!root.is_object() || root.value("type", "") != "anim")
        return fail(error, "not an .anim file");
    if (root.value("version", 0) > AnimVersion)
        return fail(error, "made by a newer version");

    if (keys) {
        AuthoredClip clip;
        clip.skeletonHash = parseHash(root.value("skeleton", json()));
        clip.model = root.value("model", "");
        clip.rate = root.value("rate", ClipData::DefaultRate);
        clip.frameCount = std::max(root.value("frameCount", 1), 1);
        clip.loop = root.value("loop", true);
        if (const auto tracks = root.find("tracks"); tracks != root.end() && tracks->is_array()) {
            for (const json& jt : *tracks) {
                AuthoredTrack track{jt.value("joint", ""), {}, {}, {}};
                bool ok = !track.joint.empty();
                if (const auto it = jt.find("t"); ok && it != jt.end())
                    ok = readKeys<glm::vec3, 3>(*it, track.t);
                if (const auto it = jt.find("r"); ok && it != jt.end())
                    ok = readKeys<glm::quat, 4>(*it, track.r);
                if (const auto it = jt.find("s"); ok && it != jt.end())
                    ok = readKeys<glm::vec3, 3>(*it, track.s);
                if (!ok)
                    return fail(error, "bad keys for joint '" + track.joint + "'");
                clip.tracks.push_back(std::move(track));
            }
        }
        *keys = std::move(clip);
    }

    if (baked) {
        const auto b = root.find("baked");
        if (b == root.end() || !b->is_object())
            return fail(error, "no baked clip");
        ClipData clip;
        clip.name = name;
        clip.rate = root.value("rate", ClipData::DefaultRate);
        clip.loop = root.value("loop", true);
        clip.frameCount = b->value("frameCount", 0u);
        clip.duration = b->value("duration", 0.0f);
        clip.frameStride = b->value("frameStride", 0u);
        if (const auto tracks = b->find("tracks"); tracks != b->end() && tracks->is_array()) {
            for (const json& jt : *tracks) {
                ClipData::Track track;
                track.jointHash = parseHash(jt.value("jointHash", json()));
                track.animated = jt.value("animated", std::uint8_t(0));
                track.offset = jt.value("offset", 0u);
                track.constant = trsFrom(jt);
                clip.tracks.push_back(track);
            }
        }
        if (const auto frames = b->find("frames"); frames != b->end() && frames->is_array()) {
            clip.frames.reserve(frames->size());
            for (const json& f : *frames)
                clip.frames.push_back(f.is_number() ? f.get<float>() : 0.0f);
        }
        if (clip.frames.size() != static_cast<std::size_t>(clip.frameCount) * clip.frameStride)
            return fail(error, "baked frames don't match their size");
        for (const ClipData::Track& track : clip.tracks) {
            const std::uint32_t floatsUsed = (track.animated & 1 ? 3 : 0) + (track.animated & 2 ? 4 : 0) + (track.animated & 4 ? 3 : 0);
            if (track.offset + floatsUsed > clip.frameStride)
                return fail(error, "baked track out of range");
        }
        *baked = std::move(clip);
    }
    return true;
}

std::string savePoseText(const PoseAsset& pose)
{
    json root;
    root["version"] = AnimVersion;
    root["type"] = "pose";
    root["skeleton"] = hashText(pose.skeletonHash);
    root["model"] = pose.model;
    json joints = json::array();
    for (const PoseJoint& joint : pose.joints) {
        json j{{"joint", joint.joint}};
        j.update(trsJson(joint.local));
        joints.push_back(std::move(j));
    }
    root["joints"] = std::move(joints);
    return root.dump(2);
}

std::optional<PoseAsset> loadPoseText(std::string_view text, std::string* error)
{
    const json root = json::parse(text, nullptr, false);
    if (!root.is_object() || root.value("type", "") != "pose") {
        fail(error, "not a .pose file");
        return std::nullopt;
    }
    PoseAsset pose;
    pose.skeletonHash = parseHash(root.value("skeleton", json()));
    pose.model = root.value("model", "");
    if (const auto joints = root.find("joints"); joints != root.end() && joints->is_array()) {
        for (const json& j : *joints) {
            const std::string name = j.value("joint", "");
            if (!name.empty())
                pose.joints.push_back({name, trsFrom(j)});
        }
    }
    return pose;
}

std::optional<AnimHeader> readAnimHeader(std::string_view text)
{
    const json root = json::parse(text, nullptr, false);
    if (!root.is_object())
        return std::nullopt;
    const std::string type = root.value("type", "");
    if (type != "anim" && type != "pose")
        return std::nullopt;
    return AnimHeader{parseHash(root.value("skeleton", json())), root.value("model", "")};
}

std::string mirroredJointName(std::string_view name)
{
    const std::string n(name);
    struct Pair
    {
        const char* a;
        const char* b;
    };
    static constexpr Pair Suffixes[] = {{".L", ".R"}, {"_L", "_R"}, {".l", ".r"}, {"_l", "_r"}};
    for (const Pair& p : Suffixes) {
        for (const auto& [from, to] : {std::pair{p.a, p.b}, std::pair{p.b, p.a}}) {
            const std::string_view f(from);
            if (n.size() > f.size() && n.ends_with(f))
                return n.substr(0, n.size() - f.size()) + to;
        }
    }
    static constexpr Pair Prefixes[] = {{"L_", "R_"}, {"l_", "r_"}};
    for (const Pair& p : Prefixes) {
        for (const auto& [from, to] : {std::pair{p.a, p.b}, std::pair{p.b, p.a}}) {
            const std::string_view f(from);
            if (n.size() > f.size() && n.starts_with(f))
                return to + n.substr(f.size());
        }
    }
    static constexpr Pair Words[] = {{"Left", "Right"}, {"left", "right"}, {"LEFT", "RIGHT"}};
    for (const Pair& p : Words) {
        for (const auto& [from, to] : {std::pair{p.a, p.b}, std::pair{p.b, p.a}}) {
            if (const std::size_t at = n.find(from); at != std::string::npos)
                return n.substr(0, at) + to + n.substr(at + std::string_view(from).size());
        }
    }
    return {};
}

JointTrs mirrorLocal(const JointTrs& local, const JointTrs& fromBind, const JointTrs& toBind)
{
    // Mirroring across x: S * delta * S with S = diag(-1, 1, 1)
    const glm::quat delta = glm::inverse(fromBind.r) * local.r;
    const glm::quat mirrored(delta.w, delta.x, -delta.y, -delta.z);
    const glm::vec3 offset = local.t - fromBind.t;
    JointTrs out;
    out.r = glm::normalize(toBind.r * mirrored);
    out.t = toBind.t + glm::vec3(-offset.x, offset.y, offset.z);
    const glm::vec3 ratio(fromBind.s.x != 0.0f ? local.s.x / fromBind.s.x : 1.0f,
                          fromBind.s.y != 0.0f ? local.s.y / fromBind.s.y : 1.0f,
                          fromBind.s.z != 0.0f ? local.s.z / fromBind.s.z : 1.0f);
    out.s = toBind.s * ratio;
    return out;
}
