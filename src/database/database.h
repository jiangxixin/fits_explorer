#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>

struct FileRecord {
    int64_t id;
    std::string path;
    std::string filename;
    int64_t size;
    int64_t modified_time;
    int hdu_count;
};

struct HDURecord {
    int64_t id;
    int64_t file_id;
    int hdu_index;
    std::string hdu_type;       // IMAGE_HDU, BINARY_TBL, ASCII_TBL, etc.
    int bitpix;
    int naxis;
    int naxis1, naxis2, naxis3;
    int64_t modified_time;
};

struct ThumbnailRecord {
    int64_t id;
    int64_t file_id;
    int hdu_index;
    int width;
    int height;
    std::vector<uint8_t> data;  // RGBA pixels
    int64_t created_time;
};

struct SearchResult {
    int64_t file_id;
    std::string filename;
    std::string path;
    int64_t size;
    int hdu_index;
    std::string match_keyword;
    std::string match_value;
};

class Database {
public:
    Database();
    ~Database();

    bool open(const std::string& path);
    void close();
    bool is_open() const { return _db != nullptr; }

    // Files
    int64_t insert_file(const FileRecord& file);
    bool update_file(int64_t id, const FileRecord& file);
    bool delete_file(int64_t id);
    std::optional<FileRecord> get_file_by_path(const std::string& path);
    std::vector<FileRecord> get_all_files();
    std::vector<FileRecord> search_files(const std::string& query);

    // HDUs
    int64_t insert_hdu(const HDURecord& hdu);
    bool delete_hdus_for_file(int64_t file_id);
    std::vector<HDURecord> get_hdus_for_file(int64_t file_id);
    std::optional<HDURecord> get_hdu(int64_t file_id, int hdu_index);

    // Thumbnails
    int64_t insert_thumbnail(const ThumbnailRecord& thumb);
    std::optional<ThumbnailRecord> get_thumbnail(int64_t file_id, int hdu_index);
    bool has_thumbnail(int64_t file_id, int hdu_index);

    // Search
    std::vector<SearchResult> search_headers(const std::string& keyword,
                                              const std::string& value);

    // Utility
    int64_t get_file_count();
    int64_t get_total_thumbnail_count();
    void vacuum();

private:
    void* _db = nullptr;  // sqlite3*
    void exec(const std::string& sql);
};
