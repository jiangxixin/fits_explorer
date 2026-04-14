/*
 * FITS Explorer - Main Entry Point
 *
 * A lightweight FITS file browser with SQLite indexing and ImGui UI.
 *
 * Build:
 *   mkdir build && cd build
 *   cmake .. -DCMAKE_BUILD_TYPE=Release
 *   make -j$(nproc)
 *
 * Usage:
 *   fits_explorer [database_path]
 *     database_path: path to SQLite DB (default: ~/.fits_explorer.db)
 */

#include "app.h"
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <ImGuiFileDialog.h>
#include <GLFW/glfw3.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <csignal>

static void glfw_error_callback(int error, const char* description) {
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

static void signal_handler(int sig) {
    (void)sig;
    // Graceful shutdown signal
}

// Forward declare app pointer for ImGuiFileDialog callback
static App* g_app = nullptr;

int main(int argc, char** argv) {
    // Determine database path
    std::string db_path;
    const char* home = getenv("HOME");
    if (home) {
        db_path = std::string(home) + "/.fits_explorer.db";
    } else {
        db_path = "/tmp/.fits_explorer.db";
    }
    if (argc > 1) db_path = argv[1];

    // Setup signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // ---- GLFW + OpenGL setup ----
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit())
        return 1;

    // OpenGL 3.0+ is required for ImGui
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    GLFWwindow* window = glfwCreateWindow(1280, 800,
                                           "FITS Explorer",
                                           nullptr, nullptr);
    if (!window) {
        fprintf(stderr, "Failed to create GLFW window.\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);  // VSync

    // ---- App init ----
    App app;
    if (!app.init(db_path)) {
        fprintf(stderr, "Failed to initialize application.\n");
        glfwTerminate();
        return 1;
    }
    g_app = &app;

    // ---- ImGui setup ----
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    // Set app theme
    ImGui::GetStyle().WindowMenuButtonPosition = ImGuiDir_None;

    // ---- Main loop ----
    while (!glfwWindowShouldClose(window) && !app.wants_to_close()) {
        glfwPollEvents();

        // Start ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // ---- Handle File Dialog ----
        static bool folder_dialog_open = false;
        if (ImGuiFileDialog::Instance()->IsOpened("OpenFolderDlgKey")) {
            folder_dialog_open = true;
        }
        if (folder_dialog_open &&
            ImGuiFileDialog::Instance()->Display("OpenFolderDlgKey",
                                                  ImGuiWindowFlags_NoDocking,
                                                  ImVec2(600, 400))) {
            folder_dialog_open = false;
            if (ImGuiFileDialog::Instance()->IsOk()) {
                std::string folder = ImGuiFileDialog::Instance()->GetCurrentPath();
                app.open_folder(folder);
            }
            ImGuiFileDialog::Instance()->Close();
        }

        // ---- Render app UI ----
        app.render();

        // Render ImGui
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.95f, 0.95f, 0.95f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    // ---- Cleanup ----
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwTerminate();

    return 0;
}
