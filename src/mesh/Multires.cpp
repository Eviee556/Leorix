#include "mesh/Multires.h"
#include "mesh/Modifier.h"
#include <algorithm>
#include <cmath>

void Multires::Rebuild(const Mesh& base, int newLevels) {
    newLevels = std::clamp(newLevels, 0, 5);
    std::vector<Float3> oldDisp = displacement;
    Mesh oldCage = cage;

    Modifier sub;
    sub.type = ModifierType::Subdivide;
    sub.subdivLevels = std::max(1, newLevels);
    sub.subdivSmooth = true;
    cage = newLevels > 0 ? ApplyModifier(base, sub, {}) : base;
    cage.ComputeNormals();

    displacement.assign(cage.positions.size(), Float3{0, 0, 0});

    // Carry existing detail across a level change by taking each new vertex's displacement from
    // the nearest old vertex. Exact for an unchanged level, approximate when the level moves.
    if (!oldDisp.empty() && !oldCage.positions.empty()) {
        for (size_t i = 0; i < cage.positions.size(); ++i) {
            const Float3& p = cage.positions[i];
            float best = 1e30f;
            size_t bestIdx = 0;
            for (size_t j = 0; j < oldCage.positions.size(); ++j) {
                const Float3& q = oldCage.positions[j];
                float d = (p.x - q.x) * (p.x - q.x) + (p.y - q.y) * (p.y - q.y) + (p.z - q.z) * (p.z - q.z);
                if (d < best) { best = d; bestIdx = j; }
            }
            if (bestIdx < oldDisp.size()) displacement[i] = oldDisp[bestIdx];
        }
    }
    levels = newLevels;
    viewLevel = newLevels;
    valid = true;
}

Mesh Multires::Evaluate() const {
    Mesh out = cage;
    if (displacement.size() == out.positions.size())
        for (size_t i = 0; i < out.positions.size(); ++i) {
            out.positions[i].x += displacement[i].x;
            out.positions[i].y += displacement[i].y;
            out.positions[i].z += displacement[i].z;
        }
    out.ComputeNormals();
    out.ComputeBounds();
    return out;
}
