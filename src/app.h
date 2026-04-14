#pragma once

#include <string>
#include <vector>
#include <thread>
#include <atomic>

class Database;
class Indexer;
class ThumbnailGenerator;
class UI;

class App {
public:
    App();
    ~App();

    bool init(const std::string& db_path);
    void run();

    // File operations
    void open_folder(const std::string& path);
    void reindex_all();
    void select_file(int64_t file_id);
    void select_hdu(int64_t file_id, int hdu_index);
    void search(const std::string& query);

    // ImGui integration
    void render();

    bool wants_to_close() const { return _wants_close; }
    void on_close() { _wants_close = true; }

private:
    void background_index(const std::vector<IndexedFile>& files);
    void update_status();

    Database* _db = nullptr;
    Indexer* _indexer = nullptr;
    ThumbnailGenerator* _thumb_gen = nullptr;
    UI* _ui = nullptr;

    std::thread* _index_thread = nullptr;
    std::atomic<bool> _indexing{false};

    std::string _current_folder;
    std::vector<FileRecord> _current_files;
    std::vector<HDURecord> _current_hdus;
    std::vector<SearchResult> _search_results;

    bool _wants_close = false;
    std::string _status;
};
