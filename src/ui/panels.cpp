// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 Kernel Forge LLC

#include "ui/panels.h"

#include "ImGuiFileDialog.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <future>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#if defined(_WIN32)
#include <shellapi.h>
#include <windows.h>
#else
#include <cstdlib>
#endif

#include "log.h"

namespace {
bool contains_space(const char *value) {
	if (!value)
		return false;
	for (const char *p = value; *p; ++p) {
		if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
			return true;
	}
	return false;
}

bool open_folder_external(const std::filesystem::path &path) {
#if defined(_WIN32)
	std::wstring wpath = path.wstring();
	HINSTANCE result = ShellExecuteW(nullptr, L"open", wpath.c_str(),
					 nullptr, nullptr, SW_SHOWNORMAL);
	return reinterpret_cast<intptr_t>(result) > 32;
#else
	std::string command = "xdg-open \"" + path.string() +
			      "\" > /dev/null 2>&1 &";
	return std::system(command.c_str()) == 0;
#endif
}

void handle_open_folder(const std::filesystem::path &path) {
	if (open_folder_external(path)) {
		log_line("Opened folder: " + path.string());
	} else {
		ImGui::SetClipboardText(path.string().c_str());
		log_line("Folder path copied to clipboard: " + path.string());
	}
}

struct DeviceSummary {
	std::string device_id;
	size_t file_count = 0;
	uint64_t total_size = 0;
};

struct ReleaseSummary {
	TaxonomyPath taxonomy;
	std::filesystem::path path;
	std::string release_name;
	std::string release_version;
	std::string created;
	std::vector<DeviceSummary> devices;
	size_t file_count = 0;
	uint64_t total_size = 0;
};

struct TaxonomyOptions {
	std::vector<std::string> l1;
	std::unordered_map<std::string, std::vector<std::string>> l2;
	std::unordered_map<std::string, std::vector<std::string>> l3;
};

std::string to_lower_ascii(const std::string &value) {
	std::string out;
	out.reserve(value.size());
	for (unsigned char ch : value)
		out.push_back(static_cast<char>(std::tolower(ch)));
	return out;
}

bool contains_case_insensitive(const std::string &haystack,
			       const std::string &needle) {
	if (needle.empty())
		return true;
	return to_lower_ascii(haystack).find(to_lower_ascii(needle)) !=
	       std::string::npos;
}

bool contains_value(const std::vector<std::string> &values,
		    const std::string &value) {
	return std::find(values.begin(), values.end(), value) != values.end();
}

void sort_unique(std::vector<std::string> &values) {
	std::sort(values.begin(), values.end());
	values.erase(std::unique(values.begin(), values.end()), values.end());
}

std::string taxonomy_key(const std::string &l1, const std::string &l2) {
	return l1 + "/" + l2;
}

TaxonomyOptions scan_taxonomy_options(const std::filesystem::path &serve_root) {
	namespace fs = std::filesystem;
	TaxonomyOptions options;
	std::error_code ec;
	if (!fs::exists(serve_root, ec) || !fs::is_directory(serve_root, ec))
		return options;

	for (fs::directory_iterator l1_it(serve_root, ec);
	     l1_it != fs::directory_iterator(); l1_it.increment(ec)) {
		if (ec)
			break;
		const fs::directory_entry &l1_entry = *l1_it;
		if (!l1_entry.is_directory(ec)) {
			ec.clear();
			continue;
		}
		const std::string l1_name = l1_entry.path().filename().string();
		options.l1.push_back(l1_name);

		std::error_code l2_ec;
		for (fs::directory_iterator l2_it(l1_entry.path(), l2_ec);
		     l2_it != fs::directory_iterator();
		     l2_it.increment(l2_ec)) {
			if (l2_ec)
				break;
			const fs::directory_entry &l2_entry = *l2_it;
			if (!l2_entry.is_directory(l2_ec)) {
				l2_ec.clear();
				continue;
			}
			const std::string l2_name =
			    l2_entry.path().filename().string();
			options.l2[l1_name].push_back(l2_name);

			std::error_code l3_ec;
			for (fs::directory_iterator l3_it(l2_entry.path(),
							  l3_ec);
			     l3_it != fs::directory_iterator();
			     l3_it.increment(l3_ec)) {
				if (l3_ec)
					break;
				const fs::directory_entry &l3_entry = *l3_it;
				if (!l3_entry.is_directory(l3_ec)) {
					l3_ec.clear();
					continue;
				}
				const std::string l3_name =
				    l3_entry.path().filename().string();
				options.l3[taxonomy_key(l1_name, l2_name)]
				    .push_back(l3_name);
			}
		}
	}

	sort_unique(options.l1);
	for (auto &entry : options.l2)
		sort_unique(entry.second);
	for (auto &entry : options.l3)
		sort_unique(entry.second);

	return options;
}

std::vector<std::string> collect_device_ids(const Manifest &manifest) {
	std::vector<std::string> ids;
	std::unordered_set<std::string> seen;
	for (const auto &rel : manifest.releases) {
		if (rel.device_id.empty())
			continue;
		if (seen.insert(rel.device_id).second)
			ids.push_back(rel.device_id);
	}
	sort_unique(ids);
	return ids;
}

void ensure_default_device_id(std::vector<std::string> &ids) {
	auto it = std::find(ids.begin(), ids.end(), "default");
	if (it == ids.end()) {
		ids.insert(ids.begin(), "default");
	} else if (it != ids.begin()) {
		std::string value = *it;
		ids.erase(it);
		ids.insert(ids.begin(), value);
	}
}

std::string release_display_name(const ReleaseSummary &summary) {
	if (summary.release_name.empty() && summary.release_version.empty())
		return "<unknown>";
	if (summary.release_name.empty())
		return summary.release_version;
	if (summary.release_version.empty())
		return summary.release_name;
	return summary.release_name + " (" + summary.release_version + ")";
}

std::string taxonomy_display_name(const ReleaseSummary &summary) {
	return summary.taxonomy.l1 + "/" + summary.taxonomy.l2 + "/" +
	       summary.taxonomy.l3;
}

void load_manifest_header(const std::filesystem::path &manifest_path,
			  ReleaseSummary &summary) {
	std::string json;
	if (!read_file_to_string(manifest_path, &json))
		return;

	Manifest manifest;
	std::string err;
	if (parse_manifest_json(json, &manifest, &err)) {
		if (!manifest.created.empty())
			summary.created = manifest.created;
		const TargetRelease *target = nullptr;
		for (const auto &release : manifest.releases) {
			if (release.device_id == "default") {
				target = &release;
				break;
			}
		}
		if (!target && !manifest.releases.empty())
			target = &manifest.releases.front();
		if (target) {
			summary.release_name = target->release_name;
			summary.release_version = target->release_version;
			if (summary.created.empty())
				summary.created = target->created;
		}
		return;
	}

	return;
}

void populate_device_summaries(const std::filesystem::path &release_dir,
			       ReleaseSummary &summary) {
	namespace fs = std::filesystem;
	std::error_code ec;
	for (fs::directory_iterator it(release_dir, ec);
	     it != fs::directory_iterator(); it.increment(ec)) {
		if (ec)
			break;
		const fs::directory_entry &entry = *it;
		if (!entry.is_directory(ec)) {
			ec.clear();
			continue;
		}

		DeviceSummary device;
		device.device_id = entry.path().filename().string();
		std::error_code file_ec;
		for (fs::directory_iterator file_it(entry.path(), file_ec);
		     file_it != fs::directory_iterator();
		     file_it.increment(file_ec)) {
			if (file_ec)
				break;
			const fs::directory_entry &file_entry = *file_it;
			if (!file_entry.is_regular_file(file_ec)) {
				file_ec.clear();
				continue;
			}
			const auto size = file_entry.file_size(file_ec);
			if (file_ec) {
				file_ec.clear();
				continue;
			}
			device.file_count += 1;
			device.total_size += static_cast<uint64_t>(size);
		}

		summary.file_count += device.file_count;
		summary.total_size += device.total_size;
		summary.devices.push_back(std::move(device));
	}

	std::sort(summary.devices.begin(), summary.devices.end(),
		  [](const DeviceSummary &a, const DeviceSummary &b) {
			  return a.device_id < b.device_id;
		  });
}

std::vector<ReleaseSummary> scan_release_summaries(const ManifestState &st) {
	namespace fs = std::filesystem;
	std::vector<ReleaseSummary> summaries;

	std::error_code ec;
	const fs::path root = get_serve_root_path(st.package_output_dir);
	if (!fs::exists(root, ec) || !fs::is_directory(root, ec))
		return summaries;

	for (fs::directory_iterator l1_it(root, ec);
	     l1_it != fs::directory_iterator(); l1_it.increment(ec)) {
		if (ec)
			break;
		const fs::directory_entry &l1_entry = *l1_it;
		if (!l1_entry.is_directory(ec)) {
			ec.clear();
			continue;
		}
		const std::string l1_name = l1_entry.path().filename().string();

		std::error_code l2_ec;
		for (fs::directory_iterator l2_it(l1_entry.path(), l2_ec);
		     l2_it != fs::directory_iterator();
		     l2_it.increment(l2_ec)) {
			if (l2_ec)
				break;
			const fs::directory_entry &l2_entry = *l2_it;
			if (!l2_entry.is_directory(l2_ec)) {
				l2_ec.clear();
				continue;
			}
			const std::string l2_name =
			    l2_entry.path().filename().string();

			std::error_code l3_ec;
			for (fs::directory_iterator l3_it(l2_entry.path(),
							  l3_ec);
			     l3_it != fs::directory_iterator();
			     l3_it.increment(l3_ec)) {
				if (l3_ec)
					break;
				const fs::directory_entry &l3_entry = *l3_it;
				if (!l3_entry.is_directory(l3_ec)) {
					l3_ec.clear();
					continue;
				}
				const std::string l3_name =
				    l3_entry.path().filename().string();
				const fs::path release_dir = l3_entry.path();
				const fs::path manifest_path = release_dir /
							       "manifest.json";
				if (!fs::exists(manifest_path, l3_ec)) {
					l3_ec.clear();
					continue;
				}

				ReleaseSummary summary;
				summary.taxonomy.l1 = l1_name;
				summary.taxonomy.l2 = l2_name;
				summary.taxonomy.l3 = l3_name;
				summary.path = release_dir;

				load_manifest_header(manifest_path, summary);
				populate_device_summaries(release_dir, summary);

				if (summary.release_name.empty())
					summary.release_name = "<unknown>";
				if (summary.release_version.empty())
					summary.release_version = "<unknown>";
				if (summary.created.empty())
					summary.created = "<unknown>";

				summaries.push_back(std::move(summary));
			}
		}
	}

	std::sort(summaries.begin(), summaries.end(),
		  [](const ReleaseSummary &a, const ReleaseSummary &b) {
			  return taxonomy_display_name(a) <
				 taxonomy_display_name(b);
		  });

	return summaries;
}

} // namespace

