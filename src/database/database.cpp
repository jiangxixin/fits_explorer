#include "database.h"
#include <sqlite3.h>
#include <cstdio>
#include <cstring>

struct Deleter { void operator()(sqlite3* p) { sqlite3_close(p); } };

using DBPtr = std::unique_ptr<sqlite3, Deleter>;

Database::Database() = default;

Database::~Database() { close(); }

bool Database::open(const std::string& path) {
    if (_db) close();
    sqlite3* db = nullptr;
    int rc = sqlite3_open(path.c_str(), &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database '%s': %s\n", path.c_str(), sqlite3_errmsg(db));
        if (db) sqlite3_close(db);
        return false;
    }
    _db = db;

    // Enable WAL mode for better concurrent read performance
    exec("PRAGMA journal_mode=WAL");
    exec("PRAGMA synchronous=NORMAL");
    exec("PRAGMA cache_size=10000");

    // Create tables
    exec(R"(
        CREATE TABLE IF NOT EXISTS files (
            id              INTEGER PRIMARY KEY AUTOINCREMENT,
            path            TEXT NOT NULL UNIQUE,
            filename        TEXT NOT NULL,
            size            INTEGER NOT NULL,
            modified_time   INTEGER NOT NULL,
            hdu_count       INTEGER DEFAULT 0,
            indexed_time    INTEGER DEFAULT 0
        );
        CREATE INDEX IF NOT EXISTS idx_files_path ON files(path);
        CREATE INDEX IF NOT EXISTS idx_files_filename ON files(filename);

        CREATE TABLE IF NOT EXISTS hdus (
            id              INTEGER PRIMARY KEY AUTOINCREMENT,
            file_id         INTEGER NOT NULL,
            hdu_index       INTEGER NOT NULL,
            hdu_type        TEXT,
            bitpix          INTEGER,
            naxis           INTEGER,
            naxis1          INTEGER,
            naxis2          INTEGER,
            naxis3          INTEGER,
            modified_time   INTEGER,
            FOREIGN KEY (file_id) REFERENCES files(id) ON DELETE CASCADE,
            UNIQUE(file_id, hdu_index)
        );
        CREATE INDEX IF NOT EXISTS idx_hdus_file ON hdus(file_id);

        CREATE TABLE IF NOT EXISTS thumbnails (
            id              INTEGER PRIMARY KEY AUTOINCREMENT,
            file_id         INTEGER NOT NULL,
            hdu_index       INTEGER NOT NULL,
            width           INTEGER NOT NULL,
            height          INTEGER NOT NULL,
            data            BLOB NOT NULL,
            created_time    INTEGER NOT NULL,
            FOREIGN KEY (file_id, hdu_index) REFERENCES hdus(file_id, hdu_index) ON DELETE CASCADE,
            UNIQUE(file_id, hdu_index)
        );
        CREATE INDEX IF NOT EXISTS idx_thumb_file ON thumbnails(file_id);
    )");

    return true;
}

void Database::close() {
    if (_db) {
        sqlite3_close((sqlite3*)_db);
        _db = nullptr;
    }
}

void Database::exec(const std::string& sql) {
    char* err = nullptr;
    int rc = sqlite3_exec((sqlite3*)_db, sql.c_str(), nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n  SQL: %s\n", err, sql.c_str());
        sqlite3_free(err);
    }
}

// ---- Files ----

int64_t Database::insert_file(const FileRecord& f) {
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2((sqlite3*)_db,
        "INSERT OR REPLACE INTO files (path,filename,size,modified_time,hdu_count) VALUES (?,?,?,?,?)",
        -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, f.path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, f.filename.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, f.size);
    sqlite3_bind_int64(stmt, 4, f.modified_time);
    sqlite3_bind_int(stmt, 5, f.hdu_count);
    sqlite3_step(stmt);
    int64_t id = sqlite3_last_insert_rowid((sqlite3*)_db);
    sqlite3_finalize(stmt);
    return id;
}

