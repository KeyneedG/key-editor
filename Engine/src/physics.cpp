#include "tiny3d/physics.hpp"

#include <algorithm>
#include <stdexcept>

namespace tiny3d {
namespace {

Vec3 absolute(Vec3 v) { return {std::abs(v.x), std::abs(v.y), std::abs(v.z)}; }
bool finite(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

struct Shape {
    bool sphere = false;
    Vec3 center{}, halfSize{};
    float radius = 0;
};

bool shape(const Entity& entity, Shape& result) {
    const Transform& transform = entity.transform;
    const Vec3 scale = absolute(transform.scale);
    for (const auto& component : entity.components) {
        if (!component || !component->enabled) continue;
        if (const auto* box = dynamic_cast<const BoxCollider*>(component.get())) {
            if (!finite(box->size) || box->size.x <= 0 || box->size.y <= 0 || box->size.z <= 0) {
                throw std::invalid_argument("Box collider size must be positive and finite");
            }
            result.sphere = false;
            result.center = transform.point(box->center);
            const Vec3 half{box->size.x * scale.x * .5f, box->size.y * scale.y * .5f, box->size.z * scale.z * .5f};
            result.halfSize = absolute(rotate({half.x, 0, 0}, transform.rotation)) +
                absolute(rotate({0, half.y, 0}, transform.rotation)) +
                absolute(rotate({0, 0, half.z}, transform.rotation));
            if (!finite(result.center) || !finite(result.halfSize) ||
                result.halfSize.x <= 0 || result.halfSize.y <= 0 || result.halfSize.z <= 0) {
                throw std::invalid_argument("Invalid box collider transform");
            }
            return true;
        }
        if (const auto* sphere = dynamic_cast<const SphereCollider*>(component.get())) {
            result.sphere = true;
            result.center = transform.point(sphere->center);
            result.radius = sphere->radius * std::max({scale.x, scale.y, scale.z});
            if (!finite(result.center) || !std::isfinite(result.radius) || result.radius <= 0) {
                throw std::invalid_argument("Sphere collider radius and scale must be positive and finite");
            }
            return true;
        }
    }
    return false;
}

struct Contact {
    Vec3 normal{}; // Points from the first shape toward the second.
    float depth = 0;
};

// Smallest overlap selects a deterministic escape direction, also for contained shapes.
Contact minimumAxis(Vec3 overlap, Vec3 direction) {
    if (overlap.x <= overlap.y && overlap.x <= overlap.z) return {{direction.x >= 0 ? 1.f : -1.f, 0, 0}, overlap.x};
    if (overlap.y <= overlap.z) return {{0, direction.y >= 0 ? 1.f : -1.f, 0}, overlap.y};
    return {{0, 0, direction.z >= 0 ? 1.f : -1.f}, overlap.z};
}

bool sphereBox(const Shape& sphere, const Shape& box, Contact& contact) {
    const Vec3 low = box.center - box.halfSize, high = box.center + box.halfSize;
    const Vec3 closest{std::clamp(sphere.center.x, low.x, high.x),
                       std::clamp(sphere.center.y, low.y, high.y),
                       std::clamp(sphere.center.z, low.z, high.z)};
    const Vec3 offset = closest - sphere.center;
    const float distance = length(offset);
    if (distance >= sphere.radius) return false;
    if (distance > 1e-6f) contact = {offset * (1 / distance), sphere.radius - distance};
    else {
        const Vec3 difference = box.center - sphere.center;
        const Vec3 radius{sphere.radius, sphere.radius, sphere.radius};
        contact = minimumAxis(box.halfSize + radius - absolute(difference), difference);
    }
    return true;
}

bool collide(const Shape& a, const Shape& b, Contact& contact) {
    if (a.sphere && b.sphere) {
        const Vec3 offset = b.center - a.center;
        const float distance = length(offset), radius = a.radius + b.radius;
        if (distance >= radius) return false;
        contact = {distance > 1e-6f ? offset * (1 / distance) : Vec3{1, 0, 0}, radius - distance};
        return true;
    }
    if (a.sphere) return sphereBox(a, b, contact);
    if (b.sphere) {
        if (!sphereBox(b, a, contact)) return false;
        contact.normal = contact.normal * -1;
        return true;
    }
    const Vec3 difference = b.center - a.center;
    const Vec3 overlap = a.halfSize + b.halfSize - absolute(difference);
    if (overlap.x <= 0 || overlap.y <= 0 || overlap.z <= 0) return false;
    contact = minimumAxis(overlap, difference);
    return true;
}

float inverseMass(const Rigidbody* body) {
    if (!body || !body->enabled || body->isKinematic) return 0;
    if (!std::isfinite(body->mass) || body->mass <= 0 || !std::isfinite(1 / body->mass)) {
        throw std::invalid_argument("Rigidbody mass must be positive and finite");
    }
    return 1 / body->mass;
}

struct Body {
    Entity* entity;
    Rigidbody* rigidbody;
    Shape shape;
    float inverseMass;
};

void resolve(Body& a, Body& b, const Contact& contact) {
    const float total = a.inverseMass + b.inverseMass;
    const Vec3 correction = contact.normal * (contact.depth / total);
    const Vec3 changeA = correction * a.inverseMass, changeB = correction * b.inverseMass;
    a.entity->transform.position = a.entity->transform.position - changeA;
    b.entity->transform.position = b.entity->transform.position + changeB;
    a.shape.center = a.shape.center - changeA;
    b.shape.center = b.shape.center + changeB;
    const Vec3 velocityA = a.inverseMass > 0 ? a.rigidbody->velocity : Vec3{};
    const Vec3 velocityB = b.inverseMass > 0 ? b.rigidbody->velocity : Vec3{};
    const float closing = dot(velocityB - velocityA, contact.normal);
    if (closing < 0) {
        const Vec3 impulse = contact.normal * (-closing / total);
        if (a.inverseMass > 0) a.rigidbody->velocity = velocityA - impulse * a.inverseMass;
        if (b.inverseMass > 0) b.rigidbody->velocity = velocityB + impulse * b.inverseMass;
    }
}

} // namespace

void Physics::step(Scene& scene, float seconds) const {
    if (!std::isfinite(seconds) || seconds < 0 || !finite(gravity)) {
        throw std::invalid_argument("Physics time and gravity must be finite; time cannot be negative");
    }
    if (seconds == 0) return;
    const int steps = static_cast<int>(std::clamp(std::ceil(seconds * 120), 1.f, 128.f));
    const float elapsed = seconds / steps;
    std::vector<Body> bodies;
    bodies.reserve(scene.entities.size());
    for (int step = 0; step < steps; ++step) {
        bodies.clear();
        for (Entity& entity : scene.entities) {
            auto* body = entity.getComponent<Rigidbody>();
            const float weight = inverseMass(body);
            if (weight > 0) {
                if (body->useGravity) body->velocity = body->velocity + gravity * elapsed;
                if (!finite(body->velocity)) throw std::invalid_argument("Rigidbody velocity must be finite");
                entity.transform.position = entity.transform.position + body->velocity * elapsed;
            }
            Shape collider;
            if (shape(entity, collider)) bodies.push_back({&entity, body, collider, weight});
        }
        // A few passes reduce overlap when several bodies touch at once.
        for (int pass = 0; pass < 4; ++pass) {
            for (std::size_t i = 0; i < bodies.size(); ++i) {
                for (std::size_t j = i + 1; j < bodies.size(); ++j) {
                    if (bodies[i].inverseMass + bodies[j].inverseMass == 0) continue;
                    Contact contact;
                    if (collide(bodies[i].shape, bodies[j].shape, contact)) resolve(bodies[i], bodies[j], contact);
                }
            }
        }
    }
}

void Physics::move(Scene& scene, Entity& entity, Vec3 displacement) const {
    if (!finite(displacement)) throw std::invalid_argument("Physics movement must be finite");
    Shape moving;
    if (!shape(entity, moving)) {
        entity.transform.position = entity.transform.position + displacement;
        return;
    }
    const float extent = moving.sphere ? moving.radius : std::min({moving.halfSize.x, moving.halfSize.y, moving.halfSize.z});
    const float stepSize = std::clamp(extent * .5f, .001f, .1f);
    const int steps = static_cast<int>(std::clamp(std::ceil(length(displacement) / stepSize), 1.f, 128.f));
    const Vec3 increment = displacement * (1.f / steps);
    for (int step = 0; step < steps; ++step) {
        entity.transform.position = entity.transform.position + increment;
        for (int pass = 0; pass < 4; ++pass) {
            for (const Entity& other : scene.entities) {
                if (&other == &entity) continue;
                Shape obstacle;
                Contact contact;
                shape(entity, moving);
                if (shape(other, obstacle) && collide(moving, obstacle, contact)) {
                    entity.transform.position = entity.transform.position - contact.normal * contact.depth;
                }
            }
        }
    }
}

} // namespace tiny3d