namespace ui {
namespace {
void draw_release_wizard_modal(ManifestState &st, HttpServerState &srv,
			       AppConfig &config, bool *open) {
	static int wizard_step = 0;
	struct PublishResult {
		bool success = false;
		std::string release_path;
		std::string manifest_path;
		std::string err;
		ManifestState state;
	};
	static bool wizard_publish_success = false;
	static bool wizard_publish_in_progress = false;
	static bool wizard_close_requested = false;
	static std::string wizard_error_message;
	static std::string wizard_release_path;
	static std::string wizard_manifest_path;
	static float manifest_preview_height = 0.0f;
	static float manifest_preview_reserved = 180.0f;
	static bool manifest_preview_user_resized = false;
	static int last_wizard_step = -1;
	static std::future<PublishResult> wizard_publish_future;
	static std::vector<std::string> wizard_device_ids;
	static std::string wizard_selected_device_id;
	static std::string wizard_new_device_id;
	static std::string wizard_new_device_error;
	static std::string wizard_new_device_warning;
	static std::string wizard_device_taxonomy_key;
	static bool wizard_new_device_focus = false;
	static std::string file_dialog_device_id;
	float preview_after_y = -1.0f;

	if (open && *open && !ImGui::IsPopupOpen("Release Wizard")) {
		ImGui::OpenPopup("Release Wizard");
		if (!wizard_publish_in_progress) {
			wizard_step = 0;
			wizard_publish_success = false;
			wizard_close_requested = false;
			wizard_error_message.clear();
			wizard_release_path.clear();
			wizard_manifest_path.clear();
			for (auto &rel : st.releases)
				rel.files.clear();
			wizard_device_ids.clear();
			wizard_selected_device_id.clear();
			wizard_new_device_id.clear();
			wizard_new_device_error.clear();
			wizard_new_device_warning.clear();
			wizard_device_taxonomy_key.clear();
			wizard_new_device_focus = false;
			file_dialog_device_id.clear();
		}
	}

	if (wizard_publish_in_progress && wizard_publish_future.valid()) {
		const auto status = wizard_publish_future.wait_for(
		    std::chrono::milliseconds(0));
		if (status == std::future_status::ready) {
			PublishResult result = wizard_publish_future.get();
			wizard_publish_in_progress = false;
			if (result.success) {
				st = result.state;
				wizard_publish_success = true;
				wizard_error_message.clear();
				wizard_release_path = result.release_path;
				wizard_manifest_path = result.manifest_path;
				log_line("Release published: " +
					 result.release_path);
				persist_configuration(st, config);
				wizard_close_requested = true;
			} else {
				wizard_publish_success = false;
				wizard_error_message = result.err;
				wizard_step = 2;
				log_line(result.err.empty()
					     ? "Failed to publish release."
					     : result.err);
			}
		}
	}

	if (!open || !*open) {
		ImGuiFileDialog::Instance()->Close();
		file_dialog_device_id.clear();
		return;
	}

	ImGui::SetNextWindowSize(ImVec2(960.0f, 720.0f), ImGuiCond_Once);
	if (!ImGui::BeginPopupModal("Release Wizard", open))
		return;

	if (wizard_close_requested) {
		if (open)
			*open = false;
		ImGui::CloseCurrentPopup();
		ImGuiFileDialog::Instance()->Close();
		file_dialog_device_id.clear();
		wizard_close_requested = false;
		ImGui::EndPopup();
		return;
	}

	auto label_or_default = [](const std::string &label,
				   const char *fallback) -> std::string {
		return label.empty() ? std::string(fallback) : label;
	};

	const std::string l1_label =
	    label_or_default(config.taxonomy_labels.l1_label, "Fleet Group");
	const std::string l2_label =
	    label_or_default(config.taxonomy_labels.l2_label, "Sub Group");
	const std::string l3_label =
	    label_or_default(config.taxonomy_labels.l3_label, "Function");
	const std::string device_label = label_or_default(
	    config.taxonomy_labels.device_selector_label, "Device ID");

	ImGui::TextUnformatted("Publish Release");
	ImGui::TextWrapped("Create OTA manifests and payload directories using "
			   "the configured taxonomy.");
	ImGui::Separator();

	const auto serve_root_path = get_serve_root_path(st.package_output_dir);
	ImGui::Text("Serve root: %s", serve_root_path.string().c_str());
	ImGui::SameLine();
	if (ImGui::Button("Open Serve Root"))
		handle_open_folder(serve_root_path);

	const TaxonomyOptions taxonomy_options = scan_taxonomy_options(
	    serve_root_path);

	std::string base_url = "<set taxonomy to compute base URL>";
	const bool taxonomy_ready =
	    is_valid_taxonomy_component(st.taxonomy.l1, nullptr) &&
	    is_valid_taxonomy_component(st.taxonomy.l2, nullptr) &&
	    is_valid_taxonomy_component(st.taxonomy.l3, nullptr);
	if (taxonomy_ready)
		base_url = build_device_base_url(srv, st.taxonomy);

	bool existing_manifest_has_default = false;
	bool existing_manifest_parse_failed = false;
	std::string existing_manifest_error;
	bool existing_manifest_loaded = false;
	Manifest existing_manifest;
	if (taxonomy_ready) {
		const auto manifest_path = serve_root_path / st.taxonomy.l1 /
					   st.taxonomy.l2 / st.taxonomy.l3 /
					   "manifest.json";
		std::error_code manifest_ec;
		if (std::filesystem::exists(manifest_path, manifest_ec) &&
		    !manifest_ec) {
			std::string err;
			if (load_manifest_file(manifest_path,
					       &existing_manifest, &err)) {
				existing_manifest_has_default =
				    manifest_has_device(existing_manifest,
							"default");
				existing_manifest_loaded = true;
			} else {
				existing_manifest_parse_failed = true;
				existing_manifest_error =
				    err.empty()
					? "Failed to parse existing manifest."
					: err;
			}
		}
	}
	std::string current_taxonomy_key;
	if (taxonomy_ready)
		current_taxonomy_key = taxonomy_key(st.taxonomy.l1,
						    st.taxonomy.l2) +
				       "/" + st.taxonomy.l3;
	if (taxonomy_ready &&
	    current_taxonomy_key != wizard_device_taxonomy_key) {
		wizard_device_taxonomy_key = current_taxonomy_key;
		wizard_device_ids.clear();
		if (existing_manifest_loaded)
			wizard_device_ids = collect_device_ids(
			    existing_manifest);
		ensure_default_device_id(wizard_device_ids);
		wizard_selected_device_id =
		    contains_value(wizard_device_ids, "default")
			? "default"
			: (wizard_device_ids.empty()
			       ? ""
			       : wizard_device_ids.front());
		st.releases.clear();
	}
	if (wizard_device_ids.empty() && taxonomy_ready) {
		wizard_device_ids.push_back("default");
		wizard_selected_device_id = "default";
	}
	if (!wizard_selected_device_id.empty() &&
	    !contains_value(wizard_device_ids, wizard_selected_device_id)) {
		wizard_selected_device_id = wizard_device_ids.empty()
						? ""
						: wizard_device_ids.front();
	}

	ImGui::TextUnformatted("Device Base URL");
	ImGui::SetNextItemWidth(-90.0f);
	ImGui::InputText("##DeviceBaseUrl", &base_url,
			 ImGuiInputTextFlags_ReadOnly);
	ImGui::SameLine();
	ImGui::BeginDisabled(!taxonomy_ready);
	if (ImGui::Button("Copy##DeviceBaseUrl")) {
		ImGui::SetClipboardText(base_url.c_str());
		log_line("Device base URL copied: " + base_url);
	}
	ImGui::EndDisabled();

	ImGui::Separator();

	static const char *k_step_titles[] = {"Details", "Devices & Files",
					      "Review & Publish"};
	ImGui::Text("Step %d/3 - %s", wizard_step + 1,
		    k_step_titles[wizard_step]);
	ImGui::ProgressBar((wizard_step + 1) / 3.0f, ImVec2(-1.0f, 0.0f));
	ImGui::Separator();

	if (wizard_step != last_wizard_step) {
		if (wizard_step == 2) {
			manifest_preview_height = 0.0f;
			manifest_preview_user_resized = false;
		}
		last_wizard_step = wizard_step;
	}

	if (wizard_step == 0) {
		static const std::vector<std::string> k_empty_options;
		bool config_dirty = false;
		auto draw_taxonomy_combo = [&](const std::string &label,
					       const std::vector<std::string>
						   &options,
					       std::string &value,
					       const char *id_suffix,
					       bool enabled) {
			const std::string field_label = label + "##" +
							id_suffix;
			const char *preview = value.empty() ? "<select>"
							    : value.c_str();
			bool changed = false;
			ImGui::BeginDisabled(!enabled);
			if (ImGui::BeginCombo(field_label.c_str(), preview)) {
				if (options.empty()) {
					ImGui::TextDisabled("No groups found.");
				} else {
					for (const auto &option : options) {
						const bool selected = (option ==
								       value);
						if (ImGui::Selectable(
							option.c_str(),
							selected)) {
							value = option;
							changed = true;
						}
						if (selected)
							ImGui::
							    SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}
			ImGui::EndDisabled();
			return changed;
		};

		const bool l1_changed = draw_taxonomy_combo(l1_label,
							    taxonomy_options.l1,
							    st.taxonomy.l1,
							    "L1", true);
		if (l1_changed) {
			config.last_l1 = st.taxonomy.l1;
			config_dirty = true;
			const auto l2_it = taxonomy_options.l2.find(
			    st.taxonomy.l1);
			const auto &l2_options = (l2_it !=
						  taxonomy_options.l2.end())
						     ? l2_it->second
						     : k_empty_options;
			if (!contains_value(l2_options, st.taxonomy.l2)) {
				st.taxonomy.l2.clear();
				st.taxonomy.l3.clear();
				if (!config.last_l2.empty()) {
					config.last_l2.clear();
					config_dirty = true;
				}
				if (!config.last_l3.empty()) {
					config.last_l3.clear();
					config_dirty = true;
				}
			}
		}

		const auto l2_it = taxonomy_options.l2.find(st.taxonomy.l1);
		const auto &l2_options = (l2_it != taxonomy_options.l2.end())
					     ? l2_it->second
					     : k_empty_options;
		const bool l2_changed =
		    draw_taxonomy_combo(l2_label, l2_options, st.taxonomy.l2,
					"L2", !st.taxonomy.l1.empty());
		if (l2_changed) {
			config.last_l2 = st.taxonomy.l2;
			config_dirty = true;
			const auto l3_it = taxonomy_options.l3.find(
			    taxonomy_key(st.taxonomy.l1, st.taxonomy.l2));
			const auto &l3_options = (l3_it !=
						  taxonomy_options.l3.end())
						     ? l3_it->second
						     : k_empty_options;
			if (!contains_value(l3_options, st.taxonomy.l3)) {
				st.taxonomy.l3.clear();
				if (!config.last_l3.empty()) {
					config.last_l3.clear();
					config_dirty = true;
				}
			}
		}

		const auto l3_it = taxonomy_options.l3.find(
		    taxonomy_key(st.taxonomy.l1, st.taxonomy.l2));
		const auto &l3_options = (l3_it != taxonomy_options.l3.end())
					     ? l3_it->second
					     : k_empty_options;
		const bool l3_changed = draw_taxonomy_combo(
		    l3_label, l3_options, st.taxonomy.l3, "L3",
		    !st.taxonomy.l1.empty() && !st.taxonomy.l2.empty());
		if (l3_changed) {
			config.last_l3 = st.taxonomy.l3;
			config_dirty = true;
		}

		if (config_dirty)
			save_app_config(config);

		if (taxonomy_options.l1.empty()) {
			ImGui::TextColored(
			    ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
			    "No groups found. Use Create Group to add one.");
		}

		if (st.taxonomy.l1.empty()) {
			ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
					   "%s is required.", l1_label.c_str());
		} else if (!is_valid_taxonomy_component(st.taxonomy.l1,
							nullptr)) {
			ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
					   "%s uses letters, numbers, dashes, "
					   "and underscores only.",
					   l1_label.c_str());
		}

		if (st.taxonomy.l2.empty()) {
			ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
					   "%s is required.", l2_label.c_str());
		} else if (!is_valid_taxonomy_component(st.taxonomy.l2,
							nullptr)) {
			ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
					   "%s uses letters, numbers, dashes, "
					   "and underscores only.",
					   l2_label.c_str());
		}

