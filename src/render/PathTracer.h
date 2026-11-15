#pragma once
// A CPU path tracer, so the Ray Tracing settings drive a real render rather than sitting in a
// panel. It runs on worker threads and reports progress, and the result can be saved to PNG.
// The GPU (Vulkan RT) backend replaces the intersection core later; the shading model here is
// what it will mirror.
#include "mesh/Scene.h"
#include "render/Camera.h"
#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

struct RayTraceSettings {
    bool  denoise = true;              // edge-aware blur over the result
    float denoiseStrength = 1.0f;
    bool  emissiveLights = true;       // emissive surfaces contribute light
    bool  lightSampling = true;        // next-event estimation toward emissive triangles (MIS)
    int   width = 960, height = 540;
    int   samples = 64;                 // paths per pixel
    int   maxBounces = 6;
    int   diffuseBounces = 4, glossyBounces = 4, transmissionBounces = 8;
    float clampIndirect = 8.0f;         // fireflies
    bool  clampEnabled = true;
    int   seed = 0;
    bool  transparentFilm = false;
    float exposure = 1.0f;
    int   threads = 0;                  // 0 = hardware concurrency
    bool  shadows = true;
    float sunAngleDeg = 1.0f;           // soft shadow width
};

struct RayTraceStats {
    double seconds = 0.0;
    size_t triangles = 0, rays = 0;
    int    threadsUsed = 0;
};

class PathTracer {
public:
    ~PathTracer();
    // Starts a render in the background. Returns false if one is already running.
    bool Start(const Scene& scene, const SceneCamera& camera, float viewYaw, float viewPitch,
               float viewDistance, const Float3& viewTarget, bool useSceneCamera,
               const RayTraceSettings& settings, const struct RayTraceWorld& world);
    void Cancel();
    bool Running() const { return running; }
    float Progress() const { return progress; }
    const std::vector<uint8_t>& Pixels() const { return pixels; }   // RGBA8
    int Width() const { return width; }
    int Height() const { return height; }
    bool HasResult() const { return !pixels.empty() && !running; }
    const RayTraceStats& Stats() const { return stats; }
    bool SavePng(const std::string& path) const;

private:
    void Join();
    void Denoise(float strength);
    std::vector<std::thread> workers;
    std::atomic<bool> running{false}, cancel{false};
    std::atomic<float> progress{0.0f};
    std::atomic<int>  nextRow{0};
    std::vector<uint8_t> pixels;
    int width = 0, height = 0;
    RayTraceStats stats;
};

// An equirectangular environment image. Shared by the viewport background and the tracer.
struct HdriImage {
    int width = 0, height = 0;
    std::vector<float> pixels;         // RGB float, linear
    bool Loaded() const { return width > 0 && !pixels.empty(); }
    void Sample(float dx, float dy, float dz, float rotationRad, float out[3]) const;
    bool Load(const std::string& path, std::string& error);
    void Clear() { width = height = 0; pixels.clear(); }
};

// The environment the tracer lights with, mirroring the Environment panel.
struct RayTraceWorld {
    const HdriImage* hdri = nullptr;
    float hdriRotation = 0.0f;         // radians
    float hdriStrength = 1.0f;
    Float3 sunDir{0.4f, 0.75f, 0.5f};
    float  sunColor[3] = {1.0f, 0.96f, 0.9f};
    float  sunStrength = 3.0f;
    float  skyColor[3] = {0.16f, 0.18f, 0.22f};
    float  horizonColor[3] = {0.07f, 0.08f, 0.10f};
    float  skyStrength = 1.0f;
    int    background = 1;
};
