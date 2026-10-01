#pragma once

#include "tiny3d/engine.hpp"

namespace tiny3d {

struct BoxCollider : Component {
    Vec3 center{};
    Vec3 size{1, 1, 1}; // Full local size; collision uses world axis-aligned bounds.
};

struct SphereCollider : Component {
    Vec3 center{};
    float radius = .5f; // Local radius; scaled by the largest absolute scale axis.
};

struct Rigidbody : Component {
    Vec3 velocity{};
    float mass = 1;
    bool useGravity = true;
    bool isKinematic = false; // Moved by scripts, unaffected by gravity and impulses.
};

class Physics {
public:
    Vec3 gravity{0, -9.81f, 0};
    void step(Scene& scene, float seconds) const;
    // Scripted movement with collision correction, including for kinematic bodies.
    void move(Scene& scene, Entity& entity, Vec3 displacement) const;
};

} // namespace tiny3d
