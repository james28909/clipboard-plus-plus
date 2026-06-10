#include "../app/ConfigStore.h"
#include "../clipboard/ClipboardHistory.h"
#include "../clipboard/ClipboardHistoryStore.h"
#include "../ui/Appearance.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <GLFW/glfw3.h>

#include <cstdio>

namespace {

void GlfwErrorCallback(int error, const char* description) {
    std::fprintf(stderr, "GLFW error %d: %s\n", error, description);
}

} // namespace

int main() {
    glfwSetErrorCallback(GlfwErrorCallback);
    if (!glfwInit())
        return 1;

    const char* glslVersion = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    GLFWwindow* window = glfwCreateWindow(720, 420, "Clipboard++ Linux ImGui Probe", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    AppConfig config = ConfigStore::Load();
    ClipboardHistory history;
    ClipboardHistoryStore::Load(config.activeClipboardId, history);

    ApplyThemeStyle(config.appearance, false);
    RebuildFontAtlas(io, config.appearance);

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glslVersion);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
        ImGui::Begin("Clipboard++ Linux Probe", nullptr,
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoCollapse);

        ImGui::Text("Clipboard++ Linux ImGui proof-of-concept");
        ImGui::Separator();
        ImGui::Text("Config: %s", ConfigStore::Path().string().c_str());
        ImGui::Text("History: %s", ClipboardHistoryStore::PathForProfile(config.activeClipboardId).string().c_str());
        ImGui::Text("Active clipboard: %s", config.activeClipboardId.c_str());
        ImGui::Text("History items: %zu", history.Size());
        ImGui::Spacing();
        ImGui::TextWrapped("This target proves Dear ImGui can compile and render on Linux with GLFW/OpenGL. Clipboard monitoring, global hotkeys, tray, and paste behavior are still separate Linux backend work.");

        if (ImGui::Button("Close"))
            glfwSetWindowShouldClose(window, GLFW_TRUE);

        ImGui::End();

        ImGui::Render();
        int displayW = 0;
        int displayH = 0;
        glfwGetFramebufferSize(window, &displayW, &displayH);
        glViewport(0, 0, displayW, displayH);
        glClearColor(config.appearance.windowBg.x, config.appearance.windowBg.y, config.appearance.windowBg.z, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
