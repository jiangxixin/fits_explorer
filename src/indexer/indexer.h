#pragma once

#include <string>
#include <vector>
#include <functional>
#include <cstdint>

struct IndexedFile {
    std::string path;
    std::string filename;
    int64_t size;
    int64_t modified_time;
    int hdu_count;
};

class Indexer {
public:
    using ProgressCallback = std::function<void(const std::string& path, int current, int total)>;

    Indexer() = default;

    // Scan a directory recursively for .fits and .fit files
    std::vector<IndexedFile> scan_directory(const std::string& dir_path);

    // Index a single FITS file, returns number of HDUs indexed
    int index_file(const std::string& filepath, void* db /* Database* */);

    // Batch index with progress callback
    void index_batch(const std::vector<IndexedFile>& files,
                     void* db,
                     ProgressCallback progress);

    // Check if file needs re-indexing (by modified time)
    bool needs_reindex(const std::string& filepath, int64_t db_modified_time);

    int get_last_error() const { return _last_error; }
    const char* get_last_error_msg() const { return _error_msg; }

private:
    int _last_error = 0;
    char _error_msg[256] = {0};
};
