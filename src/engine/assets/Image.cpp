#include "Image.h"

#include <stb_image.h>

#include <cstdio>

static std::optional<ImageData> takePixels(unsigned char* pixels, int width, int height)
{
    if (!pixels) {
        std::fprintf(stderr, "Failed to decode image: %s\n", stbi_failure_reason());
        return std::nullopt;
    }
    ImageData image;
    image.width = width;
    image.height = height;
    image.rgba.assign(pixels, pixels + static_cast<std::size_t>(width) * height * 4);
    stbi_image_free(pixels);
    return image;
}

std::optional<ImageData> loadImage(const std::string& path)
{
    int w, h, channels;
    unsigned char* pixels = stbi_load(path.c_str(), &w, &h, &channels, 4);
    if (!pixels)
        std::fprintf(stderr, "Failed to load %s\n", path.c_str());
    return takePixels(pixels, w, h);
}

std::optional<ImageData> loadImageFromMemory(const unsigned char* data, std::size_t size)
{
    int w, h, channels;
    unsigned char* pixels = stbi_load_from_memory(data, static_cast<int>(size), &w, &h, &channels, 4);
    return takePixels(pixels, w, h);
}
