// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 Kernel Forge LLC

#include <cstdio>
#include <filesystem>
#include <string>

#include <GLFW/glfw3.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include "app_config.h"
#include "log.h"
#include "manifest.h"
#include "release_store.h"
#include "ui/panels.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

static AppConfig g_app_config;
static std::string g_imgui_ini_path;

static std::string default_public_host() {
	char buffer[256] = {0};
#if defined(_WIN32)
	DWORD size = static_cast<DWORD>(sizeof(buffer));
	if (GetComputerNameA(buffer, &size) && size > 0)
		return std::string(buffer);
#else
	if (gethostname(buffer, sizeof(buffer)) == 0 && buffer[0] != '\0')
		return std::string(buffer);
#endif
	return "localhost";
}

static void glfw_error_callback(int error, const char *description) {
	std::fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

int main(int, char **) {
	glfwSetErrorCallback(glfw_error_callback);
	if (!glfwInit())
		return 1;

	const char *glsl_version = "#version 150";
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#if __APPLE__
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

	GLFWwindow *window = glfwCreateWindow(1280, 720,
					      "KernelForge OTA Workbench",
					      nullptr, nullptr);
	if (window == nullptr) {
		glfwTerminate();
		return 1;
	}

	glfwMakeContextCurrent(window);
	glfwSwapInterval(1);

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO();
	const auto imgui_ini_dir = get_config_directory();
	g_imgui_ini_path = (imgui_ini_dir / "imgui.ini").string();
	io.IniFilename = g_imgui_ini_path.c_str();
	{
		std::error_code ec;
		std::filesystem::create_directories(imgui_ini_dir, ec);
	}
	ImGui::StyleColorsDark();

	ImGui_ImplGlfw_InitForOpenGL(window, true);
	ImGui_ImplOpenGL3_Init(glsl_version);

	ManifestState manifest{};
	load_app_config(g_app_config);
	reset_manifest(manifest, &g_app_config);

	HttpServerState http_server{};
	sync_server_doc_root(manifest, http_server);
	const std::string host = default_public_host();
	std::snprintf(http_server.public_host,
		      IM_ARRAYSIZE(http_server.public_host), "%s",
		      host.c_str());

	int active_tab_index = g_app_config.last_tab_index;
	if (active_tab_index < 0 || active_tab_index > 2)
		active_tab_index = 0;
	bool show_demo_window = false;
	bool config_open = false;

	while (!glfwWindowShouldClose(window)) {
		glfwPollEvents();

		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		if (ImGui::BeginMainMenuBar()) {
			if (ImGui::BeginMenu("File")) {
				if (ImGui::MenuItem("New manifest")) {
					reset_manifest(manifest, &g_app_config);
					sync_server_doc_root(manifest,
							     http_server);
				}

				if (ImGui::MenuItem("Configuration..."))
					config_open = true;

				ImGui::Separator();
				if (ImGui::MenuItem("Quit"))
					glfwSetWindowShouldClose(window,
								 GLFW_TRUE);

				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("View")) {
				ImGui::MenuItem("ImGui demo", nullptr,
						&show_demo_window);
				ImGui::EndMenu();
			}

			ImGui::EndMainMenuBar();
		}

		if (config_open)
			ImGui::OpenPopup("Configuration");
		ui::draw_configuration_modal(manifest, g_app_config,
					     &config_open);

		ImGuiViewport *viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(viewport->WorkPos);
		ImGui::SetNextWindowSize(viewport->WorkSize);
		ImGui::SetNextWindowViewport(viewport->ID);
		ImGuiWindowFlags main_flags =
		    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
		    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar |
		    ImGuiWindowFlags_NoBringToFrontOnFocus |
		    ImGuiWindowFlags_NoNavFocus;
		if (ImGui::Begin("MainTabs", nullptr, main_flags)) {
			if (ImGui::BeginTabBar("WorkbenchTabs")) {
				ImGui::SetNextItemOpen(active_tab_index == 0,
						       ImGuiCond_Once);
				if (ImGui::BeginTabItem("Current Status")) {
					if (active_tab_index != 0) {
						active_tab_index = 0;
						g_app_config.last_tab_index =
						    active_tab_index;
						save_app_config(g_app_config);
					}
					ui::draw_current_status_panel(
					    manifest, http_server,
					    g_app_config);
					ImGui::EndTabItem();
				}
				ImGui::SetNextItemOpen(active_tab_index == 1,
						       ImGuiCond_Once);
				if (ImGui::BeginTabItem("HTTPS Server")) {
					if (active_tab_index != 1) {
						active_tab_index = 1;
						g_app_config.last_tab_index =
						    active_tab_index;
						save_app_config(g_app_config);
					}
					ui::draw_http_server_panel(manifest,
								   http_server);
					ImGui::EndTabItem();
				}
				ImGui::SetNextItemOpen(active_tab_index == 2,
						       ImGuiCond_Once);
				if (ImGui::BeginTabItem("Log")) {
					if (active_tab_index != 2) {
						active_tab_index = 2;
						g_app_config.last_tab_index =
						    active_tab_index;
						save_app_config(g_app_config);
					}
					ui::draw_log_panel();
					ImGui::EndTabItem();
				}
				ImGui::EndTabBar();
			}
		}
		ImGui::End();

		if (show_demo_window)
			ImGui::ShowDemoWindow(&show_demo_window);

		ImGui::Render();

		int display_w, display_h;
		glfwGetFramebufferSize(window, &display_w, &display_h);
		glViewport(0, 0, display_w, display_h);
		glClearColor(0.10f, 0.10f, 0.12f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);

		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		glfwSwapBuffers(window);
	}

	stop_civetweb(http_server);

	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();

	glfwDestroyWindow(window);
	glfwTerminate();
	return 0;
}
