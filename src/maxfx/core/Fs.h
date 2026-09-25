// Tiny C++11 filesystem helpers. Used by the viewer to locate
// data/database/levels next to the executable.
#ifndef MAXFX_CORE_FS_H
#define MAXFX_CORE_FS_H

#include <string>
#include <vector>

namespace maxfx {

std::string joinPath(const std::string& a, const std::string& b);
std::string parentDir(const std::string& path);
std::string fileName(const std::string& path);
std::string lowerCopy(const std::string& s);

bool pathExists(const std::string& path);
bool isDirectory(const std::string& path);
bool isFile(const std::string& path);

// Directory of the running executable (empty on failure).
std::string executableDir();

// Non-recursive listing. `extension` is like ".ldb" (case-insensitive).
std::vector<std::string> listFilesWithExtension(const std::string& dir, const char* extension);

}  // namespace maxfx

#endif  // MAXFX_CORE_FS_H
