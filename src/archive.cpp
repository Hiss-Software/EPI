#include "archive.h"
#include "tar.h"
#include <cstdint>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <vector>
#include <miniz.h>
#include <lzma.h>
#include <bzlib.h>

namespace {

bool extractZip(const std::string& archivePath,
                const std::string& destDir,
                std::vector<std::string>& files,
                std::string& err) {
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_file(&zip, archivePath.c_str(), 0)) {
        err = "cannot open zip: " + archivePath;
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(destDir, ec);

    mz_uint n = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < n; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, i, &st)) continue;
        if (st.m_is_directory) continue;

        std::filesystem::path rel = st.m_filename;
        std::filesystem::path safe;
        for (auto& part : rel) {
            if (part == ".." || part == "/" || part == "\\") continue;
            safe /= part;
        }
        if (safe.empty()) continue;

        std::filesystem::path target = std::filesystem::path(destDir) / safe;
        std::filesystem::create_directories(target.parent_path(), ec);

        size_t sz = 0;
        void* p = mz_zip_reader_extract_to_heap(&zip, i, &sz, 0);
        if (!p) {
            err = "extract failed: " + std::string(st.m_filename);
            mz_zip_reader_end(&zip);
            return false;
        }

        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        if (!out) {
            mz_free(p);
            err = "cannot write " + target.string();
            mz_zip_reader_end(&zip);
            return false;
        }
        out.write(static_cast<const char*>(p), static_cast<std::streamsize>(sz));
        mz_free(p);

        files.push_back(st.m_filename);
    }
    mz_zip_reader_end(&zip);
    return true;
}

static bool readAll(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    in.seekg(0, std::ios::end);
    std::streamoff sz = in.tellg();
    in.seekg(0, std::ios::beg);
    if (sz <= 0) return false;
    out.resize(static_cast<size_t>(sz));
    in.read(reinterpret_cast<char*>(out.data()), sz);
    return static_cast<bool>(in);
}

static bool decompressXz(const std::vector<uint8_t>& in,
                         std::vector<uint8_t>& out,
                         std::string& err) {
    std::cerr << "[debug] decompressXz: in.size()=" << in.size() << "\n";
    lzma_stream strm = LZMA_STREAM_INIT;
    if (lzma_stream_decoder(&strm, UINT64_MAX, LZMA_CONCATENATED) != LZMA_OK) {
        err = "lzma init failed";
        return false;
    }
    strm.next_in = in.data();
    strm.avail_in = in.size();

    std::vector<uint8_t> buf(1 << 16);
    lzma_ret ret;
    do {
        strm.next_out = buf.data();
        strm.avail_out = buf.size();
        ret = lzma_code(&strm, LZMA_FINISH);
        if (ret != LZMA_OK && ret != LZMA_STREAM_END) {
            lzma_end(&strm);
            err = "lzma decode failed";
            return false;
        }
        size_t produced = buf.size() - strm.avail_out;
        out.insert(out.end(), buf.begin(), buf.begin() + produced);
    } while (ret != LZMA_STREAM_END);

    lzma_end(&strm);
    std::cerr << "[debug] decompressXz: out.size()=" << out.size() << "\n";
    return true;
}

static bool decompressBz2(const std::vector<uint8_t>& in,
                          std::vector<uint8_t>& out,
                          std::string& err) {
    std::cerr << "[debug] decompressBz2: in.size()=" << in.size() << "\n";
    if (in.size() >= 3) {
        std::cerr << "[debug] decompressBz2: hdr="
                  << (int)in[0] << " " << (int)in[1] << " " << (int)in[2] << "\n";
    }

    bz_stream strm{};
    if (BZ2_bzDecompressInit(&strm, 0, 0) != BZ_OK) {
        err = "bz2 init failed";
        return false;
    }
    strm.next_in = const_cast<char*>(reinterpret_cast<const char*>(in.data()));
    strm.avail_in = static_cast<unsigned>(in.size());

    std::vector<char> buf(1 << 16);
    int ret;
    int iter = 0;
    do {
        strm.next_out = buf.data();
        strm.avail_out = static_cast<unsigned>(buf.size());
        ret = BZ2_bzDecompress(&strm);
        std::cerr << "[debug] BZ2_bzDecompress iter=" << iter
                  << " ret=" << ret
                  << " avail_in=" << strm.avail_in
                  << " avail_out=" << strm.avail_out << "\n";
        if (ret != BZ_OK && ret != BZ_STREAM_END) {
            BZ2_bzDecompressEnd(&strm);
            err = "bz2 decode failed, ret=" + std::to_string(ret);
            return false;
        }
        size_t produced = buf.size() - strm.avail_out;
        out.insert(out.end(), buf.begin(), buf.begin() + produced);
        ++iter;
        if (iter > 100000) {
            BZ2_bzDecompressEnd(&strm);
            err = "bz2 infinite loop";
            return false;
        }
    } while (ret != BZ_STREAM_END);

    BZ2_bzDecompressEnd(&strm);
    std::cerr << "[debug] decompressBz2: out.size()=" << out.size() << "\n";
    return true;
}

