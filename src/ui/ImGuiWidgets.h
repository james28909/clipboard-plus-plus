#pragma once

#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace ImGuiWidgets {

inline float& PendingSmoothWheel() {
    static float value = 0.0f;
    return value;
}

inline void AddSmoothWheelDelta(float delta) {
    PendingSmoothWheel() += delta;
}

inline void SmoothScrollCurrentWindow(const char* id, float pixelsPerWheel = 88.0f,
                                      float smoothing = 0.32f,
                                      bool includeChildWindows = true,
                                      bool suspend = false) {
    const ImGuiID scrollId = ImGui::GetID(id);
    struct State { float target = 0.0f; bool initialized = false; };
    static std::unordered_map<ImGuiID, State> states;
    State& state = states[scrollId];

    const float maxScroll = ImGui::GetScrollMaxY();
    const float current = ImGui::GetScrollY();
    if (!state.initialized) {
        state.target = current;
        state.initialized = true;
    }
    if (suspend) {
        // A nested scroll owner is active. Cancel any residual parent
        // animation so the parent remains completely stationary.
        state.target = current;
        return;
    }
    const float resetDistance = std::max(900.0f, ImGui::GetWindowHeight() * 2.5f);
    if (std::fabs(current - state.target) > resetDistance)
        state.target = current;
    state.target = std::clamp(state.target, 0.0f, maxScroll);

    ImGuiHoveredFlags hoverFlags = ImGuiHoveredFlags_AllowWhenBlockedByActiveItem;
    if (includeChildWindows)
        hoverFlags |= ImGuiHoveredFlags_ChildWindows;
    if (ImGui::IsWindowHovered(hoverFlags)) {
        ImGuiIO& io = ImGui::GetIO();
        float wheel = PendingSmoothWheel();
        if (wheel == 0.0f)
            wheel = io.MouseWheel;
        if (wheel != 0.0f) {
            state.target = std::clamp(state.target - wheel * pixelsPerWheel, 0.0f, maxScroll);
            PendingSmoothWheel() = 0.0f;
            io.MouseWheel = 0.0f;
        }
    }

    const float delta = state.target - current;
    if (std::fabs(delta) > 0.25f) {
        const float dt = std::clamp(ImGui::GetIO().DeltaTime, 1.0f / 240.0f, 1.0f / 30.0f);
        const float response = 7.5f + std::clamp(smoothing, 0.05f, 1.0f) * 32.0f;
        const float step = 1.0f - std::exp(-response * dt);
        ImGui::SetScrollY(current + delta * step);
    }
    else if (std::fabs(delta) > 0.0f)
        ImGui::SetScrollY(state.target);
}

inline void KeepMouseWheelOnLastItem() {
    ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
}

inline bool SliderFloatWheel(const char* label, float* value, float min, float max,
                             const char* format, float wheelStep,
                             ImGuiSliderFlags flags = ImGuiSliderFlags_AlwaysClamp) {
    bool changed = ImGui::SliderFloat(label, value, min, max, format, flags);
    KeepMouseWheelOnLastItem();
    if (ImGui::IsItemHovered()) {
        ImGuiIO& io = ImGui::GetIO();
        if (io.MouseWheel != 0.0f) {
            *value = std::clamp(*value + io.MouseWheel * wheelStep, min, max);
            io.MouseWheel = 0.0f;
            changed = true;
        }
    }
    return changed;
}

inline bool SliderIntWheel(const char* label, int* value, int min, int max,
                           const char* format, int wheelStep,
                           ImGuiSliderFlags flags = ImGuiSliderFlags_AlwaysClamp) {
    bool changed = ImGui::SliderInt(label, value, min, max, format, flags);
    KeepMouseWheelOnLastItem();
    if (ImGui::IsItemHovered()) {
        ImGuiIO& io = ImGui::GetIO();
        if (io.MouseWheel != 0.0f) {
            const int delta = io.MouseWheel > 0.0f ? wheelStep : -wheelStep;
            *value = std::clamp(*value + delta, min, max);
            io.MouseWheel = 0.0f;
            changed = true;
        }
    }
    return changed;
}

} // namespace ImGuiWidgets
