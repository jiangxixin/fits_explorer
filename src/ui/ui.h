#pragma once

#include <string>
#include <vector>
#include <functional>
#include <optional>

struct FileRecord;
struct HDURecord;
struct ThumbnailRecord;
struct IndexedFile;

struct ThumbCacheEntry {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;
    int64_t last_access = 0;
};

class UI {
public:
    UI() = default;
    ~UI() = default;

    // Initialize ImGui (call once after window creation)
    void init();
    void shutdown();

    // Main render loop (call every frame)
    void render();

    // ---- Callbacks (set by application) ----
    std::function<void(const std::string& dir)> on_scan_folder;
    std::function<void()> on_reindex;
    std::function<void(const std::string& query)> on_search;
    std::function<void(int64_t file_id, int hdu_index)> on_select_hdu;

    // ---- Data feeds (set by application each frame) ----
    void set_files(const std::vector<FileRecord>& files);
    void set_hdus(const std::vector<HDURecord>& hdus);
    void set_thumbnail(int64_t file_id, int hdu_index, const ThumbnailRecord* thumb);
    void set_status(const std::string& status, int64_t db_file_count, int64_t thumb_count);
    void set_progress(const std::string& current_file, int current, int total, bool active);
    void set_selected_file(const FileRecord* file);
    void set_indexing(bool indexing);

    // Open/close file dialog
    bool wants_folder_dialog() { bool v = _wants_folder_dialog; _wants_folder_dialog = false; return v; }
    const std::string& pending_folder() const { return _pending_folder; }

    // Error display
    void show_error(const std::string& msg);
    void clear_error();

private:
    void render_menu_bar();
    void render_folder_tree();
    void render_file_grid();
    void render_hdu_detail();
    void render_status_bar();
    void render_search_bar();

    // Thumbnail GL texture management
    int get_thumbnail_texture(int64_t file_id, int hdu_index,
                               const uint8_t* pixels, int w, int h);
    void prune_thumb_cache();

    bool _wants_folder_dialog = false;
    std::string _pending_folder;
    bool _show_error = false;
    std::string _error_msg;
    bool _show_search = false;

    // UI state
    std::string _search_query;
    std::string _current_folder;
    int64_t _selected_file_id = -1;
    int _selected_hdu_index = 0;
    int64_t _selected_hdu_id = -1;

    // Data references (owned by app, not UI)
    const std::vector<FileRecord>* _files = nullptr;
    const std::vector<HDURecord>* _hdus = nullptr;

    // Cached thumbnails: key = (file_id << 32) | hdu_index
    std::vector<ThumbCacheEntry> _thumb_cache;
    std::vector<int> _thumb_gl_ids;  // OpenGL texture IDs (placeholder)

    // Layout
    float _left_panel_w = 220.0f;
    float _right_panel_w = 320.0f;
    int _grid_thumb_size = 96;

    bool _indexing = false;
    std::string _progress_file;
    int _progress_cur = 0, _progress_tot = 0;

    std::string _status_text;
    int64_t _status_file_count = 0;
    int64_t _status_thumb_count = 0;
};
