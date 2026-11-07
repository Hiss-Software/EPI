# C++ EPI

**Easy Package Installer** — a minimalist package manager for C/C++ with a single binary, a plain-text registry, and no external dependencies beyond libcurl and zlib.

![logo](assets/logo.jpeg)

---

## What it is

`epi` is a small CLI tool that:

- reads a **plain-text registry** of packages (`name|version|archive|include|lib|license|sha256|depends`);
- **downloads** archives (zip, tar, tar.gz, tar.xz, tar.bz2) with **resume** and retry support;
- **verifies SHA-256** (if present in the registry);
- **unpacks** the package into `third_party/`;
- **resolves dependencies** (`depends` is a comma-separated list, `name:>=1.0` allowed);
- **imports** metadata from awesome-cpp, xmake-repo, Conan Center, and Hunter;
- can **add and remove packages** in the registry itself.

The registry is a plain text file you can keep in git and review like code.

---

## Features

| Command | What it does |
|---|---|
| `epi list` / `epi ls` | List all packages (installed ones are marked `*`) |
| `epi search <query> [--license]` | Search by name (and by license) |
| `epi info <name>` | Show package details and install status |
| `epi install [name]` | Install a package (no name → interactive picker) |
| `epi update <name> [--version V] [--archive U]` | Update the registry entry and reinstall |
| `epi remove <name> [--purge]` | Remove from the registry (`--purge` also deletes files) |
| `epi add <url> [--name N --version V --archive U ...]` | Add a package to the registry |
| `epi checksum <name>` / `epi checksum --all` | Compute and store SHA-256 |
| `epi path on` / `epi path off` | Add/remove the `epi` folder to/from the user `PATH` |
| `epi fetch <url> [-o file] [--merge]` | Download/parse a registry from a source |
| `epi sync <source>` | Merge packages from a source into the current registry |
| `epi version` / `epi help` | Version / help |

Global flags: `--quiet` / `-q`, `--verbose` / `-v`, `--no-color`.

---

## Registry format

The file `registry.epi` lives next to the binary. Comment lines start with `#`. The separator is `|`.

```
# name|version|archive|include_dir|lib|license|sha256|depends
zlib|v1.3.2|https://github.com/madler/zlib/archive/refs/tags/v1.3.2.tar.gz||||b99a0b86...|
fmt|12.2.0|https://github.com/fmtlib/fmt/releases/download/12.2.0/fmt-12.2.0.zip||||a2f4a8d5...|
nlohmann_json|3.12.0|https://github.com/nlohmann/json/archive/v3.12.0.tar.gz||||4b92eb0c...|
oatpp|1.3.0.latest|https://github.com/oatpp/oatpp/archive/1.3.0-latest.tar.gz||||adc3b880...|
```

Fields:

- **name** — unique package name;
- **version** — arbitrary string (`v1.2.3`, `cci.20240101`, `master`);
- **archive** — direct URL to the archive (zip / tar / tar.gz / tar.xz / tar.bz2);
- **include_dir** — subdirectory with headers inside the archive (optional);
- **lib** — space-separated libraries to link (empty = header-only);
- **license** — SPDX identifier or arbitrary string;
- **sha256** — hex string, case-insensitive (empty = skip verification, with a warning);
- **depends** — `dep1,dep2:>=1.0` (empty = no dependencies).

---

## Installation

### Prebuilt binary

Download the archive for your OS from the releases page, unpack it, and put `epi` on your `PATH`:

```bash
epi path on      # adds the epi directory to the user PATH
```

On Windows this edits `HKCU\Environment\Path`; on POSIX it appends `export PATH=...` to `~/.profile` (or `~/.bashrc`). `epi path off` removes the entry again.

### From source

Requirements: **C++20** compiler (GCC ≥ 10, Clang ≥ 12, MSVC ≥ 19.29), **CMake ≥ 3.24**, `libcurl`, `zlib`, `liblzma`, `libbz2`, `miniz`.

```bash
git clone <repo-url> epi && cd epi
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/epi version
```

On MSYS2/mingw64:

