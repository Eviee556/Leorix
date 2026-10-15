#pragma once
// Keyframe animation. Channels address one value each: an object's or a bone's location,
// rotation or scale on one axis, which is how Blender's F-curves work.
#include <string>
#include <vector>

// Blender splits this in two: an interpolation mode, and for the easing equations, which side
// of the key they ease on.
enum class Interp { Constant, Linear, Bezier, Sine, Quad, Cubic, Quart, Expo, Circ, Back, Bounce, Elastic, Count };
enum class Easing { In, Out, InOut };
const char* InterpName(Interp i);
const char* EasingName(Easing e);

struct Keyframe {
    int    frame = 0;
    float  value = 0.0f;
    Interp interp = Interp::Bezier;
    Easing easing = Easing::Out;       // ignored by Constant, Linear and Bezier
};

enum class ChannelTarget { Object, Bone };
enum class ChannelProp { Location, Rotation, Scale };

struct Channel {
    ChannelTarget target = ChannelTarget::Object;
    int           boneIndex = -1;         // when target == Bone
    ChannelProp   prop = ChannelProp::Location;
    int           axis = 0;               // 0 X, 1 Y, 2 Z
    std::vector<Keyframe> keys;           // kept sorted by frame

    void Insert(int frame, float value, Interp interp = Interp::Bezier, Easing easing = Easing::Out);
    void SetInterp(Interp interp, Easing easing);      // applies to every key in the channel
    void RemoveAt(int frame);
    bool HasKeyAt(int frame) const;
    bool Evaluate(float frame, float& out) const;
    std::string Label(const std::vector<std::string>& boneNames) const;
};

struct Action {
    std::string          name = "Action";
    std::vector<Channel> channels;
    int frameStart = 1, frameEnd = 120;

    Channel& Find(ChannelTarget t, int bone, ChannelProp p, int axis);
    const Channel* Get(ChannelTarget t, int bone, ChannelProp p, int axis) const;
    bool Empty() const;
    int  LastKeyFrame() const;
};

struct PlaybackState {
    float frame = 1.0f;
    bool  playing = false;
    bool  loop = true;
    int   fps = 24;
};
