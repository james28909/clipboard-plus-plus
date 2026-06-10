#pragma once

#include <imgui.h>
#include <algorithm>

namespace ImGuiWidgets {

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