```bash
pacman -S mingw-w64-x86_64-{gcc,cmake,curl,zlib,xz,bzip2,miniz}
```

---

## Quick start

```bash
# 1. See what's available
epi list
epi search json

# 2. Install a package
epi install nlohmann_json

# 3. Check where it went
epi info nlohmann_json
ls third_party/

# 4. Add your own package by archive URL
epi add https://example.com/libfoo-1.0.tar.gz \
    --name libfoo --version 1.0 \
    --archive https://example.com/libfoo-1.0.tar.gz \
    --license MIT

# 5. Compute SHA-256 and store it in the registry
epi checksum libfoo
```

Use the package in `CMakeLists.txt`:

```cmake
include_directories(${CMAKE_SOURCE_DIR}/third_party)
target_link_libraries(app PRIVATE foo)
```

For header-only packages, `include_directories` on `third_party/` is enough.

---

## Importing from other sources

`epi` can parse four popular sources and merge them into its registry:

```bash
# awesome-cpp (markdown)
epi fetch https://raw.githubusercontent.com/fffaraz/awesome-cpp/master/README.md \
    -o awesome.epi --merge

# xmake-repo (packages/*/*/xmake.lua tree)
epi sync https://github.com/xmake-io/xmake-repo

# Conan Center (recipes/*/all/conandata.yml)
epi sync https://github.com/conan-io/conan-center-index

# Hunter (cmake/configs/default.cmake)
epi sync https://github.com/ruslo/hunter
```

> `sync` writes directly to `registry.epi`; `fetch` saves to a separate file so you can review and clean it up first.

During merge, duplicates by name are **not overwritten** — the existing entry wins.

---

## Architecture

```
download.cpp   — libcurl: resume, progress bar, retries
archive.cpp    — magic-based format detection + extraction (zip/tar/tar.gz/tar.xz/tar.bz2)
tar.cpp        — tar parser + gunzip
sha256.cpp     — self-contained SHA-256 (no OpenSSL)
registry.cpp   — load/save/suggest (Levenshtein)
convert.cpp    — parsers for awesome-cpp / xmake-repo / Conan / Hunter
commands.cpp   — all CLI commands, install, dependencies, purge
path_mgmt.cpp  — PATH on/off (Windows registry / POSIX rc files)
color.cpp      — ANSI + Win32 VT, global verbosity level
util.cpp       — exePath/exeDir, humanSize, fileSizeOf, Levenshtein
main.cpp       — argv parsing, dispatch
```

Install flow:

```
cmdInstall(name)
  → for dep in depends: cmdInstall(dep)        (recursive, cycle-guarded)
  → downloadFile(archive, part)                (5 attempts, resume, Range)
  → sha256File(zip) == p.sha256                (if provided)
  → extractArchive(zip, third_party/)          (magic → zip/tar/gz/xz/bz2)
  → write third_party/<name>.epi-manifest      (file list for --purge)
```

---

## Security

- **Archives are extracted only inside `third_party/`**. Entries with `..`, absolute paths, and symlinks/hardlinks/device nodes are skipped.
- **`epi remove --purge`** deletes only paths listed in the manifest and verifies that `weakly_canonical(target)` stays inside `INSTALL_DIR`.
- **SHA-256** is verified after download; on mismatch the file is deleted and installation aborts.
- If a package has no `sha256`, a warning is printed but installation continues (for compatibility with "raw" registries).
- `epi add` does **not** run any build scripts — it only downloads and unpacks. The registry is data, not code.

---

## Limitations

- No package building — only downloading and unpacking prebuilt archives. Projects that need `cmake --build` must be built by you.
- No semver resolver: `depends` is "present/absent" only; versions in dependencies are ignored (`name:>=1.0` is parsed but not checked).
- `tar.xz` and `tar.bz2` require `liblzma` / `libbz2`; if they are missing at build time, the format is simply unsupported.
- No parallel downloads — packages are fetched sequentially.
- No transitive dependency removal: `remove` only removes the package itself.

---

## License

This project is distributed under the **MIT** license. See [LICENSE](LICENSE) for details.