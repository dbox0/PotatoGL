#include "Paths.h"

#include <SDL3/SDL_filesystem.h>

#include <filesystem>

std::string resourcePath(std::string_view relative)
{
    static const std::string base = [] {
#ifdef DEV_RESOURCE_DIR
        std::error_code ec;
        if (std::filesystem::is_directory(DEV_RESOURCE_DIR, ec))
            return std::string(DEV_RESOURCE_DIR);
#endif
        const char* path = SDL_GetBasePath();
        return path ? std::string(path) : std::string();
    }();
    return base + std::string(relative);
}

std::string directoryOf(const std::string& path)
{
    const auto slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}
