#include "indexer.h"
#include "../database/database.h"
#include <fitsio.h>
#include <sys/stat.h>
#include <dirent.h>
#include <cstring>
#include <strings.h>
#include <algorithm>

static int64_t get_file_mtime(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) == 0)
        return (int64_t)st.st_mtime;
    return 0;
}

static int64_t get_file_size(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) == 0)
        return (int64_t)st.st_size;
    return 0;
}

static bool is_fits_file(const std::string& path) {
    const char* ext = strrchr(path.c_str(), '.');
    if (!ext) return false;
    return strcasecmp(ext, ".fits") == 0 ||
           strcasecmp(ext, ".fit") == 0 ||
           strcasecmp(ext, ".fts") == 0;
}

static std::string filename_from_path(const std::string& path) {
    size_t pos = path.find_last_of("/\\");
    if (pos == std::string::npos) return path;
    return path.substr(pos + 1);
}

static std::string get_hdu_type_string(int hdu_type) {
    switch (hdu_type) {
        case IMAGE_HDU:     return "IMAGE_HDU";
        case BINARY_TBL:   return "BINARY_TBL";
        case ASCII_TBL:    return "ASCII_TBL";
        case UNKNOWN_HDU:  return "UNKNOWN";
        default:           return "UNKNOWN";
    }
}

std::vector<IndexedFile> Indexer::scan_directory(const std::string& dir_path) {
    std::vector<IndexedFile> out;
    std::vector<std::string> dirs{dir_path};

    while (!dirs.empty()) {
        std::string current = dirs.back();
        dirs.pop_back();

        DIR* dp = opendir(current.c_str());
        if (!dp) continue;

        struct dirent* entry;
        while ((entry = readdir(dp)) != nullptr) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                continue;

            std::string full_path = current + "/" + entry->d_name;

            if (entry->d_type == DT_DIR) {
                dirs.push_back(full_path);
            } else if (entry->d_type == DT_REG && is_fits_file(full_path)) {
                IndexedFile f;
                f.path = full_path;
                f.filename = filename_from_path(full_path);
                f.size = get_file_size(full_path);
                f.modified_time = get_file_mtime(full_path);
                out.push_back(f);
            }
        }
        closedir(dp);
    }

    std::sort(out.begin(), out.end(),
              [](const IndexedFile& a, const IndexedFile& b) {
                  return a.filename < b.filename;
              });

    return out;
}

int Indexer::index_file(const std::string& filepath, void* db_ptr) {
    Database* db = static_cast<Database*>(db_ptr);
    int status = 0;
    fitsfile* fptr = nullptr;

    if (fits_open_file(&fptr, filepath.c_str(), READONLY, &status)) {
        _last_error = status;
        fits_get_errstatus(status, _error_msg);
        return 0;
    }

    int hdu_count = 0;
    int num_hdus;
    fits_get_num_hdus(fptr, &num_hdus, &status);

    for (int i = 1; i <= num_hdus; i++) {
        fits_movabs_hdu(fptr, i, NULL, &status);
        if (status) break;

        int hdu_type;
        fits_get_hdu_type(fptr, &hdu_type, &status);

        int bitpix, naxis;
        long naxes[3] = {0, 0, 0};
        fits_get_img_param(fptr, 3, &bitpix, &naxis, naxes, &status);

        HDURecord hdu;
        hdu.file_id = 0;  // Will be set after file insert
        hdu.hdu_index = i - 1;
        hdu.hdu_type = get_hdu_type_string(hdu_type);
        hdu.bitpix = bitpix;
        hdu.naxis = naxis;
        hdu.naxis1 = (naxis >= 1) ? (int)naxes[0] : 0;
        hdu.naxis2 = (naxis >= 2) ? (int)naxes[1] : 0;
        hdu.naxis3 = (naxis >= 3) ? (int)naxes[2] : 0;
        hdu.modified_time = get_file_mtime(filepath);

        int64_t existing_file_id = 0;
        auto existing = db->get_file_by_path(filepath);
        if (existing.has_value()) {
            existing_file_id = existing->id;
        } else {
            FileRecord file_rec;
            file_rec.path = filepath;
            file_rec.filename = filename_from_path(filepath);
            file_rec.size = get_file_size(filepath);
            file_rec.modified_time = get_file_mtime(filepath);
            file_rec.hdu_count = num_hdus;
            existing_file_id = db->insert_file(file_rec);
        }

        hdu.file_id = existing_file_id;
        db->insert_hdu(hdu);
        hdu_count++;
    }

    // Update file record with HDU count
    if (hdu_count > 0) {
        auto file_rec = db->get_file_by_path(filepath);
        if (file_rec.has_value()) {
            file_rec->hdu_count = hdu_count;
            db->update_file(file_rec->id, *file_rec);
        }
    }

    fits_close_file(fptr, &status);
    return hdu_count;
}

void Indexer::index_batch(const std::vector<IndexedFile>& files,
                          void* db,
                          ProgressCallback progress) {
    int total = (int)files.size();
    for (int i = 0; i < total; i++) {
        index_file(files[i].path, db);
        if (progress) progress(files[i].path, i + 1, total);
    }
}

bool Indexer::needs_reindex(const std::string& filepath, int64_t db_modified_time) {
    int64_t file_mtime = get_file_mtime(filepath);
    return file_mtime != db_modified_time;
}