static bool decompressAndExtractTar(const std::string& archivePath,
                                    const std::string& destDir,
                                    std::vector<std::string>& files,
                                    std::string& err,
                                    bool isXz) {
    std::vector<uint8_t> raw;
    if (!readAll(archivePath, raw)) { err = "cannot read archive"; return false; }
    std::cerr << "[debug] decompressAndExtractTar: raw.size()=" << raw.size()
              << " isXz=" << isXz << "\n";

    std::vector<uint8_t> tar;
    bool ok = isXz ? decompressXz(raw, tar, err) : decompressBz2(raw, tar, err);
    if (!ok) return false;

    if (tar.size() >= 262) {
        std::cerr << "[debug] tar magic at 257: ";
        for (int i = 257; i < 262; ++i)
            std::cerr << (char)(tar[i] ? tar[i] : '.');
        std::cerr << "\n";
    } else {
        std::cerr << "[debug] tar too short: " << tar.size() << "\n";
    }

    std::filesystem::path tmp = std::filesystem::path(archivePath).string() + ".tar";
    {
        std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
        if (!o) { err = "cannot write temp tar"; return false; }
        o.write(reinterpret_cast<const char*>(tar.data()),
                static_cast<std::streamsize>(tar.size()));
        if (!o) { err = "write temp tar failed"; return false; }
    }

    std::string innerErr;
    bool extracted = extractTar(tmp.string(), destDir, files, innerErr);
    std::error_code ec;
    std::filesystem::remove(tmp, ec);
    if (!extracted) { err = innerErr; return false; }
    return true;
}

} // namespace

bool extractArchive(const std::string& archivePath,
                    const std::string& destDir,
                    std::vector<std::string>& files,
                    std::string& err) {
    files.clear();
    std::ifstream in(archivePath, std::ios::binary);
    if (!in) { err = "cannot open archive"; return false; }
    uint8_t hdr[6] = {0};
    in.read(reinterpret_cast<char*>(hdr), 6);
    if (!in) { err = "short archive"; return false; }

    std::cerr << "[debug] extractArchive: hdr="
              << (int)hdr[0] << " " << (int)hdr[1] << " " << (int)hdr[2]
              << " " << (int)hdr[3] << " " << (int)hdr[4] << " " << (int)hdr[5]
              << "\n";

    if (hdr[0] == 'P' && hdr[1] == 'K') {
        std::cerr << "[debug] detected: zip\n";
        return extractZip(archivePath, destDir, files, err);
    }
    if (hdr[0] == 0x1f && hdr[1] == 0x8b) {
        std::cerr << "[debug] detected: gzip\n";
        return extractTar(archivePath, destDir, files, err);
    }
    if (hdr[0] == 0xfd && hdr[1] == 0x37 && hdr[2] == 0x7a &&
        hdr[3] == 0x58 && hdr[4] == 0x5a && hdr[5] == 0x00) {
        std::cerr << "[debug] detected: xz\n";
        return decompressAndExtractTar(archivePath, destDir, files, err, true);
    }
    if (hdr[0] == 'B' && hdr[1] == 'Z' && hdr[2] == 'h') {
        std::cerr << "[debug] detected: bz2\n";
        return decompressAndExtractTar(archivePath, destDir, files, err, false);
    }
    {
        uint8_t m[6] = {0};
        in.clear(); in.seekg(257);
        in.read(reinterpret_cast<char*>(m), 5);
        if (in && std::memcmp(m, "ustar", 5) == 0) {
            std::cerr << "[debug] detected: plain tar\n";
            return extractTar(archivePath, destDir, files, err);
        }
    }
    std::cerr << "[debug] unknown archive format\n";
    err = "unknown archive format";
    return false;
}