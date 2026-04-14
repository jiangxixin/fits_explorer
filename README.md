# FITS Explorer

A lightweight FITS file browser with SQLite indexing and ImGui UI.

## Features

- **Recursive folder scanning** for `.fits`, `.fit`, `.fts` files
- **Full HDU header extraction** via cfitsio
- **Thumbnail generation** with bilinear downsampling + histogram stretch
- **SQLite WAL mode** for fast concurrent reads
- **Background threading** for non-blocking indexing
- **Grid view** with adjustable thumbnail size

## Architecture

```
fits_explorer/
├── src/
│   ├── database/      # SQLite schema: files, hdus, thumbnails
│   ├── indexer/        # Recursive FITS scanner using cfitsio
│   ├── thumbnail/      # RGBA thumbnail generation
│   ├── ui/            # ImGui-based interface
│   └── app.cpp        # Main application + callbacks
└── CMakeLists.txt
```

## Build

### Dependencies

```bash
brew install cfitsio glfw sqlite3
```

ImGui and ImGuiFileDialog are fetched via CMake FetchContent.

### Compile

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
./fits_explorer [database_path]
```

Default database: `~/.fits_explorer.db`

## Usage

1. Click **📂 Open Folder** or press `Ctrl+O`
2. Wait for background indexing to complete
3. Click files to view HDU details in the right panel
4. Use the search bar to filter by filename

## Controls

- `Ctrl+O` — Open folder
- `Ctrl+F` — Toggle search
- `Ctrl+R` — Reindex all files