		if (st.taxonomy.l3.empty()) {
			ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
					   "%s is required.", l3_label.c_str());
		} else if (!is_valid_taxonomy_component(st.taxonomy.l3,
							nullptr)) {
			ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
					   "%s uses letters, numbers, dashes, "
					   "and underscores only.",
					   l3_label.c_str());
		}

		ImGui::Separator();
		ImGui::InputText("Release name", &st.release_name);
		if (st.release_name.empty())
			ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
					   "Release name is required.");

		ImGui::InputText("Release version", &st.release_version);
		if (st.release_version.empty())
			ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
					   "Release version is required.");

		static const char *k_file_types[] = {"rauc_bundle",
						     "rauc_bundle_test",
						     "custom"};
		int selected_file_type = 0;
		bool matched_file_type = false;
		for (int i = 0; i < IM_ARRAYSIZE(k_file_types); ++i) {
			if (st.file_type == k_file_types[i]) {
				selected_file_type = i;
				matched_file_type = true;
				break;
			}
		}
		if (!matched_file_type)
			selected_file_type = 2;
		if (ImGui::Combo("File type", &selected_file_type, k_file_types,
				 IM_ARRAYSIZE(k_file_types)))
			st.file_type = k_file_types[selected_file_type];
		if (selected_file_type == 2)
			ImGui::InputText("Custom file type", &st.file_type);

		ImGui::InputText("Created (UTC)", &st.created);
		ImGui::SameLine();
		if (ImGui::Button("Now##Created"))
			st.created = now_utc_iso8601();
	} else if (wizard_step == 1) {
		auto find_release_index =
		    [&](const std::string &device_id) -> int {
			for (size_t i = 0; i < st.releases.size(); ++i) {
				if (st.releases[i].device_id == device_id)
					return static_cast<int>(i);
			}
			return -1;
		};

		ImGui::TextUnformatted(device_label.c_str());
		const float combo_width = 220.0f;
		ImGui::SetNextItemWidth(combo_width);
		const char *preview = wizard_selected_device_id.empty()
					  ? "<select>"
					  : wizard_selected_device_id.c_str();
		if (ImGui::BeginCombo("##DeviceIdSelect", preview)) {
			for (const auto &option : wizard_device_ids) {
				const bool selected =
				    (option == wizard_selected_device_id);
				if (ImGui::Selectable(option.c_str(), selected))
					wizard_selected_device_id = option;
				if (selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		ImGui::SameLine();
		if (ImGui::Button("Add Device")) {
			wizard_new_device_id.clear();
			wizard_new_device_error.clear();
			wizard_new_device_warning.clear();
			wizard_new_device_focus = true;
			ImGui::OpenPopup("Add Device");
		}
		ImGui::SameLine();
		ImGui::TextUnformatted(
		    "Select a device ID to edit payload files.");

		if (ImGui::BeginPopupModal("Add Device", nullptr,
					   ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::TextUnformatted(device_label.c_str());
			if (wizard_new_device_focus) {
				ImGui::SetKeyboardFocusHere();
				wizard_new_device_focus = false;
			}
			if (ImGui::InputText("##NewDeviceId",
					     &wizard_new_device_id)) {
				wizard_new_device_error.clear();
				wizard_new_device_warning.clear();
			}
			if (!wizard_new_device_error.empty()) {
				ImGui::TextColored(
				    ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "%s",
				    wizard_new_device_error.c_str());
			} else if (!wizard_new_device_warning.empty()) {
				ImGui::TextColored(
				    ImVec4(0.9f, 0.7f, 0.2f, 1.0f), "%s",
				    wizard_new_device_warning.c_str());
			}

			ImGui::Separator();
			if (ImGui::Button("OK")) {
				std::string err;
				if (!is_valid_device_id(wizard_new_device_id,
							&err)) {
					wizard_new_device_error = err;
					wizard_new_device_warning.clear();
				} else if (contains_value(
					       wizard_device_ids,
					       wizard_new_device_id)) {
					wizard_selected_device_id =
					    wizard_new_device_id;
					wizard_new_device_warning =
					    "Device ID already exists; "
					    "selecting it.";
				} else {
					wizard_device_ids.push_back(
					    wizard_new_device_id);
					ensure_default_device_id(
					    wizard_device_ids);
					wizard_selected_device_id =
					    wizard_new_device_id;
					ImGui::CloseCurrentPopup();
				}
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel"))
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}

		bool has_default = false;
		for (const auto &rel : st.releases) {
			if (rel.device_id == "default")
				has_default = true;
		}

		if (!has_default && !existing_manifest_has_default)
			ImGui::TextColored(
			    ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
			    "Default %s ('default') is required.",
			    device_label.c_str());
		else if (!has_default && existing_manifest_has_default)
			ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
					   "Default %s will be kept from the "
					   "existing manifest.",
					   device_label.c_str());
		if (existing_manifest_parse_failed)
			ImGui::TextColored(
			    ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
			    "Existing manifest could not be read: %s",
			    existing_manifest_error.c_str());

		const int active_release_index = find_release_index(
		    wizard_selected_device_id);
		DeviceReleaseState *active_release =
		    active_release_index >= 0
			? &st.releases[static_cast<size_t>(
			      active_release_index)]
			: nullptr;

		ImGui::Separator();
		ImGui::BeginDisabled(wizard_selected_device_id.empty());
		if (ImGui::Button("Add payload files")) {
			IGFD::FileDialogConfig config_dialog;
			config_dialog.path = ".";
			config_dialog.countSelectionMax = 0;
			config_dialog.flags =
			    ImGuiFileDialogFlags_NaturalSorting;
			ImGuiFileDialog::Instance()->OpenDialog(
			    "AddBundleDlg", "Add payload files", ".raucb,.*",
			    config_dialog);
			file_dialog_device_id = wizard_selected_device_id;
		}
		ImGui::EndDisabled();

		if (!active_release || active_release->files.empty()) {
			ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
					   "Add at least one payload file.");
		} else if (ImGui::BeginTable("DeviceFiles", 6,
					     ImGuiTableFlags_Borders |
						 ImGuiTableFlags_RowBg |
						 ImGuiTableFlags_ScrollY,
					     ImVec2(0.0f, 220.0f))) {
			ImGui::TableSetupColumn(
			    "#", ImGuiTableColumnFlags_WidthFixed, 32.0f);
			ImGui::TableSetupColumn("Filename");
			ImGui::TableSetupColumn(
			    "Size", ImGuiTableColumnFlags_WidthFixed, 100.0f);
			ImGui::TableSetupColumn("SHA-256");
			ImGui::TableSetupColumn("Local path");
			ImGui::TableSetupColumn(
			    "", ImGuiTableColumnFlags_WidthFixed, 60.0f);
			ImGui::TableHeadersRow();

			bool remove_release = false;
			for (size_t fidx = 0;
			     fidx < active_release->files.size(); ++fidx) {
				auto &f = active_release->files[fidx];
				ImGui::PushID(static_cast<int>(fidx));
				ImGui::TableNextRow();

				ImGui::TableSetColumnIndex(0);
				ImGui::Text("%zu", fidx + 1);

				ImGui::TableSetColumnIndex(1);
				ImGui::InputText("##filename", &f.filename);

				ImGui::TableSetColumnIndex(2);
				ImGui::Text("%llu", (unsigned long long)f.size);

				ImGui::TableSetColumnIndex(3);
				const std::string sha_display =
				    f.sha256.empty() ? "<pending>" : f.sha256;
				ImGui::TextUnformatted(sha_display.c_str());
				if (!sha_display.empty() &&
				    ImGui::IsItemHovered())
					ImGui::SetTooltip("%s",
							  sha_display.c_str());

				ImGui::TableSetColumnIndex(4);
				ImGui::TextUnformatted(f.local_path.c_str());

				ImGui::TableSetColumnIndex(5);
				if (ImGui::SmallButton("Remove")) {
					active_release->files.erase(
					    active_release->files.begin() +
					    static_cast<long>(fidx));
					ImGui::PopID();
					if (active_release->files.empty()) {
						remove_release = true;
						break;
					}
					--fidx;
					continue;
				}

				ImGui::PopID();
			}
			ImGui::EndTable();

			if (remove_release && active_release_index >= 0 &&
			    active_release_index <
				static_cast<int>(st.releases.size())) {
				st.releases.erase(st.releases.begin() +
						  active_release_index);
			}
		}
	} else if (wizard_step == 2) {
		if (wizard_publish_in_progress) {
			ImGui::TextUnformatted("Publishing release...");
			const float phase = static_cast<float>(
			    std::fmod(ImGui::GetTime(), 1.0));
			ImGui::ProgressBar(phase, ImVec2(-1.0f, 0.0f),
					   "Working...");
			ImGui::TextWrapped("Hashing and signing can take a "
					   "while for large bundles.");
		} else {
			size_t file_count = 0;
			for (const auto &rel : st.releases)
				file_count += rel.files.size();

			ImGui::Text("Devices: %zu", st.releases.size());
			ImGui::Text("Files: %zu", file_count);
			ImGui::Text("Total size: %llu bytes",
				    (unsigned long long)manifest_total_size(
					st));
			ImGui::TextWrapped("Device Base URL: %s",
					   taxonomy_ready ? base_url.c_str()
							  : "<set taxonomy>");
			ImGui::Spacing();
			ImGui::TextUnformatted("Signing algorithm");
			ImGui::SetNextItemWidth(280.0f);
			const SignAlg alg_options[] = {
			    SignAlg::Ed25519, SignAlg::EcdsaP256Sha256,
			    SignAlg::RsaPssSha256};
			const char *alg_labels[] = {
			    sign_alg_display_name(alg_options[0]),
			    sign_alg_display_name(alg_options[1]),
			    sign_alg_display_name(alg_options[2])};
			int alg_index = 0;
			for (int i = 0; i < static_cast<int>(
					 IM_ARRAYSIZE(alg_options));
			     ++i) {
				if (st.signing_algorithm == alg_options[i]) {
					alg_index = i;
					break;
				}
			}
			if (ImGui::Combo("##SigningAlgorithm", &alg_index,
					 alg_labels,
					 IM_ARRAYSIZE(alg_labels))) {
				st.signing_algorithm = alg_options[alg_index];
				persist_configuration(st, config);
			}

			const Manifest *preview_manifest =
			    existing_manifest_loaded ? &existing_manifest
						     : nullptr;
			const std::string manifest_json =
			    build_manifest_preview_json(st, preview_manifest);
			ImGui::Separator();
			ImGui::TextUnformatted("Manifest preview");
			const float min_preview_height =
			    ImGui::GetTextLineHeightWithSpacing() * 6.0f;
			const float splitter_height = 6.0f;
			const float spacing = ImGui::GetStyle().ItemSpacing.y;
			const float avail_height =
			    ImGui::GetContentRegionAvail().y;
			const float default_height =
			    std::max(min_preview_height,
				     avail_height - manifest_preview_reserved -
					 splitter_height - spacing * 2.0f);
			if (!manifest_preview_user_resized ||
			    manifest_preview_height <= 0.0f)
				manifest_preview_height = default_height;
			else if (manifest_preview_height < min_preview_height)
				manifest_preview_height = min_preview_height;

			ImGui::BeginChild("ManifestPreview",
					  ImVec2(0.0f, manifest_preview_height),
					  true,
					  ImGuiWindowFlags_HorizontalScrollbar);
			ImGui::TextUnformatted(manifest_json.c_str());
			ImGui::EndChild();
			ImGui::InvisibleButton("ManifestPreviewSplitter",
					       ImVec2(-1.0f, splitter_height));
			if (ImGui::IsItemActive()) {
				manifest_preview_user_resized = true;
				manifest_preview_height =
				    std::max(min_preview_height,
					     manifest_preview_height +
						 ImGui::GetIO().MouseDelta.y);
			}
			if (ImGui::IsItemHovered() || ImGui::IsItemActive())
				ImGui::SetMouseCursor(
				    ImGuiMouseCursor_ResizeNS);
			preview_after_y = ImGui::GetCursorPosY();

			if (wizard_publish_success) {
				ImGui::TextColored(
				    ImVec4(0.3f, 0.9f, 0.3f, 1.0f),
				    "Release created successfully.");
				if (!wizard_release_path.empty())
					ImGui::TextWrapped(
					    "Release directory: %s",
					    wizard_release_path.c_str());
				if (!wizard_manifest_path.empty())
					ImGui::TextWrapped(
					    "Manifest path: %s",
					    wizard_manifest_path.c_str());
			} else if (!wizard_error_message.empty()) {
				ImGui::TextColored(
				    ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
				    "Release failed: %s",
				    wizard_error_message.c_str());
			}

			if (st.signer_key_path.empty() ||
			    st.signer_cert_path.empty()) {
				ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f,
							  1.0f),
						   "Signer key and certificate "
						   "are required to publish.");
			}

			const bool ready_for_publish =
			    manifest_ready_for_release(
				st, existing_manifest_has_default) &&
			    taxonomy_ready && !st.signer_key_path.empty() &&
			    !st.signer_cert_path.empty();
			ImGui::BeginDisabled(!ready_for_publish ||
					     wizard_publish_in_progress);
			if (ImGui::Button("Publish release")) {
				ManifestState snapshot = st;
				wizard_publish_in_progress = true;
				wizard_publish_success = false;
				wizard_error_message.clear();
				wizard_release_path.clear();
				wizard_manifest_path.clear();
				wizard_publish_future = std::async(
				    std::launch::async,
				    [snapshot, srv]() mutable {
					    PublishResult result;
					    result.state = snapshot;
					    std::string release_path;
					    std::string manifest_path;
					    std::string err;
					    if (publish_release_manifest(
						    result.state, srv,
						    &release_path,
						    &manifest_path, &err)) {
						    result.success = true;
						    result.release_path =
							release_path;
						    result.manifest_path =
							manifest_path;
					    } else {
						    result.success = false;
						    result.err = err;
					    }
					    return result;
				    });
			}
			ImGui::EndDisabled();
		}
	}

	ImGui::Separator();
	ImGui::BeginDisabled(wizard_publish_in_progress);
	if (ImGui::Button("Close")) {
		if (open)
			*open = false;
		ImGui::CloseCurrentPopup();
		ImGuiFileDialog::Instance()->Close();
		file_dialog_device_id.clear();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(wizard_publish_in_progress);
	if (wizard_step > 0) {
		if (ImGui::Button("Back"))
			wizard_step--;
		ImGui::SameLine();
	}
	if (wizard_step < 2) {
		const bool next_enabled =
		    (wizard_step == 0) ? manifest_has_basic_info(st)
				       : manifest_ready_for_release(
					     st, existing_manifest_has_default);
		ImGui::BeginDisabled(!next_enabled);
		if (ImGui::Button("Next"))
			wizard_step++;
		ImGui::EndDisabled();
	}
	ImGui::EndDisabled();

	if (wizard_step == 2 && preview_after_y >= 0.0f) {
		float cursor_y = ImGui::GetCursorPosY();
		cursor_y += ImGui::GetFrameHeightWithSpacing();
		manifest_preview_reserved = std::max(0.0f, cursor_y -
							       preview_after_y);
	}

	auto ensure_release_for_device =
	    [&](const std::string &device_id) -> DeviceReleaseState * {
		if (device_id.empty())
			return nullptr;
		for (auto &rel : st.releases) {
			if (rel.device_id == device_id)
				return &rel;
		}
		DeviceReleaseState rel;
		rel.device_id = device_id;
		st.releases.push_back(std::move(rel));
		return &st.releases.back();
	};

	if (ImGuiFileDialog::Instance()->Display("AddBundleDlg")) {
		if (ImGuiFileDialog::Instance()->IsOk()) {
			const std::string target_device =
			    !file_dialog_device_id.empty()
				? file_dialog_device_id
				: wizard_selected_device_id;
			auto selection =
			    ImGuiFileDialog::Instance()->GetSelection();
			if (!selection.empty()) {
				DeviceReleaseState *target_release =
				    ensure_release_for_device(target_device);
				if (target_release) {
					for (const auto &kv : selection)
						add_bundle_file(*target_release,
								kv.second);
				}
			} else {
				const std::string path =
				    ImGuiFileDialog::Instance()
					->GetFilePathName();
				if (!path.empty()) {
					DeviceReleaseState *target_release =
					    ensure_release_for_device(
						target_device);
					if (target_release)
						add_bundle_file(*target_release,
								path);
				}
			}
		}
		ImGuiFileDialog::Instance()->Close();
		file_dialog_device_id.clear();
	}

	ImGui::EndPopup();
}

void draw_add_group_modal(ManifestState &st, AppConfig &config, bool *open) {
	static std::string group_l1;
	static std::string group_l2;
	static std::string group_l3;
	static std::string error_message;

	if (open && *open && !ImGui::IsPopupOpen("Add Group")) {
		ImGui::OpenPopup("Add Group");
		group_l1 = st.taxonomy.l1;
		group_l2 = st.taxonomy.l2;
		group_l3 = st.taxonomy.l3;
		error_message.clear();
	}

	if (!open || !*open)
		return;

	ImGui::SetNextWindowSize(ImVec2(520.0f, 320.0f), ImGuiCond_Once);
	if (!ImGui::BeginPopupModal("Add Group", open,
				    ImGuiWindowFlags_AlwaysAutoResize))
		return;

	auto label_or_default = [](const std::string &label,
				   const char *fallback) -> std::string {
		return label.empty() ? std::string(fallback) : label;
	};

	const std::string l1_label =
	    label_or_default(config.taxonomy_labels.l1_label, "Fleet Group");
	const std::string l2_label =
	    label_or_default(config.taxonomy_labels.l2_label, "Sub Group");
	const std::string l3_label =
	    label_or_default(config.taxonomy_labels.l3_label, "Function");

	ImGui::TextUnformatted("Add Group");
	ImGui::TextWrapped(
	    "Create a taxonomy group under the serve root for later releases.");
	ImGui::Separator();

	auto draw_group_field = [&](const std::string &label,
				    std::string &value, const char *id_suffix) {
		const std::string field_label = label + "##" + id_suffix;
		ImGui::InputText(field_label.c_str(), &value);
		if (value.empty()) {
			ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
					   "%s is required.", label.c_str());
		} else if (!is_valid_taxonomy_component(value, nullptr)) {
			ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
					   "%s uses letters, numbers, dashes, "
					   "and underscores only.",
					   label.c_str());
		}
	};

	draw_group_field(l1_label, group_l1, "GroupL1");
	draw_group_field(l2_label, group_l2, "GroupL2");
	draw_group_field(l3_label, group_l3, "GroupL3");

	const bool can_create = is_valid_taxonomy_component(group_l1,
							    nullptr) &&
				is_valid_taxonomy_component(group_l2,
							    nullptr) &&
				is_valid_taxonomy_component(group_l3, nullptr);

	const auto serve_root_path = get_serve_root_path(st.package_output_dir);
	if (can_create) {
		const auto group_path = serve_root_path / group_l1 / group_l2 /
					group_l3;
		ImGui::TextWrapped("Group path: %s",
				   group_path.string().c_str());
	} else {
		ImGui::TextUnformatted(
		    "Group path: <set all fields to preview>");
	}

	if (!error_message.empty()) {
		ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s",
				   error_message.c_str());
	}

	ImGui::Separator();
	ImGui::BeginDisabled(!can_create);
	if (ImGui::Button("Create Group")) {
		error_message.clear();
		const auto group_path = serve_root_path / group_l1 / group_l2 /
					group_l3;
		std::error_code ec;
		const bool created =
		    std::filesystem::create_directories(group_path, ec);
		if (ec) {
			error_message = "Failed to create group: " +
					ec.message();
		} else {
			st.taxonomy.l1 = group_l1;
			st.taxonomy.l2 = group_l2;
			st.taxonomy.l3 = group_l3;
			config.last_l1 = group_l1;
			config.last_l2 = group_l2;
			config.last_l3 = group_l3;
			persist_configuration(st, config);
			log_line(created
				     ? ("Group created: " + group_path.string())
				     : ("Group already exists: " +
					group_path.string()));
			if (open)
				*open = false;
			ImGui::CloseCurrentPopup();
		}
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button("Cancel")) {
		if (open)
			*open = false;
		ImGui::CloseCurrentPopup();
	}

	ImGui::EndPopup();
}

