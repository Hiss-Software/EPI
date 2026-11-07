#include "tar.h"
#include "util.h"
#include <cstdint>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <vector>
#include <zlib.h>

namespace {

struct Header {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char chksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char pad[12];
};

long long parseOctal(const char* s, size_t n) {
    long long v = 0;
    size_t i = 0;
    while (i < n && (s[i] == ' ' || s[i] == '\0')) ++i;
    while (i < n && s[i] >= '0' && s[i] <= '7') {
        v = v * 8 + (s[i] - '0');
        ++i;
    }
    return v;
}

bool isZeroBlock(const uint8_t* b) {
    for (int i = 0; i < 512; ++i) if (b[i]) return false;
    return true;
}

bool safeJoin(const std::string& base,
              const std::string& name,
              std::filesystem::path& out) {
    std::filesystem::path rel = name;
    std::filesystem::path safe;
    for (auto& part : rel) {
        if (part == ".." || part == "/" || part == "\\") continue;
        safe /= part;
    }
    if (safe.empty()) return false;
    out = std::filesystem::path(base) / safe;
    return true;
}

} // namespace

bool extractTar(const std::string& archivePath,
                const std::string& destDir,
                std::vector<std::string>& files,
                std::string& err) {
    files.clear();
    std::ifstream in(archivePath, std::ios::binary);
    if (!in) { err = "cannot open archive: " + archivePath; return false; }

    // Detect gzip
    uint8_t magic[2] = {0,0};
    in.read(reinterpret_cast<char*>(magic), 2);
    in.clear(); in.seekg(0);
    bool isGz = (magic[0] == 0x1f && magic[1] == 0x8b);

    std::error_code ec;
    std::filesystem::create_directories(destDir, ec);

    // Read whole file into memory (archives are small, ~few MB)
    std::vector<uint8_t> data;
    {
        in.seekg(0, std::ios::end);
        std::streamoff sz = in.tellg();
        in.seekg(0, std::ios::beg);
        if (sz <= 0) { err = "empty archive"; return false; }
        data.resize(static_cast<size_t>(sz));
        in.read(reinterpret_cast<char*>(data.data()), sz);
        if (!in) { err = "read failed"; return false; }
    }

    std::vector<uint8_t> raw;
    if (isGz) {
        // Inflate with zlib
        z_stream zs{};
        if (inflateInit2(&zs, 15 + 16) != Z_OK) { err = "zlib init failed"; return false; }
        zs.next_in = data.data();
        zs.avail_in = static_cast<uInt>(data.size());
        std::vector<uint8_t> buf(1 << 16);
        int ret;
        do {
            zs.next_out = buf.data();
            zs.avail_out = static_cast<uInt>(buf.size());
            ret = inflate(&zs, Z_NO_FLUSH);
            if (ret == Z_STREAM_ERROR || ret == Z_DATA_ERROR || ret == Z_MEM_ERROR) {
                inflateEnd(&zs);
                err = "gzip decompress failed";
                return false;
            }
            size_t produced = buf.size() - zs.avail_out;
            raw.insert(raw.end(), buf.begin(), buf.begin() + produced);
        } while (ret != Z_STREAM_END);
        inflateEnd(&zs);
    } else {
        raw = std::move(data);
    }

    size_t off = 0;
    while (off + 512 <= raw.size()) {
        if (isZeroBlock(raw.data() + off)) break;
        Header h;
        std::memcpy(&h, raw.data() + off, sizeof(Header));
        off += 512;

        if (std::memcmp(h.magic, "ustar", 5) != 0) {
            err = "not a tar archive (bad magic)";
            return false;
        }

        std::string name(h.name, strnlen(h.name, sizeof(h.name)));
        std::string prefix(h.prefix, strnlen(h.prefix, sizeof(h.prefix)));
        if (!prefix.empty()) name = prefix + "/" + name;

        long long size = parseOctal(h.size, sizeof(h.size));
        char type = h.typeflag ? h.typeflag : '0';

        std::filesystem::path target;
        if (!safeJoin(destDir, name, target)) {
            off += static_cast<size_t>((size + 511) & ~511LL);
            continue;
        }

        if (type == '5' || name.back() == '/') {
            std::filesystem::create_directories(target, ec);
        } else if (type == '0' || type == '\0') {
            std::filesystem::create_directories(target.parent_path(), ec);
            std::ofstream out(target, std::ios::binary | std::ios::trunc);
            if (!out) { err = "cannot write " + target.string(); return false; }
            if (size > 0 && off + static_cast<size_t>(size) <= raw.size()) {
                out.write(reinterpret_cast<const char*>(raw.data() + off), size);
            }
            files.push_back(name);
        }
        // symlinks/hardlinks/devices skipped for safety

        off += static_cast<size_t>((size + 511) & ~511LL);
    }
    return true;
}
