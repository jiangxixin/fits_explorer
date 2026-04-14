#include "ui.h"
#include "../database/database.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <ImGuiFileDialog.h>
#include <cstdio>

// ImGui color theme
namespace ImGui {
    static void SetupLightTheme() {
        ImGuiStyle* style = &ImGui::GetStyle();
        ImVec4* colors = style->Colors;
        colors[ImGuiCol_WindowBg]        = ImVec4(0.95f, 0.95f, 0.95f, 1.0f);
        colors[ImGuiCol_Header]          = ImVec4(0.85f, 0.88f, 0.92f, 1.0f);
        colors[ImGuiCol_HeaderHovered]    = ImVec4(0.75f, 0.80f, 0.88f, 1.0f);
        colors[ImGuiCol_HeaderActive]    = ImVec4(0.65f, 0.72f, 0.82f, 1.0f);
        colors[ImGuiCol_Button]          = ImVec4(0.85f, 0.88f, 0.92f, 1.0f);
        colors[ImGuiCol_ButtonHovered]    = ImVec4(0.75f, 0.80f, 0.88f, 1.0f);
        colors[ImGuiCol_FrameBg]         = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        colors[ImGuiCol_FrameBgHovered]  = ImVec4(0.9f, 0.92f, 0.95f, 1.0f);
        style->WindowRounding = 6;
        style->FrameRounding = 4;
        style->ScrollbarRounding = 4;
    }
}

void UI::init() {
    ImGui::SetupLightTheme();
}

void UI::shutdown() {
    // Free GL textures
}

