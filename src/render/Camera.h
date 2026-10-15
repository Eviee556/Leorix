#pragma once
// A scene camera, with the same controls the Render Settings panel exposes, so what you frame in
// the viewport is what renders. Perspective or orthographic, like Blender's camera data.
#include "mesh/Mesh.h"
#include <algorithm>
#include <cmath>

struct SceneCamera {
    // placement, stored the same way the viewport orbit camera is, so "align to view" is exact
    Float3 target{0, 1, 0};
    float  yaw = 0.7f, pitch = 0.42f, distance = 7.0f;

    bool   orthographic = false;
    float  orthoScale = 3.0f;
    float  focalLengthMm = 50.0f;
    float  sensorWidthMm = 36.0f;      // full frame, Blender's default
    float  clipStart = 0.1f, clipEnd = 1000.0f;

    // depth of field
    bool   dof = false;
    float  fStop = 5.6f;
    float  focusDistance = 7.0f;

    bool   showPassepartout = true;
    float  passepartoutAlpha = 0.6f;

    // Horizontal FOV from focal length and sensor size, then vertical for the viewport.
    float FovYDegrees(float aspect) const {
        float fovX = 2.0f * std::atan(sensorWidthMm * 0.5f / std::max(1.0f, focalLengthMm));
        float fovY = 2.0f * std::atan(std::tan(fovX * 0.5f) / std::max(0.01f, aspect));
        return fovY * 180.0f / 3.14159265f;
    }
    Float3 Eye() const {
        float cp = std::cos(pitch);
        return {target.x - std::sin(yaw) * cp * distance,
                target.y - std::sin(pitch) * distance * -1.0f,
                target.z - std::cos(yaw) * cp * distance};
    }
};
