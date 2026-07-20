#pragma once

#include <cstddef>
#include <cstdint>

namespace rev {
namespace particles {

struct Vec3 {
    float x;
    float y;
    float z;
};

enum SimulationSpace {
    SimulationSpaceLocal = 0,
    SimulationSpaceWorld = 1,
};

enum VisualSource {
    VisualSourceAsset = 0,
    VisualSourcePrimitive = 1,
};

enum PrimitiveShape {
    PrimitiveShapeSquare = 0,
    PrimitiveShapeCircle = 1,
    PrimitiveShapeTriangle = 2,
    PrimitiveShapeDiamond = 3,
};

struct Range {
    float min;
    float max;
};

struct Particle {
    Vec3 position;
    Vec3 velocity;
    Vec3 acceleration;
    float rotation;
    float angular_velocity;
    float scale;
    float age;
    float lifetime;
    float animation_speed;
    uint32_t seed;
    uint16_t frame;
    bool active;
};

struct EmitterSettings {
    uint32_t seed;
    VisualSource visual_source;
    PrimitiveShape primitive_shape;
    int max_particles;
    float emission_rate;
    int burst_count;
    float duration;
    bool loop;
    float start_delay;
    SimulationSpace simulation_space;
    Vec3 position;
    Vec3 direction;
    float cone_angle_degrees;
    Range speed;
    Range lifetime;
    Range scale;
    Range rotation;
    Range angular_velocity;
    Range animation_speed;
    Vec3 acceleration;
    float drag;
};

struct ParticleSystem {
    Particle* particles;
    int capacity;
    int count;
    float time;
    float emission_remainder;
    int bursts_emitted;
    uint32_t random_state;
    EmitterSettings settings;
};

bool Initialize(ParticleSystem* system, Particle* storage, int capacity,
                const EmitterSettings& settings);
void Reset(ParticleSystem* system);
void Update(ParticleSystem* system, float delta_time);
int ActiveCount(const ParticleSystem* system);
const Particle* GetParticles(const ParticleSystem* system);

} // namespace particles
} // namespace rev
