#include "maxfx/core/Fs.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <stdexcept>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace maxfx {
namespace {

bool isSep(char c) { return c == '/' || c == '\\'; }

}  // namespace

std::string lowerCopy(const std::string& s) {
    std::string out = s;
    for (std::size_t i = 0; i < out.size(); ++i) {
        out[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(out[i])));
    }
    return out;
}

std::string joinPath(const std::string& a, const std::string& b) {
    if (a.empty()) {
        return b;
    }
    if (b.empty()) {
        return a;
    }
    const char last = a[a.size() - 1];
    if (isSep(last)) {
        return a + b;
    }
#ifdef _WIN32
    return a + "\\" + b;
#else
    return a + "/" + b;
#endif
}

std::string parentDir(const std::string& path) {
    if (path.empty()) {
        return std::string();
    }
    std::size_t end = path.size();
    while (end > 0 && isSep(path[end - 1])) {
        --end;
    }
    const std::size_t slash = path.find_last_of("/\\", end == 0 ? 0 : end - 1);
    if (slash == std::string::npos) {
        return std::string();
    }
    if (slash == 0) {
        return path.substr(0, 1);
    }
    return path.substr(0, slash);
}

std::string fileName(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos) {
        return path;
    }
    return path.substr(slash + 1);
}

#ifdef _WIN32

bool pathExists(const std::string& path) {
    const DWORD attr = GetFileAttributesA(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES;
}

bool isDirectory(const std::string& path) {
    const DWORD attr = GetFileAttributesA(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool isFile(const std::string& path) {
    const DWORD attr = GetFileAttributesA(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::string executableDir() {
    char buf[MAX_PATH];
    const DWORD n = GetModuleFileNameA(NULL, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        return std::string();
    }
    return parentDir(std::string(buf, n));
}

std::vector<std::string> listFilesWithExtension(const std::string& dir, const char* extension) {
    std::vector<std::string> out;
    if (dir.empty()) {
        return out;
    }
    const std::string needle = lowerCopy(extension ? extension : "");
    const std::string query = joinPath(dir, "*");
    WIN32_FIND_DATAA fd;
    const HANDLE h = FindFirstFileA(query.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) {
        return out;
    }
    do {
        if (fd.cFileName[0] == '.' &&
            (fd.cFileName[1] == 0 || (fd.cFileName[1] == '.' && fd.cFileName[2] == 0))) {
            continue;
        }
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            continue;
        }
        const std::string name = fd.cFileName;
        const std::string lower = lowerCopy(name);
        if (lower.size() >= needle.size() &&
            lower.compare(lower.size() - needle.size(), needle.size(), needle) == 0) {
            out.push_back(joinPath(dir, name));
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    std::sort(out.begin(), out.end());
    return out;
}

#else

bool pathExists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

bool isDirectory(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool isFile(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

std::string executableDir() {
    char buf[4096];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) {
        return std::string();
    }
    buf[n] = 0;
    return parentDir(std::string(buf));
}

std::vector<std::string> listFilesWithExtension(const std::string& dir, const char* extension) {
    std::vector<std::string> out;
    if (dir.empty()) {
        return out;
    }
    const std::string needle = lowerCopy(extension ? extension : "");
    DIR* d = opendir(dir.c_str());
    if (d == 0) {
        return out;
    }
    while (const struct dirent* ent = readdir(d)) {
        if (ent->d_name[0] == '.' &&
            (ent->d_name[1] == 0 || (ent->d_name[1] == '.' && ent->d_name[2] == 0))) {
            continue;
        }
        const std::string name = ent->d_name;
        const std::string full = joinPath(dir, name);
        if (!isFile(full)) {
            continue;
        }
        const std::string lower = lowerCopy(name);
        if (lower.size() >= needle.size() &&
            lower.compare(lower.size() - needle.size(), needle.size(), needle) == 0) {
            out.push_back(full);
        }
    }
    closedir(d);
    std::sort(out.begin(), out.end());
    return out;
}

#endif

std::string readFileText(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f == 0) {
        throw std::runtime_error("cannot open file \"" + path + "\"");
    }
    if (std::fseek(f, 0, SEEK_END) != 0) {
        std::fclose(f);
        throw std::runtime_error("cannot seek file \"" + path + "\"");
    }
    const long sz = std::ftell(f);
    if (sz < 0) {
        std::fclose(f);
        throw std::runtime_error("cannot size file \"" + path + "\"");
    }
    if (std::fseek(f, 0, SEEK_SET) != 0) {
        std::fclose(f);
        throw std::runtime_error("cannot rewind file \"" + path + "\"");
    }
    std::string out;
    out.resize(static_cast<std::size_t>(sz));
    if (sz > 0) {
        const std::size_t n = std::fread(&out[0], 1, out.size(), f);
        std::fclose(f);
        if (n != out.size()) {
            throw std::runtime_error("short read of \"" + path + "\"");
        }
    } else {
        std::fclose(f);
    }
    return out;
}

}  // namespace maxfx