bool Database::update_file(int64_t id, const FileRecord& f) {
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2((sqlite3*)_db,
        "UPDATE files SET filename=?,size=?,modified_time=?,hdu_count=? WHERE id=?",
        -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, f.filename.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, f.size);
    sqlite3_bind_int64(stmt, 3, f.modified_time);
    sqlite3_bind_int(stmt, 4, f.hdu_count);
    sqlite3_bind_int64(stmt, 5, id);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool Database::delete_file(int64_t id) {
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2((sqlite3*)_db, "DELETE FROM files WHERE id=?", -1, &stmt, nullptr);
    sqlite3_bind_int64(stmt, 1, id);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

std::optional<FileRecord> Database::get_file_by_path(const std::string& path) {
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2((sqlite3*)_db,
        "SELECT id,path,filename,size,modified_time,hdu_count FROM files WHERE path=?",
        -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        FileRecord r;
        r.id = sqlite3_column_int64(stmt, 0);
        r.path = (const char*)sqlite3_column_text(stmt, 1);
        r.filename = (const char*)sqlite3_column_text(stmt, 2);
        r.size = sqlite3_column_int64(stmt, 3);
        r.modified_time = sqlite3_column_int64(stmt, 4);
        r.hdu_count = sqlite3_column_int(stmt, 5);
        sqlite3_finalize(stmt);
        return r;
    }
    sqlite3_finalize(stmt);
    return {};
}

std::vector<FileRecord> Database::get_all_files() {
    std::vector<FileRecord> out;
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2((sqlite3*)_db,
        "SELECT id,path,filename,size,modified_time,hdu_count FROM files ORDER BY filename",
        -1, &stmt, nullptr);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        FileRecord r;
        r.id = sqlite3_column_int64(stmt, 0);
        r.path = (const char*)sqlite3_column_text(stmt, 1);
        r.filename = (const char*)sqlite3_column_text(stmt, 2);
        r.size = sqlite3_column_int64(stmt, 3);
        r.modified_time = sqlite3_column_int64(stmt, 4);
        r.hdu_count = sqlite3_column_int(stmt, 5);
        out.push_back(r);
    }
    sqlite3_finalize(stmt);
    return out;
}

std::vector<FileRecord> Database::search_files(const std::string& query) {
    std::vector<FileRecord> out;
    sqlite3_stmt* stmt = nullptr;
    std::string sql = "SELECT id,path,filename,size,modified_time,hdu_count FROM files WHERE filename LIKE ? ORDER BY filename";
    sqlite3_prepare_v2((sqlite3*)_db, sql.c_str(), -1, &stmt, nullptr);
    std::string pat = "%" + query + "%";
    sqlite3_bind_text(stmt, 1, pat.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        FileRecord r;
        r.id = sqlite3_column_int64(stmt, 0);
        r.path = (const char*)sqlite3_column_text(stmt, 1);
        r.filename = (const char*)sqlite3_column_text(stmt, 2);
        r.size = sqlite3_column_int64(stmt, 3);
        r.modified_time = sqlite3_column_int64(stmt, 4);
        r.hdu_count = sqlite3_column_int(stmt, 5);
        out.push_back(r);
    }
    sqlite3_finalize(stmt);
    return out;
}

// ---- HDUs ----

int64_t Database::insert_hdu(const HDURecord& h) {
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2((sqlite3*)_db,
        "INSERT OR REPLACE INTO hdus (file_id,hdu_index,hdu_type,bitpix,naxis,naxis1,naxis2,naxis3,modified_time) VALUES (?,?,?,?,?,?,?,?,?)",
        -1, &stmt, nullptr);
    sqlite3_bind_int64(stmt, 1, h.file_id);
    sqlite3_bind_int(stmt, 2, h.hdu_index);
    sqlite3_bind_text(stmt, 3, h.hdu_type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, h.bitpix);
    sqlite3_bind_int(stmt, 5, h.naxis);
    sqlite3_bind_int(stmt, 6, h.naxis1);
    sqlite3_bind_int(stmt, 7, h.naxis2);
    sqlite3_bind_int(stmt, 8, h.naxis3);
    sqlite3_bind_int64(stmt, 9, h.modified_time);
    sqlite3_step(stmt);
    int64_t id = sqlite3_last_insert_rowid((sqlite3*)_db);
    sqlite3_finalize(stmt);
    return id;
}

bool Database::delete_hdus_for_file(int64_t file_id) {
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2((sqlite3*)_db, "DELETE FROM hdus WHERE file_id=?", -1, &stmt, nullptr);
    sqlite3_bind_int64(stmt, 1, file_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return true;
}

std::vector<HDURecord> Database::get_hdus_for_file(int64_t file_id) {
    std::vector<HDURecord> out;
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2((sqlite3*)_db,
        "SELECT id,file_id,hdu_index,hdu_type,bitpix,naxis,naxis1,naxis2,naxis3,modified_time FROM hdus WHERE file_id=? ORDER BY hdu_index",
        -1, &stmt, nullptr);
    sqlite3_bind_int64(stmt, 1, file_id);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        HDURecord r;
        r.id = sqlite3_column_int64(stmt, 0);
        r.file_id = sqlite3_column_int64(stmt, 1);
        r.hdu_index = sqlite3_column_int(stmt, 2);
        const char* t = (const char*)sqlite3_column_text(stmt, 3);
        r.hdu_type = t ? t : "";
        r.bitpix = sqlite3_column_int(stmt, 4);
        r.naxis = sqlite3_column_int(stmt, 5);
        r.naxis1 = sqlite3_column_int(stmt, 6);
        r.naxis2 = sqlite3_column_int(stmt, 7);
        r.naxis3 = sqlite3_column_int(stmt, 8);
        r.modified_time = sqlite3_column_int64(stmt, 9);
        out.push_back(r);
    }
    sqlite3_finalize(stmt);
    return out;
}

std::optional<HDURecord> Database::get_hdu(int64_t file_id, int hdu_index) {
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2((sqlite3*)_db,
        "SELECT id,file_id,hdu_index,hdu_type,bitpix,naxis,naxis1,naxis2,naxis3,modified_time FROM hdus WHERE file_id=? AND hdu_index=?",
        -1, &stmt, nullptr);
    sqlite3_bind_int64(stmt, 1, file_id);
    sqlite3_bind_int(stmt, 2, hdu_index);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        HDURecord r;
        r.id = sqlite3_column_int64(stmt, 0);
        r.file_id = sqlite3_column_int64(stmt, 1);
        r.hdu_index = sqlite3_column_int(stmt, 2);
        const char* t = (const char*)sqlite3_column_text(stmt, 3);
        r.hdu_type = t ? t : "";
        r.bitpix = sqlite3_column_int(stmt, 4);
        r.naxis = sqlite3_column_int(stmt, 5);
        r.naxis1 = sqlite3_column_int(stmt, 6);
        r.naxis2 = sqlite3_column_int(stmt, 7);
        r.naxis3 = sqlite3_column_int(stmt, 8);
        r.modified_time = sqlite3_column_int64(stmt, 9);
        sqlite3_finalize(stmt);
        return r;
    }
    sqlite3_finalize(stmt);
    return {};
}

