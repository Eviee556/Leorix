#include "anim/Animation.h"
#include <algorithm>
#include <algorithm>
#include <cmath>

const char* InterpName(Interp i) {
    switch (i) {
        case Interp::Constant: return "Constant"; case Interp::Linear: return "Linear";
        case Interp::Bezier: return "Bezier"; case Interp::Sine: return "Sinusoidal";
        case Interp::Quad: return "Quadratic"; case Interp::Cubic: return "Cubic";
        case Interp::Quart: return "Quartic"; case Interp::Expo: return "Exponential";
        case Interp::Circ: return "Circular"; case Interp::Back: return "Back";
        case Interp::Bounce: return "Bounce"; case Interp::Elastic: return "Elastic";
        default: return "";
    }
}
const char* EasingName(Easing e) {
    switch (e) { case Easing::In: return "Ease In"; case Easing::Out: return "Ease Out"; default: return "Ease In-Out"; }
}

namespace {
// The standard Penner easing equations, on t in 0..1.
float EaseIn(Interp i, float t) {
    switch (i) {
        case Interp::Sine:  return 1.0f - std::cos(t * 1.57079633f);
        case Interp::Quad:  return t * t;
        case Interp::Cubic: return t * t * t;
        case Interp::Quart: return t * t * t * t;
        case Interp::Expo:  return t <= 0.0f ? 0.0f : std::pow(2.0f, 10.0f * (t - 1.0f));
        case Interp::Circ:  return 1.0f - std::sqrt(std::max(0.0f, 1.0f - t * t));
        case Interp::Back:  { const float s = 1.70158f; return t * t * ((s + 1) * t - s); }
        case Interp::Elastic: {
            if (t <= 0) return 0; if (t >= 1) return 1;
            return -std::pow(2.0f, 10.0f * (t - 1)) * std::sin((t - 1.075f) * 6.2831853f / 0.3f);
        }
        case Interp::Bounce: {
            float u = 1.0f - t, r;
            if (u < 1 / 2.75f) r = 7.5625f * u * u;
            else if (u < 2 / 2.75f) { u -= 1.5f / 2.75f; r = 7.5625f * u * u + 0.75f; }
            else if (u < 2.5f / 2.75f) { u -= 2.25f / 2.75f; r = 7.5625f * u * u + 0.9375f; }
            else { u -= 2.625f / 2.75f; r = 7.5625f * u * u + 0.984375f; }
            return 1.0f - r;
        }
        default: return t;
    }
}
float Ease(Interp i, Easing e, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    switch (e) {
        case Easing::In:  return EaseIn(i, t);
        case Easing::Out: return 1.0f - EaseIn(i, 1.0f - t);
        default: return t < 0.5f ? EaseIn(i, t * 2.0f) * 0.5f
                                 : 1.0f - EaseIn(i, (1.0f - t) * 2.0f) * 0.5f;
    }
}
} // namespace

void Channel::SetInterp(Interp interp, Easing easing) {
    for (auto& k : keys) { k.interp = interp; k.easing = easing; }
}

void Channel::Insert(int frame, float value, Interp interp, Easing easing) {
    for (auto& k : keys)
        if (k.frame == frame) { k.value = value; k.interp = interp; k.easing = easing; return; }
    keys.push_back({frame, value, interp, easing});
    std::sort(keys.begin(), keys.end(), [](const Keyframe& a, const Keyframe& b) { return a.frame < b.frame; });
}

void Channel::RemoveAt(int frame) {
    keys.erase(std::remove_if(keys.begin(), keys.end(), [&](const Keyframe& k) { return k.frame == frame; }), keys.end());
}

bool Channel::HasKeyAt(int frame) const {
    for (const auto& k : keys) if (k.frame == frame) return true;
    return false;
}

bool Channel::Evaluate(float frame, float& out) const {
    if (keys.empty()) return false;
    if (frame <= keys.front().frame) { out = keys.front().value; return true; }
    if (frame >= keys.back().frame)  { out = keys.back().value; return true; }
    for (size_t i = 0; i + 1 < keys.size(); ++i) {
        const Keyframe& a = keys[i];
        const Keyframe& b = keys[i + 1];
        if (frame < a.frame || frame > b.frame) continue;
        float span = static_cast<float>(b.frame - a.frame);
        float t = span > 0 ? (frame - a.frame) / span : 0.0f;
        switch (a.interp) {
            case Interp::Constant: out = a.value; break;
            case Interp::Linear:   out = a.value + (b.value - a.value) * t; break;
            case Interp::Bezier:   out = a.value + (b.value - a.value) * (t * t * (3.0f - 2.0f * t)); break;
            default:               out = a.value + (b.value - a.value) * Ease(a.interp, a.easing, t); break;
        }
        return true;
    }
    out = keys.back().value;
    return true;
}

std::string Channel::Label(const std::vector<std::string>& boneNames) const {
    const char* axes = "XYZ";
    const char* p = prop == ChannelProp::Location ? "Location" : prop == ChannelProp::Rotation ? "Rotation" : "Scale";
    std::string who = "Object";
    if (target == ChannelTarget::Bone)
        who = (boneIndex >= 0 && boneIndex < (int)boneNames.size()) ? boneNames[boneIndex] : "Bone";
    return who + "  " + p + " " + axes[std::clamp(axis, 0, 2)];
}

Channel& Action::Find(ChannelTarget t, int bone, ChannelProp p, int axis) {
    for (auto& c : channels)
        if (c.target == t && c.boneIndex == bone && c.prop == p && c.axis == axis) return c;
    Channel c;
    c.target = t; c.boneIndex = bone; c.prop = p; c.axis = axis;
    channels.push_back(c);
    return channels.back();
}

const Channel* Action::Get(ChannelTarget t, int bone, ChannelProp p, int axis) const {
    for (const auto& c : channels)
        if (c.target == t && c.boneIndex == bone && c.prop == p && c.axis == axis) return &c;
    return nullptr;
}

bool Action::Empty() const {
    for (const auto& c : channels) if (!c.keys.empty()) return false;
    return true;
}

int Action::LastKeyFrame() const {
    int last = 0;
    for (const auto& c : channels)
        if (!c.keys.empty()) last = std::max(last, c.keys.back().frame);
    return last;
}
