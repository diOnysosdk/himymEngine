#include "rev_particles.h"

#include <cmath>
#include <cstdio>

namespace {

constexpr float kPi = 3.14159265358979323846f;

bool NearlyEqual(float a, float b, float epsilon = 0.0001f) {
    return std::fabs(a - b) <= epsilon;
}

bool TestStraightDirection() {
    rev::particles::Particle particles[4] = {};
    rev::particles::ParticleSystem system = {};
    rev::particles::EmitterSettings settings = {};
    settings.seed = 7;
    settings.max_particles = 4;
    settings.burst_count = 1;
    settings.direction = {1.0f, 0.0f, 0.0f};
    settings.cone_angle_degrees = 0.0f;
    settings.speed = {2.0f, 2.0f};
    settings.lifetime = {1.0f, 1.0f};
    settings.scale = {1.0f, 1.0f};

    if (!rev::particles::Initialize(&system, particles, 4, settings)) return false;
    rev::particles::Update(&system, 0.01f);
    return particles[0].active &&
           NearlyEqual(particles[0].velocity.x, 2.0f) &&
           NearlyEqual(particles[0].velocity.y, 0.0f) &&
           NearlyEqual(particles[0].velocity.z, 0.0f);
}

bool TestFlatFullConeSpread() {
    rev::particles::Particle particles[128] = {};
    rev::particles::ParticleSystem system = {};
    rev::particles::EmitterSettings settings = {};
    settings.seed = 11;
    settings.max_particles = 128;
    settings.burst_count = 128;
    settings.direction = {0.0f, -1.0f, 0.0f};
    settings.cone_angle_degrees = 90.0f;
    settings.speed = {1.0f, 1.0f};
    settings.lifetime = {1.0f, 1.0f};
    settings.scale = {1.0f, 1.0f};

    if (!rev::particles::Initialize(&system, particles, 128, settings)) return false;
    rev::particles::Update(&system, 0.01f);
    for (const rev::particles::Particle& particle : particles) {
        if (!particle.active) return false;
        float speed = std::sqrt(particle.velocity.x * particle.velocity.x +
                                particle.velocity.y * particle.velocity.y);
        if (!NearlyEqual(speed, 1.0f) || !NearlyEqual(particle.velocity.z, 0.0f)) return false;
        float angle = std::atan2(particle.velocity.y, particle.velocity.x);
        float delta = angle - (-kPi * 0.5f);
        if (delta < -kPi) delta += 2.0f * kPi;
        if (delta > kPi) delta -= 2.0f * kPi;
        if (std::fabs(delta) > kPi * 0.25f + 0.0001f) return false;
    }
    return true;
}

} // namespace

int main() {
    if (!TestStraightDirection()) {
        std::fprintf(stderr, "[particle_direction_tests] straight direction failed\n");
        return 1;
    }
    if (!TestFlatFullConeSpread()) {
        std::fprintf(stderr, "[particle_direction_tests] cone spread failed\n");
        return 1;
    }
    std::printf("[particle_direction_tests] PASS\n");
    return 0;
}