// ---- Thumbnails ----

int64_t Database::insert_thumbnail(const ThumbnailRecord& t) {
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2((sqlite3*)_db,
        "INSERT OR REPLACE INTO thumbnails (file_id,hdu_index,width,height,data,created_time) VALUES (?,?,?,?,?,?)",
        -1, &stmt, nullptr);
    sqlite3_bind_int64(stmt, 1, t.file_id);
    sqlite3_bind_int(stmt, 2, t.hdu_index);
    sqlite3_bind_int(stmt, 3, t.width);
    sqlite3_bind_int(stmt, 4, t.height);
    sqlite3_bind_blob(stmt, 5, t.data.data(), (int)t.data.size(), SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 6, t.created_time);
    sqlite3_step(stmt);
    int64_t id = sqlite3_last_insert_rowid((sqlite3*)_db);
    sqlite3_finalize(stmt);
    return id;
}

std::optional<ThumbnailRecord> Database::get_thumbnail(int64_t file_id, int hdu_index) {
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2((sqlite3*)_db,
        "SELECT id,file_id,hdu_index,width,height,data,created_time FROM thumbnails WHERE file_id=? AND hdu_index=?",
        -1, &stmt, nullptr);
    sqlite3_bind_int64(stmt, 1, file_id);
    sqlite3_bind_int(stmt, 2, hdu_index);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        ThumbnailRecord r;
        r.id = sqlite3_column_int64(stmt, 0);
        r.file_id = sqlite3_column_int64(stmt, 1);
        r.hdu_index = sqlite3_column_int(stmt, 2);
        r.width = sqlite3_column_int(stmt, 3);
        r.height = sqlite3_column_int(stmt, 4);
        const void* blob = sqlite3_column_blob(stmt, 5);
        int nbytes = sqlite3_column_bytes(stmt, 5);
        r.data.assign((const uint8_t*)blob, (const uint8_t*)blob + nbytes);
        r.created_time = sqlite3_column_int64(stmt, 6);
        sqlite3_finalize(stmt);
        return r;
    }
    sqlite3_finalize(stmt);
    return {};
}

bool Database::has_thumbnail(int64_t file_id, int hdu_index) {
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2((sqlite3*)_db,
        "SELECT 1 FROM thumbnails WHERE file_id=? AND hdu_index=?",
        -1, &stmt, nullptr);
    sqlite3_bind_int64(stmt, 1, file_id);
    sqlite3_bind_int(stmt, 2, hdu_index);
    bool found = sqlite3_step(stmt) == SQLITE_ROW;
    sqlite3_finalize(stmt);
    return found;
}

// ---- Search ----

std::vector<SearchResult> Database::search_headers(const std::string& keyword,
                                                   const std::string& value) {
    std::vector<SearchResult> out;
    (void)keyword; (void)value;
    // Placeholder: full header search would require storing header text in DB
    // For now, search through HDU metadata
    return out;
}

int64_t Database::get_file_count() {
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2((sqlite3*)_db, "SELECT COUNT(*) FROM files", -1, &stmt, nullptr);
    int64_t count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) count = sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
    return count;
}

int64_t Database::get_total_thumbnail_count() {
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2((sqlite3*)_db, "SELECT COUNT(*) FROM thumbnails", -1, &stmt, nullptr);
    int64_t count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) count = sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
    return count;
}

void Database::vacuum() {
    exec("VACUUM");
}
