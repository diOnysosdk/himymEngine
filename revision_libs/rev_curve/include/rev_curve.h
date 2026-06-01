#pragma once

namespace rev {
namespace curve {

// Easing modes for interpolation between curve points
enum class EaseMode {
    Linear,      // Simple linear interpolation
    EaseIn,      // Accelerate into the segment
    EaseOut,     // Decelerate at the end
    EaseInOut,   // Smooth S-curve
    Smoothstep,  // Hermite interpolation (3t^2 - 2t^3)
    Hold         // Hold previous value (step function)
};

// Wrap modes for curve behavior outside 0-1 range
enum class WrapMode {
    Clamp,       // Hold first/last value (default)
    Loop,        // Repeat the curve (0-1, 0-1, ...)
    PingPong,    // Bounce back and forth (0-1, 1-0, 0-1, ...)
    Mirror       // Mirror the curve (0-1, 1-0 mirrored, 0-1, ...)
};

// A single point on a curve
struct Point {
    float t;          // Time (normalized 0-1 or absolute)
    float v;          // Value at this time
    float in_ease;    // In tangent weight (0-1)
    float out_ease;   // Out tangent weight (0-1)
    EaseMode mode;    // Interpolation mode to next point
};

// A curve defined by multiple points
struct Curve {
    Point* points;
    int point_count;
    int capacity;
    WrapMode wrap_mode;  // How to handle t < 0 or t > 1
    float duration;      // How long the curve takes to complete (in seconds)
};

// Curve creation and destruction
Curve CreateCurve(int reserve_points = 16);
void DestroyCurve(Curve& curve);

// Curve building
void AddPoint(Curve& curve, float t, float v, EaseMode mode = EaseMode::Linear);
void AddPoint(Curve& curve, const Point& point);
void SortPoints(Curve& curve);  // Sort by time (call after adding all points)

// Curve evaluation
float Evaluate(const Curve& curve, float t);
float EvaluateClamped(const Curve& curve, float t, float min_val, float max_val);

}  // namespace curve
}  // namespace rev