bool remove_release_contents(const std::filesystem::path &release_path,
			     std::string *err_out) {
	namespace fs = std::filesystem;

	std::error_code ec;
	if (!fs::exists(release_path, ec)) {
		if (err_out)
			*err_out = "Release path does not exist: " +
				   release_path.string();
		return false;
	}
	if (!fs::is_directory(release_path, ec)) {
		if (err_out)
			*err_out = "Release path is not a directory: " +
				   release_path.string();
		return false;
	}

	std::vector<fs::path> entries;
	for (fs::directory_iterator it(release_path, ec);
	     it != fs::directory_iterator(); it.increment(ec)) {
		if (ec)
			break;
		entries.push_back(it->path());
	}

	if (ec) {
		if (err_out)
			*err_out = "Failed to read release directory: " +
				   release_path.string();
		return false;
	}

	for (const auto &entry_path : entries) {
		fs::remove_all(entry_path, ec);
		if (ec) {
			if (err_out)
				*err_out = "Failed to remove: " +
					   entry_path.string();
			return false;
		}
	}

	return true;
}

bool remove_group_tree(const std::filesystem::path &serve_root,
		       const TaxonomyPath &taxonomy, std::string *err_out) {
	namespace fs = std::filesystem;

	const fs::path group_path = serve_root / taxonomy.l1 / taxonomy.l2 /
				    taxonomy.l3;
	std::error_code ec;
	if (!fs::exists(group_path, ec)) {
		if (err_out)
			*err_out = "Group path does not exist: " +
				   group_path.string();
		return false;
	}
	if (!fs::is_directory(group_path, ec)) {
		if (err_out)
			*err_out = "Group path is not a directory: " +
				   group_path.string();
		return false;
	}

	fs::remove_all(group_path, ec);
	if (ec) {
		if (err_out)
			*err_out = "Failed to delete group: " +
				   group_path.string();
		return false;
	}

	fs::path parent = group_path.parent_path();
	while (!parent.empty() && parent != serve_root) {
		if (!fs::is_empty(parent, ec) || ec)
			break;
		fs::remove(parent, ec);
		if (ec)
			break;
		parent = parent.parent_path();
	}

	return true;
}

