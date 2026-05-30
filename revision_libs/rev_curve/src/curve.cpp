#include "rev_curve.h"
#include <cstring>
#include <algorithm>

namespace rev {
namespace curve {

// Easing functions
static float EaseInQuad(float t) {
    return t * t;
}

static float EaseOutQuad(float t) {
    return t * (2.0f - t);
}

static float EaseInOutQuad(float t) {
    if (t < 0.5f) {
        return 2.0f * t * t;
    } else {
        return -1.0f + (4.0f - 2.0f * t) * t;
    }
}

static float Smoothstep(float t) {
    return t * t * (3.0f - 2.0f * t);
}

// Linear interpolation
static float Lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

// Apply easing mode to normalized segment time
static float ApplyEasing(float t, EaseMode mode) {
    switch (mode) {
        case EaseMode::Linear:
            return t;
        case EaseMode::EaseIn:
            return EaseInQuad(t);
        case EaseMode::EaseOut:
            return EaseOutQuad(t);
        case EaseMode::EaseInOut:
            return EaseInOutQuad(t);
        case EaseMode::Smoothstep:
            return Smoothstep(t);
        case EaseMode::Hold:
            return 0.0f;  // Always return start value
        default:
            return t;
    }
}

Curve CreateCurve(int reserve_points) {
    Curve curve;
    curve.points = new Point[reserve_points];
    curve.point_count = 0;
    curve.capacity = reserve_points;
    return curve;
}

void DestroyCurve(Curve& curve) {
    if (curve.points) {
        delete[] curve.points;
        curve.points = nullptr;
    }
    curve.point_count = 0;
    curve.capacity = 0;
}

void AddPoint(Curve& curve, float t, float v, EaseMode mode) {
    Point point;
    point.t = t;
    point.v = v;
    point.in_ease = 0.5f;
    point.out_ease = 0.5f;
    point.mode = mode;
    AddPoint(curve, point);
}

void AddPoint(Curve& curve, const Point& point) {
    if (curve.point_count >= curve.capacity) {
        // Grow array
        int new_capacity = curve.capacity * 2;
        Point* new_points = new Point[new_capacity];
        memcpy(new_points, curve.points, curve.point_count * sizeof(Point));
        delete[] curve.points;
        curve.points = new_points;
        curve.capacity = new_capacity;
    }
    
    curve.points[curve.point_count++] = point;
}

void SortPoints(Curve& curve) {
    // Simple bubble sort (fine for small arrays)
    for (int i = 0; i < curve.point_count - 1; ++i) {
        for (int j = 0; j < curve.point_count - i - 1; ++j) {
            if (curve.points[j].t > curve.points[j + 1].t) {
                Point temp = curve.points[j];
                curve.points[j] = curve.points[j + 1];
                curve.points[j + 1] = temp;
            }
        }
    }
}

float Evaluate(const Curve& curve, float t) {
    if (curve.point_count == 0) {
        return 0.0f;
    }
    
    if (curve.point_count == 1) {
        return curve.points[0].v;
    }
    
    // Before first point
    if (t <= curve.points[0].t) {
        return curve.points[0].v;
    }
    
    // After last point
    if (t >= curve.points[curve.point_count - 1].t) {
        return curve.points[curve.point_count - 1].v;
    }
    
    // Find segment containing t
    for (int i = 0; i < curve.point_count - 1; ++i) {
        const Point& p0 = curve.points[i];
        const Point& p1 = curve.points[i + 1];
        
        if (t >= p0.t && t <= p1.t) {
            // Normalize t within segment
            float segment_t = (t - p0.t) / (p1.t - p0.t);
            
            // Apply easing
            float eased_t = ApplyEasing(segment_t, p0.mode);
            
            // Interpolate
            return Lerp(p0.v, p1.v, eased_t);
        }
    }
    
    // Fallback
    return curve.points[curve.point_count - 1].v;
}

float EvaluateClamped(const Curve& curve, float t, float min_val, float max_val) {
    float value = Evaluate(curve, t);
    
    if (value < min_val) return min_val;
    if (value > max_val) return max_val;
    
    return value;
}

}  // namespace curve
}  // namespace rev
