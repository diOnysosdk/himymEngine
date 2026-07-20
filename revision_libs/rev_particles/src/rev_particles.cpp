#include "rev_particles.h"

#include <cmath>

namespace rev {
namespace particles {

namespace {

constexpr float kPi = 3.14159265358979323846f;

uint32_t NextRandom(uint32_t* state) {
    *state = (*state * 1664525u) + 1013904223u;
    return *state;
}

float Random01(uint32_t* state) {
    return static_cast<float>(NextRandom(state) & 0x00ffffffu) / 16777215.0f;
}

float RandomRange(uint32_t* state, Range range) {
    return range.min + (range.max - range.min) * Random01(state);
}

float Length(Vec3 value) {
    return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

Vec3 Normalize(Vec3 value) {
    float length = Length(value);
    if (length <= 0.00001f) return {0.0f, 1.0f, 0.0f};
    return {value.x / length, value.y / length, value.z / length};
}

Vec3 SpawnDirection(uint32_t* state, const EmitterSettings& settings) {
    Vec3 direction = Normalize(settings.direction);
    float cone = settings.cone_angle_degrees * kPi / 180.0f;
    if (cone <= 0.00001f) return direction;

    float z = 1.0f - Random01(state) * (1.0f - std::cos(cone));
    float radius = std::sqrt(1.0f - z * z);
    float angle = Random01(state) * kPi * 2.0f;
    Vec3 local = {radius * std::cos(angle), radius * std::sin(angle), z};

    Vec3 helper = std::fabs(direction.y) < 0.95f ? Vec3{0.0f, 1.0f, 0.0f} : Vec3{1.0f, 0.0f, 0.0f};
    Vec3 tangent = Normalize({
        helper.y * direction.z - helper.z * direction.y,
        helper.z * direction.x - helper.x * direction.z,
        helper.x * direction.y - helper.y * direction.x});
    Vec3 bitangent = {
        direction.y * tangent.z - direction.z * tangent.y,
        direction.z * tangent.x - direction.x * tangent.z,
        direction.x * tangent.y - direction.y * tangent.x};
    return Normalize({
        tangent.x * local.x + bitangent.x * local.y + direction.x * local.z,
        tangent.y * local.x + bitangent.y * local.y + direction.y * local.z,
        tangent.z * local.x + bitangent.z * local.y + direction.z * local.z});
}

void KillParticle(ParticleSystem* system, Particle& particle) {
    particle.active = false;
    if (system->count > 0) --system->count;
}

void SpawnParticle(ParticleSystem* system) {
    if (!system || !system->particles || system->count >= system->capacity) return;

    Particle* particle = nullptr;
    for (int i = 0; i < system->capacity; ++i) {
        if (!system->particles[i].active) {
            particle = &system->particles[i];
            break;
        }
    }
    if (!particle) return;

    uint32_t seed = NextRandom(&system->random_state);
    Vec3 direction = SpawnDirection(&system->random_state, system->settings);
    float speed = RandomRange(&system->random_state, system->settings.speed);
    particle->position = system->settings.position;
    particle->velocity = {
        direction.x * speed, direction.y * speed, direction.z * speed};
    particle->acceleration = system->settings.acceleration;
    particle->rotation = RandomRange(&system->random_state, system->settings.rotation);
    particle->angular_velocity = RandomRange(&system->random_state, system->settings.angular_velocity);
    particle->scale = RandomRange(&system->random_state, system->settings.scale);
    particle->age = 0.0f;
    particle->lifetime = RandomRange(&system->random_state, system->settings.lifetime);
    particle->animation_speed = RandomRange(&system->random_state, system->settings.animation_speed);
    particle->seed = seed;
    particle->frame = 0;
    particle->active = particle->lifetime > 0.0f;
    if (particle->active) ++system->count;
}

} // namespace

bool Initialize(ParticleSystem* system, Particle* storage, int capacity,
                const EmitterSettings& settings) {
    if (!system || !storage || capacity <= 0 || settings.max_particles <= 0) return false;
    *system = {};
    system->particles = storage;
    system->capacity = capacity < settings.max_particles ? capacity : settings.max_particles;
    system->settings = settings;
    system->random_state = settings.seed ? settings.seed : 1u;
    Reset(system);
    return true;
}

void Reset(ParticleSystem* system) {
    if (!system || !system->particles) return;
    for (int i = 0; i < system->capacity; ++i) system->particles[i] = {};
    system->count = 0;
    system->time = 0.0f;
    system->emission_remainder = 0.0f;
    system->bursts_emitted = 0;
    system->random_state = system->settings.seed ? system->settings.seed : 1u;
}

void Update(ParticleSystem* system, float delta_time) {
    if (!system || !system->particles || delta_time <= 0.0f) return;

    system->time += delta_time;
    if (system->time < system->settings.start_delay) return;

    float local_time = system->time - system->settings.start_delay;
    if (system->settings.burst_count > 0 && system->bursts_emitted == 0) {
        for (int i = 0; i < system->settings.burst_count; ++i) SpawnParticle(system);
        system->bursts_emitted = 1;
    }

    bool emitting = system->settings.duration <= 0.0f || local_time <= system->settings.duration;
    if (emitting && system->settings.emission_rate > 0.0f) {
        system->emission_remainder += system->settings.emission_rate * delta_time;
        int spawn_count = static_cast<int>(system->emission_remainder);
        system->emission_remainder -= static_cast<float>(spawn_count);
        for (int i = 0; i < spawn_count; ++i) SpawnParticle(system);
    }

    for (int i = 0; i < system->capacity; ++i) {
        Particle& particle = system->particles[i];
        if (!particle.active) continue;
        particle.velocity.x += particle.acceleration.x * delta_time;
        particle.velocity.y += particle.acceleration.y * delta_time;
        particle.velocity.z += particle.acceleration.z * delta_time;
        float drag = 1.0f - system->settings.drag * delta_time;
        if (drag < 0.0f) drag = 0.0f;
        particle.velocity.x *= drag;
        particle.velocity.y *= drag;
        particle.velocity.z *= drag;
        particle.position.x += particle.velocity.x * delta_time;
        particle.position.y += particle.velocity.y * delta_time;
        particle.position.z += particle.velocity.z * delta_time;
        particle.rotation += particle.angular_velocity * delta_time;
        particle.age += delta_time;
        if (particle.age >= particle.lifetime) KillParticle(system, particle);
    }

    if (!system->settings.loop && system->settings.duration > 0.0f && local_time > system->settings.duration) {
        system->emission_remainder = 0.0f;
    }
}

int ActiveCount(const ParticleSystem* system) {
    return system ? system->count : 0;
}

const Particle* GetParticles(const ParticleSystem* system) {
    return system ? system->particles : nullptr;
}

} // namespace particles
} // namespace rev
