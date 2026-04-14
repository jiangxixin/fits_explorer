#include "app.h"
#include "database/database.h"
#include "indexer/indexer.h"
#include "thumbnail/thumbnail.h"
#include "ui/ui.h"
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <sys/stat.h>
#include <chrono>
#include <algorithm>

static std::string getenv_default(const char* key, const char* def) {
    const char* v = getenv(key);
    return v ? std::string(v) : std::string(def);
}

App::App() = default;
App::~App() {
    if (_index_thread && _index_thread->joinable())
        _index_thread->join();
    delete _thumb_gen;
    delete _indexer;
    delete _db;
    delete _ui;
}

bool App::init(const std::string& db_path) {
    _db = new Database();
    if (!_db->open(db_path)) {
        fprintf(stderr, "Failed to open database: %s\n", db_path.c_str());
        return false;
    }

    _indexer = new Indexer();
    _thumb_gen = new ThumbnailGenerator();
    _ui = new UI();

    // Setup callbacks
    _ui->on_scan_folder = [this](const std::string& dir) {
        open_folder(dir);
    };
    _ui->on_reindex = [this]() {
        reindex_all();
    };
    _ui->on_search = [this](const std::string& q) {
        search(q);
    };
    _ui->on_select_hdu = [this](int64_t file_id, int hdu_index) {
        select_hdu(file_id, hdu_index);
    };

    _ui->init();

    update_status();
    return true;
}

void App::open_folder(const std::string& path) {
    _current_folder = path;
    _current_files = _db->get_all_files();
    _ui->set_files(_current_files);
    update_status();
}

void App::reindex_all() {
    if (_indexing.load()) return;
    if (_current_folder.empty()) return;

    auto files = _indexer->scan_directory(_current_folder);
    if (files.empty()) return;

    if (_index_thread && _index_thread->joinable())
        _index_thread->join();

    _indexing = true;
    _index_thread = new std::thread([this, files]() {
        int total = (int)files.size();
        int cur = 0;
        for (const auto& f : files) {
            _indexer->index_file(f.path, _db);

            // Check if file needs thumbnail generation
            auto file_rec = _db->get_file_by_path(f.path);
            if (file_rec.has_value()) {
                auto hdus = _db->get_hdus_for_file(file_rec->id);
                for (const auto& hdu : hdus) {
                    if (hdu.naxis >= 2 && !_db->has_thumbnail(file_rec->id, hdu.hdu_index)) {
                        auto thumb = _thumb_gen->generate(f.path, hdu.hdu_index, 128);
                        if (thumb.has_value()) {
                            ThumbnailRecord tr;
                            tr.file_id = file_rec->id;
                            tr.hdu_index = hdu.hdu_index;
                            tr.width = thumb->width;
                            tr.height = thumb->height;
                            tr.data = std::move(thumb->pixels);
                            tr.created_time = (int64_t)time(nullptr);
                            _db->insert_thumbnail(tr);
                        }
                    }
                }
            }

            cur++;
            _ui->set_progress(f.filename, cur, total, true);
        }

        _indexing = false;
        _ui->set_indexing(false);

        // Refresh file list
        _current_files = _db->get_all_files();
        _ui->set_files(_current_files);
        update_status();
    });
}

void App::select_file(int64_t file_id) {
    _current_hdus = _db->get_hdus_for_file(file_id);
    _ui->set_hdus(_current_hdus);
}

void App::select_hdu(int64_t file_id, int hdu_index) {
    auto file_rec = std::find_if(_current_files.begin(), _current_files.end(),
                                  [file_id](const FileRecord& f) { return f.id == file_id; });
    if (file_rec == _current_files.end()) return;

    _current_hdus = _db->get_hdus_for_file(file_id);
    _ui->set_hdus(_current_hdus);
    _ui->set_selected_file(&(*file_rec));
}

void App::search(const std::string& query) {
    if (query.empty()) {
        _current_files = _db->get_all_files();
    } else {
        _current_files = _db->search_files(query);
    }
    _ui->set_files(_current_files);
}

void App::update_status() {
    char buf[256];
    snprintf(buf, sizeof(buf), "Folder: %s",
              _current_folder.empty() ? "(none)" : _current_folder.c_str());
    _status = buf;
    _ui->set_status(_status,
                    _db->get_file_count(),
                    _db->get_total_thumbnail_count());
}

void App::render() {
    // Check for folder dialog
    if (_ui->wants_folder_dialog()) {
        // Folder dialog is handled in main.cpp via ImGuiFileDialog
    }

    _ui->render();
}
