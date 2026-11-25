#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

struct ImageData
{
    int width = 0;
    int height = 0;
    std::vector<unsigned char> rgba;
};

std::optional<ImageData> loadImage(const std::string& path);
std::optional<ImageData> loadImageFromMemory(const unsigned char* data, std::size_t size);
