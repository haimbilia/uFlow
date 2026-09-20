#pragma once
#include <algorithm>

struct CoverTransitionPose { float travel, yaw, pitch, roll; };
struct CoverInspectPose { float travel, yaw, pitch, roll; };

inline float CoverTransitionDuration(int speed) {
    return speed == 0 ? .60f : speed == 1 ? .45f : .32f;
}

inline CoverTransitionPose CoverTransition(float progress) {
    const float t = std::clamp(progress, 0.f, 1.f);
    const float eased = t*t*(3.f-2.f*t);
    constexpr float pi = 3.14159265358979323846f;
    // One complete turn, ending front-facing with the spine visible. Positive
    // roll leans the top left / bottom right, matching the Aurora reference.
    return {eased, .055f + (2.f*pi + .48f - .055f)*eased, -.075f*eased, .17f*eased};
}

inline CoverInspectPose CoverInspect(float zoomProgress, float backProgress) {
    const float zoom = std::clamp(zoomProgress, 0.f, 1.f);
    const float back = std::clamp(backProgress, 0.f, 1.f);
    const float zoomEased = zoom*zoom*(3.f-2.f*zoom);
    const float backEased = back*back*(3.f-2.f*back);
    constexpr float pi = 3.14159265358979323846f;
    return {zoomEased, .055f + (pi - .11f) * backEased, -.025f * zoomEased, 0.f};
}

inline float AdvanceCoverTransition(float value, bool opening, float seconds, float duration) {
    const float step = std::max(0.f, seconds) / std::max(.001f, duration);
    return std::clamp(value + (opening ? step : -step), 0.f, 1.f);
}