bool draw_delete_release_modal(const std::filesystem::path &release_path,
			       const std::string &release_label, bool *open,
			       std::string &error_message) {
	bool deleted = false;
	if (open && *open && !ImGui::IsPopupOpen("Delete Release"))
		ImGui::OpenPopup("Delete Release");

	if (!open || !*open)
		return false;

	ImGui::SetNextWindowSize(ImVec2(520.0f, 260.0f), ImGuiCond_Once);
	if (!ImGui::BeginPopupModal("Delete Release", open,
				    ImGuiWindowFlags_AlwaysAutoResize))
		return false;

	ImGui::TextUnformatted("Delete Release");
	ImGui::TextWrapped("This deletes the manifest and payload directories "
			   "for the selected release. "
			   "The group folder will remain.");
	ImGui::Separator();
	ImGui::Text("Release: %s", release_label.c_str());
	ImGui::Text("Path: %s", release_path.string().c_str());

	if (!error_message.empty())
		ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s",
				   error_message.c_str());

	ImGui::Separator();
	ImGui::BeginDisabled(release_path.empty());
	if (ImGui::Button("Delete Release")) {
		error_message.clear();
		std::string err;
		if (remove_release_contents(release_path, &err)) {
			log_line("Release deleted: " + release_path.string());
			if (open)
				*open = false;
			ImGui::CloseCurrentPopup();
			deleted = true;
		} else {
			error_message = err.empty()
					    ? "Failed to delete release."
					    : err;
		}
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button("Cancel")) {
		if (open)
			*open = false;
		ImGui::CloseCurrentPopup();
	}

	ImGui::EndPopup();
	return deleted;
}

