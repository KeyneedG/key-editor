#include "tiny3d/physics.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace tiny3d {
namespace {

Vector3 absolute(Vector3 v) { return {std::abs(v.x), std::abs(v.y), std::abs(v.z)}; }
bool finite(Vector3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

struct Shape {
    bool box = false;
    Vector3 center{}, halfSize{}, halfSegment{};
    float radius = 0;
};

bool shape(const Entity& entity, Shape& result) {
    const Transform& transform = entity.transform;
    const Vector3 scale = transform.lossyScale();
    for (const auto& component : entity.components) {
        if (!component || !component->enabled) continue;
        if (const auto* box = dynamic_cast<const BoxCollider*>(component.get())) {
            if (!finite(box->size) || box->size.x <= 0 || box->size.y <= 0 || box->size.z <= 0) {
                throw std::invalid_argument("Box collider size must be positive and finite");
            }
            result.box = true;
            result.center = transform.point(box->center);
            result.halfSize = absolute(transform.vector({box->size.x * .5f, 0, 0})) +
                absolute(transform.vector({0, box->size.y * .5f, 0})) +
                absolute(transform.vector({0, 0, box->size.z * .5f}));
            if (!finite(result.center) || !finite(result.halfSize) ||
                result.halfSize.x <= 0 || result.halfSize.y <= 0 || result.halfSize.z <= 0) {
                throw std::invalid_argument("Invalid box collider transform");
            }
            return true;
        }
        if (const auto* sphere = dynamic_cast<const SphereCollider*>(component.get())) {
            result.box = false;
            result.halfSegment = {};
            result.center = transform.point(sphere->center);
            result.radius = sphere->radius * std::max({scale.x, scale.y, scale.z});
            if (!finite(result.center) || !std::isfinite(result.radius) || result.radius <= 0) {
                throw std::invalid_argument("Sphere collider radius and scale must be positive and finite");
            }
            return true;
        }
        if (const auto* capsule = dynamic_cast<const CapsuleCollider*>(component.get())) {
            if (!std::isfinite(capsule->height) || !std::isfinite(capsule->radius) ||
                capsule->radius <= 0 || capsule->height < 2 * capsule->radius || !finite(scale) || scale.y <= 0) {
                throw std::invalid_argument("Capsule height must be at least twice its positive radius");
            }
            result.box = false;
            result.center = transform.point(capsule->center);
            result.radius = capsule->radius * std::max(scale.x, scale.z);
            result.halfSegment = normalized(transform.vector({0, 1, 0})) *
                std::max(0.f, capsule->height * scale.y * .5f - result.radius);
            if (!finite(result.center) || !finite(result.halfSegment) ||
                !std::isfinite(result.radius) || result.radius <= 0) {
                throw std::invalid_argument("Invalid capsule collider transform");
            }
            return true;
        }
    }
    return false;
}

struct Contact {
    Vector3 normal{}; // Points from the first shape toward the second.
    float depth = 0;
};

Contact minimumAxis(Vector3 overlap, Vector3 direction) {
    if (overlap.x <= overlap.y && overlap.x <= overlap.z) return {{direction.x >= 0 ? 1.f : -1.f, 0, 0}, overlap.x};
    if (overlap.y <= overlap.z) return {{0, direction.y >= 0 ? 1.f : -1.f, 0}, overlap.y};
    return {{0, 0, direction.z >= 0 ? 1.f : -1.f}, overlap.z};
}

Vector3 closestBoxPoint(Vector3 point, const Shape& box) {
    const Vector3 low = box.center - box.halfSize, high = box.center + box.halfSize;
    return {std::clamp(point.x, low.x, high.x), std::clamp(point.y, low.y, high.y),
        std::clamp(point.z, low.z, high.z)};
}

// Minimize segment-to-box distance in its piecewise quadratic intervals.
void closestSegmentBox(const Shape& round, const Shape& box, Vector3& onSegment, Vector3& onBox) {
    const Vector3 start = round.center - round.halfSegment, delta = round.halfSegment * 2;
    const Vector3 low = box.center - box.halfSize, high = box.center + box.halfSize;
    const float origin[3]{start.x, start.y, start.z}, direction[3]{delta.x, delta.y, delta.z};
    const float lo[3]{low.x, low.y, low.z}, hi[3]{high.x, high.y, high.z};
    // Unused entries stay at the final endpoint, so the whole array can be sorted.
    std::array<float, 8> cuts;
    cuts.fill(1);
    cuts[0] = 0;
    int count = 2;
    for (int axis = 0; axis < 3; ++axis) {
        if (direction[axis] == 0) continue;
        for (float boundary : {lo[axis], hi[axis]}) {
            const float t = (boundary - origin[axis]) / direction[axis];
            if (t > 0 && t < 1) cuts[count++] = t;
        }
    }
    std::sort(cuts.begin(), cuts.end());
    onSegment = start;
    onBox = closestBoxPoint(start, box);
    float best = dot(onSegment - onBox, onSegment - onBox);
    for (int i = 1; i < count; ++i) {
        const float middle = (cuts[i - 1] + cuts[i]) * .5f;
        float slope = 0, offset = 0;
        for (int axis = 0; axis < 3; ++axis) {
            const float coordinate = origin[axis] + direction[axis] * middle;
            if (coordinate >= lo[axis] && coordinate <= hi[axis]) continue;
            const float boundary = coordinate < lo[axis] ? lo[axis] : hi[axis];
            slope += direction[axis] * direction[axis];
            offset += direction[axis] * (origin[axis] - boundary);
        }
        const float t = slope > 0 ? std::clamp(-offset / slope, cuts[i - 1], cuts[i]) : middle;
        const Vector3 point = start + delta * t, closest = closestBoxPoint(point, box);
        const float distance = dot(point - closest, point - closest);
        if (distance < best) { best = distance; onSegment = point; onBox = closest; }
    }
}

void closestSegments(const Shape& a, const Shape& b, Vector3& pointA, Vector3& pointB) {
    const Vector3 startA = a.center - a.halfSegment, startB = b.center - b.halfSegment;
    const Vector3 da = a.halfSegment * 2, db = b.halfSegment * 2, offset = startA - startB;
    const float aa = dot(da, da), bb = dot(db, db), ab = dot(da, db);
    const float ar = dot(da, offset), br = dot(db, offset);
    float s = 0, t = 0;
    if (aa <= 1e-12f) t = bb > 1e-12f ? std::clamp(br / bb, 0.f, 1.f) : 0;
    else if (bb <= 1e-12f) s = std::clamp(-ar / aa, 0.f, 1.f);
    else {
        const float denominator = aa * bb - ab * ab;
        if (denominator > 1e-12f) s = std::clamp((ab * br - ar * bb) / denominator, 0.f, 1.f);
        t = (ab * s + br) / bb;
        if (t < 0) { t = 0; s = std::clamp(-ar / aa, 0.f, 1.f); }
        else if (t > 1) { t = 1; s = std::clamp((ab - ar) / aa, 0.f, 1.f); }
    }
    pointA = startA + da * s;
    pointB = startB + db * t;
}

bool roundBox(const Shape& round, const Shape& box, Contact& contact) {
    Vector3 onSegment, onBox;
    closestSegmentBox(round, box, onSegment, onBox);
    const Vector3 offset = onBox - onSegment;
    const float distance = length(offset);
    if (distance >= round.radius) return false;
    if (distance > 1e-6f) contact = {offset * (1 / distance), round.radius - distance};
    else {
        // If the center line enters the box, move the entire capsule out along one axis.
        const Vector3 radius{round.radius, round.radius, round.radius};
        contact = minimumAxis(box.halfSize + absolute(round.halfSegment) + radius -
            absolute(box.center - round.center), box.center - round.center);
    }
    return true;
}

bool collide(const Shape& a, const Shape& b, Contact& contact) {
    if (!a.box && !b.box) {
        Vector3 pointA, pointB;
        closestSegments(a, b, pointA, pointB);
        const Vector3 offset = pointB - pointA;
        const float distance = length(offset), radius = a.radius + b.radius;
        if (distance >= radius) return false;
        Vector3 normal = normalized(offset);
        if (distance <= 1e-6f) {
            normal = cross(a.halfSegment, b.halfSegment);
            if (length(normal) <= 1e-6f) {
                const Vector3 axis = length(a.halfSegment) > 0 ? a.halfSegment : b.halfSegment;
                normal = cross(axis, std::abs(axis.z) < length(axis) * .9f ? Vector3{0, 0, 1} : Vector3{0, 1, 0});
            }
            normal = length(normal) > 0 ? normalized(normal) : Vector3{1, 0, 0};
        }
        contact = {normal, radius - distance};
        return true;
    }
    if (!a.box) return roundBox(a, b, contact);
    if (!b.box) {
        if (!roundBox(b, a, contact)) return false;
        contact.normal = contact.normal * -1;
        return true;
    }
    const Vector3 difference = b.center - a.center;
    const Vector3 overlap = a.halfSize + b.halfSize - absolute(difference);
    if (overlap.x <= 0 || overlap.y <= 0 || overlap.z <= 0) return false;
    contact = minimumAxis(overlap, difference);
    return true;
}

bool related(const Entity& a, const Entity& b) {
    for (const Entity* e = &a; e; e = e->parent()) if (e == &b) return true;
    for (const Entity* e = &b; e; e = e->parent()) if (e == &a) return true;
    return false;
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
    float inverseMass;
};

void resolve(Body& a, Body& b, const Contact& contact) {
    const float total = a.inverseMass + b.inverseMass;
    const Vector3 correction = contact.normal * (contact.depth / total);
    if (a.inverseMass > 0) a.entity->transform.setPosition(a.entity->transform.position() - correction * a.inverseMass);
    if (b.inverseMass > 0) b.entity->transform.setPosition(b.entity->transform.position() + correction * b.inverseMass);
    const Vector3 velocityA = a.inverseMass > 0 ? a.rigidbody->velocity : Vector3{};
    const Vector3 velocityB = b.inverseMass > 0 ? b.rigidbody->velocity : Vector3{};
    const float closing = dot(velocityB - velocityA, contact.normal);
    if (closing < 0) {
        const Vector3 impulse = contact.normal * (-closing / total);
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
                entity.transform.setPosition(entity.transform.position() + body->velocity * elapsed);
            }
            Shape collider;
            if (shape(entity, collider)) bodies.push_back({&entity, body, weight});
        }
        for (int pass = 0; pass < 4; ++pass) {
            for (std::size_t i = 0; i < bodies.size(); ++i) {
                for (std::size_t j = i + 1; j < bodies.size(); ++j) {
                    Body& a = bodies[i]; Body& b = bodies[j];
                    if (a.inverseMass + b.inverseMass == 0 || related(*a.entity, *b.entity)) continue;
                    Shape shapeA, shapeB;
                    shape(*a.entity, shapeA); shape(*b.entity, shapeB);
                    Contact contact;
                    if (collide(shapeA, shapeB, contact)) resolve(a, b, contact);
                }
            }
        }
    }
}

void Physics::move(Scene& scene, Entity& entity, Vector3 displacement) const {
    if (!finite(displacement)) throw std::invalid_argument("Physics movement must be finite");
    Shape moving;
    if (!shape(entity, moving)) {
        entity.transform.setPosition(entity.transform.position() + displacement);
        return;
    }
    const float extent = moving.box ? std::min({moving.halfSize.x, moving.halfSize.y, moving.halfSize.z}) : moving.radius;
    const float stepSize = std::clamp(extent * .5f, .001f, .1f);
    const int steps = static_cast<int>(std::clamp(std::ceil(length(displacement) / stepSize), 1.f, 128.f));
    const Vector3 increment = displacement * (1.f / steps);
    for (int step = 0; step < steps; ++step) {
        entity.transform.setPosition(entity.transform.position() + increment);
        for (int pass = 0; pass < 4; ++pass) {
            for (const Entity& other : scene.entities) {
                if (related(entity, other)) continue;
                Shape obstacle;
                Contact contact;
                shape(entity, moving);
                if (shape(other, obstacle) && collide(moving, obstacle, contact)) {
                    entity.transform.setPosition(entity.transform.position() - contact.normal * contact.depth);
                }
            }
        }
    }
}

} // namespace tiny3d