void UI::render() {
    clear_error();

    // ---- Menu Bar ----
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open Folder...", "Ctrl+O")) {
                _wants_folder_dialog = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Reindex All", "Ctrl+R")) {
                if (on_reindex) on_reindex();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) {}
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Search", "Ctrl+F", &_show_search);
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    // ---- Search Bar (collapsible) ----
    if (_show_search) {
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputTextWithHint("##search", "Search files, keywords, values...",
                                      &_search_query, ImGuiInputTextFlags_EnterReturnText)) {
            if (on_search && !_search_query.empty()) {
                on_search(_search_query);
            }
        }
        if (!_search_query.empty() && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            _search_query.clear();
        }
    }

    // ---- Main layout: Left Panel | Center Grid | Right Panel ----
    float avail_w = ImGui::GetContentRegionAvail().x;

    // ---- Left: Folder Tree ----
    ImGui::BeginChild("##left", ImVec2(_left_panel_w, -30), true);
    render_folder_tree();
    ImGui::EndChild();

    ImGui::SameLine();

    // ---- Center: File Grid ----
    float center_x = avail_w - _right_panel_w - _left_panel_w - 8;
    if (center_x > 200) {
        ImGui::BeginChild("##center", ImVec2(center_x, -30), false);
        render_file_grid();
        ImGui::EndChild();
        ImGui::SameLine();
    } else {
        // If not enough space, put grid in center and collapse right
        _right_panel_w = 0;
    }

    // ---- Right: HDU Detail ----
    if (_right_panel_w > 100) {
        ImGui::BeginChild("##right", ImVec2(_right_panel_w, -30), true);
        render_hdu_detail();
        ImGui::EndChild();
    }

    // ---- Bottom: Status Bar ----
    ImGui::Separator();
    render_status_bar();

    // ---- Error overlay ----
    if (_show_error) {
        ImGui::OpenPopup("Error");
        if (ImGui::BeginPopupModal("Error", &_show_error,
                                    ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("%s", _error_msg.c_str());
            if (ImGui::Button("OK")) {
                _show_error = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    // ---- Indexing progress overlay ----
    if (_indexing) {
        ImGui::SetNextWindowBgAlpha(0.8f);
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                                  ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        if (ImGui::Begin("Indexing...", nullptr,
                          ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                          ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Indexing: %s", _progress_file.c_str());
            ImGui::Text("%d / %d", _progress_cur, _progress_tot);
            if (_progress_tot > 0) {
                ImGui::ProgressBar((float)_progress_cur / _progress_tot, ImVec2(300, 20));
            }
        }
        ImGui::End();
    }
}

void UI::render_folder_tree() {
    ImGui::Text("Folders");
    ImGui::Separator();
    ImGui::Spacing();

    if (_current_folder.empty()) {
        ImGui::Indent();
        ImGui::TextColored(ImGui::GetStyle().Colors[ImGuiCol_Disabled],
                            "(No folder open)");
        ImGui::Unindent();
    } else {
        // Show folder path with icon
        ImGuiTreeNodeFlags flags = ImGui::TreeNodeFlags_Leaf |
                                    ImGui::TreeNodeFlags_OpenOnDoubleClick;
        if (ImGui::TreeNodeEx("📁  _root_", flags)) {
            ImGui::TreePop();
        }

        ImGui::TextWrapped("%s", _current_folder.c_str());
    }

    ImGui::Spacing();
    if (ImGui::Button("📂  Open Folder", ImVec2(-1, 0))) {
        _wants_folder_dialog = true;
    }
}

void UI::render_file_grid() {
    ImGui::Text("Files");
    ImGui::SameLine();
    ImGui::TextDisabled("(%d)", (int)(_files ? _files->size() : 0));

    // Toolbar
    ImGui::Separator();
    ImGui::Spacing();

    // Thumbnail size slider
    ImGui::SetNextItemWidth(120);
    ImGui::SliderInt("Thumb", &_grid_thumb_size, 48, 256, "%dpx");
    ImGui::SameLine();

    if (ImGui::Button("Reindex All")) {
        if (on_reindex) on_reindex();
    }
    ImGui::Separator();

    if (!_files || _files->empty()) {
        ImGui::TextColored(ImGui::GetStyle().Colors[ImGuiCol_Disabled],
                             "No files. Open a folder to begin.");
        return;
    }

    // File grid using table
    int cols = std::max(1, (int)(ImGui::GetContentRegionAvail().x) / (_grid_thumb_size + 20));
    if (ImGui::BeginTable("##filetable", cols,
                           ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit)) {
        for (size_t i = 0; i < _files->size();) {
            ImGui::TableNextRow();
            for (int col = 0; col < cols && i < _files->size(); col++, i++) {
                ImGui::TableSetColumnIndex(col);
                const FileRecord& f = (*_files)[i];

                ImGui::BeginGroup();

                // Thumbnail placeholder (gray box with icon)
                ImVec2 thumb_size(_grid_thumb_size, _grid_thumb_size);
                ImGui::BeginChild(("##thumb_" + std::to_string(f.id)).c_str(),
                                   thumb_size, true,
                                   ImGuiWindowFlags_NoScrollbar);
                float tw = ImGui::CalcTextSize("📷").x;
                ImGui::SetCursorPosX((_grid_thumb_size - tw) * 0.5f);
                ImGui::Text("📷");
                ImGui::EndChild();

                // Filename (truncated)
                std::string name = f.filename;
                if (name.length() > 20) {
                    name = name.substr(0, 17) + "...";
                }
                ImGui::TextWrapped("%s", name.c_str());

                // File info
                ImGui::TextDisabled("%d HDUs", f.hdu_count);

                if (ImGui::IsItemClicked()) {
                    _selected_file_id = f.id;
                    if (on_select_hdu) on_select_hdu(f.id, 0);
                }

                ImGui::EndGroup();
                ImGui::Spacing();
            }
        }
        ImGui::EndTable();
    }
}

void UI::render_hdu_detail() {
    ImGui::Text("HDU Detail");
    ImGui::Separator();

    if (_selected_file_id < 0) {
        ImGui::TextColored(ImGui::GetStyle().Colors[ImGuiCol_Disabled],
                             "Select a file to view details.");
        return;
    }

    if (!_hdus || _hdus->empty()) {
        ImGui::TextColored(ImGui::GetStyle().Colors[ImGuiCol_Disabled],
                             "No HDU info available.");
        return;
    }

    // HDU tabs
    ImGui::Text("HDUs:");
    for (size_t i = 0; i < _hdus->size(); i++) {
        const HDURecord& h = (*_hdus)[i];
        char label[32];
        snprintf(label, sizeof(label), "HDU %d##%zu", h.hdu_index, i);
        if (ImGui::Selectable(label, (int)_selected_hdu_index == (int)i)) {
            _selected_hdu_index = (int)i;
        }
        if (ImGui::IsItemClicked() && on_select_hdu) {
            on_select_hdu(_selected_file_id, h.hdu_index);
        }
    }

    ImGui::Separator();

    // Show selected HDU details
    if (_selected_hdu_index < (int)_hdus->size()) {
        const HDURecord& h = (*_hdus)[_selected_hdu_index];
        ImGui::Text("Index:   %d", h.hdu_index);
        ImGui::Text("Type:    %s", h.hdu_type.c_str());
        ImGui::Text("BITPIX:  %d", h.bitpix);
        ImGui::Text("NAXIS:   %d", h.naxis);
        if (h.naxis >= 1) ImGui::Text("NAXIS1:  %d", h.naxis1);
        if (h.naxis >= 2) ImGui::Text("NAXIS2:  %d", h.naxis2);
        if (h.naxis >= 3) ImGui::Text("NAXIS3:  %d", h.naxis3);
    }
}

void UI::render_status_bar() {
    ImGui::Text("%s", _status_text.c_str());
    ImGui::SameLine();
    ImGui::Text("Files: %lld | Thumbnails: %lld",
                 (long long)_status_file_count,
                 (long long)_status_thumb_count);
}

// ---- Data setters ----

void UI::set_files(const std::vector<FileRecord>& files) {
    _files = &files;
}

void UI::set_hdus(const std::vector<HDURecord>& hdus) {
    _hdus = &hdus;
}

void UI::set_thumbnail(int64_t file_id, int hdu_index,
                         const ThumbnailRecord* thumb) {
    // Thumbnail cache logic
    (void)file_id; (void)hdu_index; (void)thumb;
}

void UI::set_status(const std::string& status, int64_t db_file_count,
                     int64_t thumb_count) {
    _status_text = status;
    _status_file_count = db_file_count;
    _status_thumb_count = thumb_count;
}

void UI::set_progress(const std::string& current_file,
                       int current, int total, bool active) {
    _progress_file = current_file;
    _progress_cur = current;
    _progress_tot = total;
    _indexing = active;
}

void UI::set_selected_file(const FileRecord* file) {
    if (file) {
        _selected_file_id = file->id;
    } else {
        _selected_file_id = -1;
    }
}

void UI::set_indexing(bool indexing) {
    _indexing = indexing;
}

void UI::show_error(const std::string& msg) {
    _error_msg = msg;
    _show_error = true;
}

void UI::clear_error() {
    if (!_show_error) _error_msg.clear();
}

int UI::get_thumbnail_texture(int64_t file_id, int hdu_index,
                               const uint8_t* pixels, int w, int h) {
    (void)file_id; (void)hdu_index; (void)pixels; (void)w; (void)h;
    return 0;  // GL texture ID placeholder
}

void UI::prune_thumb_cache() {
    // Evict LRU entries if cache is too large
}