bool draw_delete_group_modal(const std::filesystem::path &serve_root,
			     const TaxonomyPath &taxonomy,
			     const std::string &group_label, bool *open,
			     std::string &error_message) {
	bool deleted = false;
	if (open && *open && !ImGui::IsPopupOpen("Delete Group"))
		ImGui::OpenPopup("Delete Group");

	if (!open || !*open)
		return false;

	ImGui::SetNextWindowSize(ImVec2(520.0f, 260.0f), ImGuiCond_Once);
	if (!ImGui::BeginPopupModal("Delete Group", open,
				    ImGuiWindowFlags_AlwaysAutoResize))
		return false;

	const std::filesystem::path group_path = serve_root / taxonomy.l1 /
						 taxonomy.l2 / taxonomy.l3;
	ImGui::TextUnformatted("Delete Group");
	ImGui::TextWrapped(
	    "This deletes the entire group folder and any payloads within it.");
	ImGui::Separator();
	ImGui::Text("Group: %s", group_label.c_str());
	ImGui::Text("Path: %s", group_path.string().c_str());

	if (!error_message.empty())
		ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s",
				   error_message.c_str());

	ImGui::Separator();
	ImGui::BeginDisabled(group_path.empty());
	if (ImGui::Button("Delete Group")) {
		error_message.clear();
		std::string err;
		if (remove_group_tree(serve_root, taxonomy, &err)) {
			log_line("Group deleted: " + group_path.string());
			if (open)
				*open = false;
			ImGui::CloseCurrentPopup();
			deleted = true;
		} else {
			error_message = err.empty() ? "Failed to delete group."
						    : err;
		}
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button("Cancel")) {
		if (open)
			*open = false;
		ImGui::CloseCurrentPopup();
	}

	ImGui::EndPopup();
	return deleted;
}

} // namespace

void draw_current_status_panel(ManifestState &st, HttpServerState &srv,
			       AppConfig &config) {
	static bool wizard_open = false;
	static bool add_group_open = false;
	static bool delete_release_open = false;
	static bool delete_group_open = false;
	static std::filesystem::path delete_release_path;
	static std::string delete_release_label;
	static std::string delete_release_error;
	static TaxonomyPath delete_group_taxonomy;
	static std::string delete_group_label;
	static std::string delete_group_error;
	static double last_scan_time = -1000.0;
	static std::vector<ReleaseSummary> cached_releases;
	static std::string group_filter;
	static std::string device_filter;
	static int selected_release = -1;

	const auto serve_root_path = get_serve_root_path(st.package_output_dir);
	if (ImGui::BeginTable("StatusActions", 2,
			      ImGuiTableFlags_SizingStretchProp)) {
		ImGui::TableSetupColumn("Actions",
					ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("ServeRoot",
					ImGuiTableColumnFlags_WidthFixed,
					360.0f);
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::TextUnformatted("Actions");
		if (ImGui::Button("Create Group"))
			add_group_open = true;
		ImGui::SameLine();
		if (ImGui::Button("Create Release..."))
			wizard_open = true;
		const bool has_selection = selected_release >= 0 &&
					   selected_release <
					       static_cast<int>(
						   cached_releases.size());
		ImGui::SameLine();
		ImGui::BeginDisabled(!has_selection);
		if (ImGui::Button("Delete Release")) {
			const auto &summary =
			    cached_releases[static_cast<size_t>(
				selected_release)];
			delete_release_path = summary.path;
			delete_release_label = release_display_name(summary);
			delete_release_error.clear();
			delete_release_open = true;
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(!has_selection);
		if (ImGui::Button("Delete Group")) {
			const auto &summary =
			    cached_releases[static_cast<size_t>(
				selected_release)];
			delete_group_taxonomy = summary.taxonomy;
			delete_group_label = taxonomy_display_name(summary);
			delete_group_error.clear();
			delete_group_open = true;
		}
		ImGui::EndDisabled();

		ImGui::TableSetColumnIndex(1);
		ImGui::TextUnformatted("Serve root");
		ImGui::TextWrapped("%s", serve_root_path.string().c_str());
		if (ImGui::Button("Open Serve Root"))
			handle_open_folder(serve_root_path);
		ImGui::EndTable();
	}

	ImGui::Separator();

	const double now = ImGui::GetTime();
	const bool refresh_now = ImGui::IsWindowAppearing() ||
				 (now - last_scan_time) > 2.0;

	if (refresh_now) {
		cached_releases = scan_release_summaries(st);
		last_scan_time = now;
		if (selected_release >=
		    static_cast<int>(cached_releases.size()))
			selected_release = -1;
	}

	const ImVec2 split_size(0.0f, ImGui::GetContentRegionAvail().y);
	if (ImGui::BeginTable("CurrentStatusSplit", 2,
			      ImGuiTableFlags_Resizable |
				  ImGuiTableFlags_BordersInnerV,
			      split_size)) {
		ImGui::TableSetupColumn("Groups",
					ImGuiTableColumnFlags_WidthStretch,
					0.55f);
		ImGui::TableSetupColumn("Details",
					ImGuiTableColumnFlags_WidthStretch,
					0.45f);
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::BeginChild("CurrentStatusLeft", ImVec2(0.0f, 0.0f),
				  false);
		ImGui::TextUnformatted("Groups & Releases");
		ImGui::Separator();
		ImGui::TextUnformatted("Filters");
		ImGui::Spacing();
		if (ImGui::BeginTable("CurrentStatusFilters", 2,
				      ImGuiTableFlags_SizingFixedFit)) {
			ImGui::TableSetupColumn(
			    "Label", ImGuiTableColumnFlags_WidthFixed, 110.0f);
			ImGui::TableSetupColumn(
			    "Input", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted("Group");
			ImGui::TableSetColumnIndex(1);
			ImGui::SetNextItemWidth(-1.0f);
			ImGui::InputTextWithHint("##GroupFilter",
						 "Filter by group name",
						 &group_filter);
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted("Device");
			ImGui::TableSetColumnIndex(1);
			ImGui::SetNextItemWidth(-1.0f);
			ImGui::InputTextWithHint("##DeviceFilter",
						 "Filter by device ID",
						 &device_filter);
			ImGui::EndTable();
		}

		std::vector<int> visible_indices;
		visible_indices.reserve(cached_releases.size());
		for (size_t i = 0; i < cached_releases.size(); ++i) {
			const auto &summary = cached_releases[i];
			const std::string group_name = taxonomy_display_name(
			    summary);
			if (!contains_case_insensitive(group_name,
						       group_filter))
				continue;

			if (!device_filter.empty()) {
				bool found_device = false;
				for (const auto &device : summary.devices) {
					if (contains_case_insensitive(
						device.device_id,
						device_filter)) {
						found_device = true;
						break;
					}
				}
				if (!found_device)
					continue;
			}

			visible_indices.push_back(static_cast<int>(i));
		}

		ImGui::Spacing();
		ImGui::Text("Releases: %zu", visible_indices.size());

		const float list_height =
		    std::max(0.0f, ImGui::GetContentRegionAvail().y);
		if (visible_indices.empty()) {
			ImGui::BeginChild("ReleaseListEmpty",
					  ImVec2(0.0f, list_height), true);
			ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
					   "No releases found yet.");
			ImGui::EndChild();
		} else if (ImGui::BeginTable("ReleaseSummaryTable", 7,
					     ImGuiTableFlags_Borders |
						 ImGuiTableFlags_RowBg |
						 ImGuiTableFlags_Resizable |
						 ImGuiTableFlags_ScrollY,
					     ImVec2(0.0f, list_height))) {
			ImGui::TableSetupColumn("Release");
			ImGui::TableSetupColumn("Group");
			ImGui::TableSetupColumn(
			    "Created", ImGuiTableColumnFlags_WidthFixed,
			    150.0f);
			ImGui::TableSetupColumn(
			    "Devices", ImGuiTableColumnFlags_WidthFixed, 80.0f);
			ImGui::TableSetupColumn(
			    "Files", ImGuiTableColumnFlags_WidthFixed, 80.0f);
			ImGui::TableSetupColumn(
			    "Size (bytes)", ImGuiTableColumnFlags_WidthFixed,
			    120.0f);
			ImGui::TableSetupColumn(
			    "Actions", ImGuiTableColumnFlags_WidthFixed, 80.0f);
			ImGui::TableHeadersRow();

			for (int idx : visible_indices) {
				const auto &summary =
				    cached_releases[static_cast<size_t>(idx)];
				const bool selected = (idx == selected_release);
				const std::string release_label =
				    release_display_name(summary);
				const std::string group_label =
				    taxonomy_display_name(summary);

				ImGui::PushID(idx);
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				if (ImGui::Selectable(
					release_label.c_str(), selected,
					ImGuiSelectableFlags_SpanAllColumns |
					    ImGuiSelectableFlags_AllowOverlap)) {
					selected_release = idx;
				}
				ImGui::TableSetColumnIndex(1);
				ImGui::TextUnformatted(group_label.c_str());
				ImGui::TableSetColumnIndex(2);
				ImGui::TextUnformatted(summary.created.c_str());
				ImGui::TableSetColumnIndex(3);
				ImGui::Text("%zu", summary.devices.size());
				ImGui::TableSetColumnIndex(4);
				ImGui::Text("%zu", summary.file_count);
				ImGui::TableSetColumnIndex(5);
				ImGui::Text("%llu",
					    static_cast<unsigned long long>(
						summary.total_size));
				ImGui::TableSetColumnIndex(6);
				if (ImGui::SmallButton("Open"))
					handle_open_folder(summary.path);
				ImGui::PopID();
			}
			ImGui::EndTable();
		}
		ImGui::EndChild();

		ImGui::TableSetColumnIndex(1);
		ImGui::BeginChild("CurrentStatusRight", ImVec2(0.0f, 0.0f),
				  false);
		ImGui::TextUnformatted("Release Details");
		ImGui::Separator();
		if (selected_release >= 0 &&
		    selected_release <
			static_cast<int>(cached_releases.size())) {
			const auto &summary =
			    cached_releases[static_cast<size_t>(
				selected_release)];
			ImGui::TextUnformatted("Group");
			ImGui::Text("%s",
				    taxonomy_display_name(summary).c_str());
			ImGui::Text("Path: %s", summary.path.string().c_str());
			ImGui::Separator();

			ImGui::TextUnformatted("Release");
			ImGui::Text("Name: %s", summary.release_name.c_str());
			ImGui::Text("Version: %s",
				    summary.release_version.c_str());
			ImGui::Text("Created: %s", summary.created.c_str());
			ImGui::Separator();

			const std::string base_url =
			    build_device_base_url(srv, summary.taxonomy);
			ImGui::TextUnformatted("Device Base URL");
			ImGui::SetNextItemWidth(-90.0f);
			std::string base_url_copy = base_url;
			ImGui::InputText("##DeviceBaseUrlStatus",
					 &base_url_copy,
					 ImGuiInputTextFlags_ReadOnly);
			ImGui::SameLine();
			if (ImGui::Button("Copy##DeviceBaseUrlStatus")) {
				ImGui::SetClipboardText(base_url.c_str());
				log_line("Device base URL copied: " + base_url);
			}

			ImGui::Separator();
			ImGui::TextUnformatted("Devices");
			const float device_height =
			    std::max(0.0f, ImGui::GetContentRegionAvail().y);
			if (summary.devices.empty()) {
				ImGui::BeginChild("DeviceSummaryEmpty",
						  ImVec2(0.0f, device_height),
						  true);
				ImGui::TextColored(
				    ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
				    "No devices found for this release.");
				ImGui::EndChild();
			} else if (ImGui::BeginTable(
				       "DeviceSummaryTable", 4,
				       ImGuiTableFlags_Borders |
					   ImGuiTableFlags_RowBg |
					   ImGuiTableFlags_ScrollY,
				       ImVec2(0.0f, device_height))) {
				ImGui::TableSetupColumn("Device ID");
				ImGui::TableSetupColumn(
				    "Files", ImGuiTableColumnFlags_WidthFixed,
				    80.0f);
				ImGui::TableSetupColumn(
				    "Size (bytes)",
				    ImGuiTableColumnFlags_WidthFixed, 120.0f);
				ImGui::TableSetupColumn(
				    "Open", ImGuiTableColumnFlags_WidthFixed,
				    60.0f);
				ImGui::TableHeadersRow();

				for (size_t i = 0; i < summary.devices.size();
				     ++i) {
					const auto &device = summary.devices[i];
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextUnformatted(
					    device.device_id.c_str());
					ImGui::TableSetColumnIndex(1);
					ImGui::Text("%zu", device.file_count);
					ImGui::TableSetColumnIndex(2);
					ImGui::Text(
					    "%llu",
					    static_cast<unsigned long long>(
						device.total_size));
					ImGui::TableSetColumnIndex(3);
					ImGui::PushID(static_cast<int>(i));
					if (ImGui::SmallButton("Open"))
						handle_open_folder(
						    summary.path /
						    device.device_id);
					ImGui::PopID();
				}
				ImGui::EndTable();
			}
		} else {
			ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
					   "Select a release to view details.");
		}
		ImGui::EndChild();
		ImGui::EndTable();
	}

	const bool release_deleted = draw_delete_release_modal(
	    delete_release_path, delete_release_label, &delete_release_open,
	    delete_release_error);
	const bool group_deleted = draw_delete_group_modal(
	    serve_root_path, delete_group_taxonomy, delete_group_label,
	    &delete_group_open, delete_group_error);
	if (release_deleted || group_deleted) {
		cached_releases = scan_release_summaries(st);
		last_scan_time = ImGui::GetTime();
		selected_release = -1;
	}

	draw_add_group_modal(st, config, &add_group_open);
	draw_release_wizard_modal(st, srv, config, &wizard_open);
}

void draw_configuration_modal(ManifestState &st, AppConfig &config,
			      bool *open) {
	if (ImGui::BeginPopupModal("Configuration", open,
				   ImGuiWindowFlags_AlwaysAutoResize)) {
		if (ImGui::BeginTabBar("ConfigurationTabs")) {
			if (ImGui::BeginTabItem("Signing")) {
				ImGui::TextUnformatted("Signer private key");
				ImGui::TextWrapped(
				    "%s", st.signer_key_path.empty()
					      ? "<no key selected>"
					      : st.signer_key_path.c_str());
				if (ImGui::Button("Select key file")) {
					IGFD::FileDialogConfig config_dialog;
					config_dialog.path = ".";
					config_dialog.countSelectionMax = 1;
					config_dialog.flags =
					    ImGuiFileDialogFlags_NaturalSorting;
					ImGuiFileDialog::Instance()->OpenDialog(
					    "SignerKeyDlg", "Select signer key",
					    ".pem,.key,.*", config_dialog);
				}
				ImGui::SameLine();
				if (!st.signer_key_path.empty() &&
				    ImGui::Button("Clear##SignerKey")) {
					st.signer_key_path.clear();
					log_line(
					    "Signer key selection cleared.");
					persist_configuration(st, config);
				}

				ImGui::Separator();
				ImGui::TextUnformatted(
				    "Signer certificate (copied into each "
				    "package)");
				ImGui::TextWrapped(
				    "%s", st.signer_cert_path.empty()
					      ? "<no certificate selected>"
					      : st.signer_cert_path.c_str());
				if (ImGui::Button("Select certificate file")) {
					IGFD::FileDialogConfig config_dialog;
					config_dialog.path = ".";
					config_dialog.countSelectionMax = 1;
					config_dialog.flags =
					    ImGuiFileDialogFlags_NaturalSorting;
					ImGuiFileDialog::Instance()->OpenDialog(
					    "SignerCertDlg",
					    "Select signer certificate",
					    ".pem,.crt,.cer,.der,.*",
					    config_dialog);
				}
				ImGui::SameLine();
				if (!st.signer_cert_path.empty() &&
				    ImGui::Button("Clear##SignerCert")) {
					st.signer_cert_path.clear();
					log_line("Signer certificate selection "
						 "cleared.");
					persist_configuration(st, config);
				}

				ImGui::Spacing();
				ImGui::TextWrapped(
				    "The certificate file will be added to "
				    "each generated package so downstream "
				    "tooling can pick it up.");
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("TLS")) {
				ImGui::TextWrapped(
				    "Used for mutual TLS (mTLS) connections "
				    "when sharing artifacts.");
				ImGui::Separator();

				ImGui::TextUnformatted("TLS private key");
				ImGui::TextWrapped(
				    "%s", st.tls_key_path.empty()
					      ? "<no key selected>"
					      : st.tls_key_path.c_str());
				if (ImGui::Button("Select TLS key file")) {
					IGFD::FileDialogConfig config_dialog;
					config_dialog.path = ".";
					config_dialog.countSelectionMax = 1;
					config_dialog.flags =
					    ImGuiFileDialogFlags_NaturalSorting;
					ImGuiFileDialog::Instance()->OpenDialog(
					    "TlsKeyDlg", "Select TLS key",
					    ".pem,.key,.*", config_dialog);
				}
				ImGui::SameLine();
				if (!st.tls_key_path.empty() &&
				    ImGui::Button("Clear##TlsKey")) {
					st.tls_key_path.clear();
					log_line("TLS key selection cleared.");
					persist_configuration(st, config);
				}

				ImGui::Separator();
				ImGui::TextUnformatted("TLS certificate");
				ImGui::TextWrapped(
				    "%s", st.tls_cert_path.empty()
					      ? "<no certificate selected>"
					      : st.tls_cert_path.c_str());
				if (ImGui::Button(
					"Select TLS certificate file")) {
					IGFD::FileDialogConfig config_dialog;
					config_dialog.path = ".";
					config_dialog.countSelectionMax = 1;
					config_dialog.flags =
					    ImGuiFileDialogFlags_NaturalSorting;
					ImGuiFileDialog::Instance()->OpenDialog(
					    "TlsCertDlg",
					    "Select TLS certificate",
					    ".pem,.crt,.cer,.der,.*",
					    config_dialog);
				}
				ImGui::SameLine();
				if (!st.tls_cert_path.empty() &&
				    ImGui::Button("Clear##TlsCert")) {
					st.tls_cert_path.clear();
					log_line("TLS certificate selection "
						 "cleared.");
					persist_configuration(st, config);
				}
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Terminology")) {
				ImGui::TextWrapped(
				    "UI labels only. Directory names and "
				    "manifest keys stay unchanged.");
				bool labels_dirty = false;
				labels_dirty =
				    ImGui::InputText(
					"L1 label",
					&config.taxonomy_labels.l1_label) ||
				    labels_dirty;
				labels_dirty =
				    ImGui::InputText(
					"L2 label",
					&config.taxonomy_labels.l2_label) ||
				    labels_dirty;
				labels_dirty =
				    ImGui::InputText(
					"L3 label",
					&config.taxonomy_labels.l3_label) ||
				    labels_dirty;
				labels_dirty =
				    ImGui::InputText(
					"Device selector label",
					&config.taxonomy_labels
					     .device_selector_label) ||
				    labels_dirty;
				if (labels_dirty)
					save_app_config(config);
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Output")) {
				ImGui::TextUnformatted("Serve root directory");
				const std::string output_display =
				    st.package_output_dir.empty()
					? (std::string(kDefaultServeRoot) +
					   " (default)")
					: st.package_output_dir;
				ImGui::TextWrapped("%s",
						   output_display.c_str());
				if (ImGui::Button("Select serve root")) {
					IGFD::FileDialogConfig config_dialog;
					config_dialog.path =
					    st.package_output_dir.empty()
						? "."
						: st.package_output_dir;
					config_dialog.countSelectionMax = 1;
					config_dialog.flags =
					    ImGuiFileDialogFlags_NaturalSorting;
					ImGuiFileDialog::Instance()->OpenDialog(
					    "OutputDirDlg",
					    "Select output directory", nullptr,
					    config_dialog);
				}
				ImGui::SameLine();
				if (ImGui::Button("Use default##ServeRoot")) {
					st.package_output_dir =
					    kDefaultServeRoot;
					log_line(std::string("Serve root reset "
							     "to default: ") +
						 kDefaultServeRoot);
					persist_configuration(st, config);
				}
				ImGui::EndTabItem();
			}

			ImGui::EndTabBar();
		}

		ImGui::Separator();
		if (ImGui::Button("Close")) {
			if (open)
				*open = false;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	if (ImGuiFileDialog::Instance()->Display("SignerKeyDlg")) {
		if (ImGuiFileDialog::Instance()->IsOk()) {
			const std::string key_path =
			    ImGuiFileDialog::Instance()->GetFilePathName();
			if (!key_path.empty()) {
				st.signer_key_path = key_path;
				log_line("Signer key selected: " + key_path);
				persist_configuration(st, config);
			}
		}
		ImGuiFileDialog::Instance()->Close();
	}

	if (ImGuiFileDialog::Instance()->Display("SignerCertDlg")) {
		if (ImGuiFileDialog::Instance()->IsOk()) {
			const std::string cert_path =
			    ImGuiFileDialog::Instance()->GetFilePathName();
			if (!cert_path.empty()) {
				st.signer_cert_path = cert_path;
				log_line("Signer certificate selected: " +
					 cert_path);
				persist_configuration(st, config);
			}
		}
		ImGuiFileDialog::Instance()->Close();
	}

	if (ImGuiFileDialog::Instance()->Display("TlsKeyDlg")) {
		if (ImGuiFileDialog::Instance()->IsOk()) {
			const std::string key_path =
			    ImGuiFileDialog::Instance()->GetFilePathName();
			if (!key_path.empty()) {
				st.tls_key_path = key_path;
				log_line("TLS key selected: " + key_path);
				persist_configuration(st, config);
			}
		}
		ImGuiFileDialog::Instance()->Close();
	}

	if (ImGuiFileDialog::Instance()->Display("TlsCertDlg")) {
		if (ImGuiFileDialog::Instance()->IsOk()) {
			const std::string cert_path =
			    ImGuiFileDialog::Instance()->GetFilePathName();
			if (!cert_path.empty()) {
				st.tls_cert_path = cert_path;
				log_line("TLS certificate selected: " +
					 cert_path);
				persist_configuration(st, config);
			}
		}
		ImGuiFileDialog::Instance()->Close();
	}

	if (ImGuiFileDialog::Instance()->Display("OutputDirDlg")) {
		if (ImGuiFileDialog::Instance()->IsOk()) {
			std::string dir_path =
			    ImGuiFileDialog::Instance()->GetFilePathName();
			if (dir_path.empty())
				dir_path = ImGuiFileDialog::Instance()
					       ->GetCurrentPath();
			if (!dir_path.empty()) {
				st.package_output_dir = dir_path;
				log_line("Serve root set to: " + dir_path);
				persist_configuration(st, config);
			}
		}
		ImGuiFileDialog::Instance()->Close();
	}
}

void draw_http_server_panel(const ManifestState &st, HttpServerState &srv) {
	ImGui::TextUnformatted("HTTPS Server");
	ImGui::TextWrapped("Lightweight controls for serving generated "
			   "packages over HTTP(S) while testing OTA flows.");
	ImGui::Separator();

	if (ImGui::Checkbox("Serve over HTTPS", &srv.use_https)) {
		if (!srv.running)
			srv.port = srv.use_https ? 8443 : 8080;
		else
			log_line("Stop the server before switching protocols.");
	}

	ImGui::InputText("Bind address", srv.bind_address,
			 IM_ARRAYSIZE(srv.bind_address));
	if (!std::strlen(srv.bind_address)) {
		ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
				   "Bind address is required.");
	} else if (contains_space(srv.bind_address)) {
		ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
				   "Bind address must not contain spaces.");
	}

	if (ImGui::InputInt("Port", &srv.port)) {
		if (srv.port < 1)
			srv.port = 1;
		else if (srv.port > 65535)
			srv.port = 65535;
	}
	if (srv.port < 1024) {
		ImGui::TextColored(
		    ImVec4(0.9f, 0.7f, 0.2f, 1.0f),
		    "Ports under 1024 may require elevated permissions.");
	}

	ImGui::InputText("Public host", srv.public_host,
			 IM_ARRAYSIZE(srv.public_host));
	if (!std::strlen(srv.public_host)) {
		ImGui::TextColored(
		    ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
		    "Public host is required for device base URLs.");
	} else if (contains_space(srv.public_host)) {
		ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
				   "Public host must not contain spaces.");
	}

	ImGui::InputText("Content root", srv.doc_root,
			 IM_ARRAYSIZE(srv.doc_root));
	ImGui::SameLine();
	if (ImGui::Button("Use serve root")) {
		sync_server_doc_root(st, srv);
		const std::string dir = srv.doc_root;
		log_line("HTTP server content root set to serve root: " + dir);
	}

	if (srv.use_https) {
		if (st.tls_cert_path.empty() || st.tls_key_path.empty()) {
			ImGui::TextColored(
			    ImVec4(1.0f, 0.6f, 0.0f, 1.0f),
			    "TLS key/certificate missing. Configure them in "
			    "the Configuration panel.");
		} else {
			ImGui::TextWrapped("TLS certificate: %s",
					   st.tls_cert_path.c_str());
			ImGui::TextWrapped("TLS private key: %s",
					   st.tls_key_path.c_str());
		}
	}

	ImGui::Separator();
	ImGui::Text("Status: %s", srv.running ? "Running" : "Stopped");

	if (!srv.running) {
		if (ImGui::Button("Start server", ImVec2(140, 0))) {
			start_civetweb(st, srv);
		}
	} else {
		if (ImGui::Button("Stop server", ImVec2(140, 0))) {
			stop_civetweb(srv);
		}
		ImGui::SameLine();
		const auto uptime =
		    std::chrono::duration_cast<std::chrono::duration<float>>(
			std::chrono::steady_clock::now() - srv.started_at)
			.count();
		ImGui::Text("Uptime: %.1f s", uptime);
	}

	if (!srv.status.empty()) {
		ImGui::Separator();
		ImGui::TextWrapped("%s", srv.status.c_str());
	}
}

void draw_log_panel() {
	ImGui::TextUnformatted("Log");
	if (ImGui::Button("Clear log"))
		log_clear();
	ImGui::SameLine();
	if (ImGui::Button("Copy to clipboard")) {
		std::string combined;
		for (const auto &line : log_entries()) {
			combined += line;
			combined.push_back('\n');
		}
		ImGui::SetClipboardText(combined.c_str());
		log_line("Log copied to clipboard.");
	}

	ImGui::Separator();

	static std::string log_text;
	static size_t last_log_count = 0;
	const auto &entries = log_entries();
	if (entries.size() != last_log_count) {
		log_text.clear();
		for (const auto &line : entries) {
			log_text += line;
			log_text.push_back('\n');
		}
		last_log_count = entries.size();
	}

	ImGui::InputTextMultiline("##log_text", &log_text, ImVec2(0, 0),
				  ImGuiInputTextFlags_ReadOnly |
				      ImGuiInputTextFlags_NoUndoRedo);
}
} // namespace ui
