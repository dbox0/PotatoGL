#pragma once

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <filesystem>
#include <string>

class Shader
{
private:
    GLuint m_program = 0;
    std::string m_vertPath;
    std::string m_fragPath;
    std::filesystem::file_time_type m_vertTime;
    std::filesystem::file_time_type m_fragTime;
    std::string m_lastError;
    bool m_compute = false;  // m_vertPath is then the compute shader

public:
    Shader() = default;
    ~Shader();

    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    // On failure the previous program is kept and the log is in lastError()
    bool load(const std::string& vertPath, const std::string& fragPath);
    bool loadCompute(const std::string& path);
    bool reload();
    // True only if a source file changed and the new program linked
    bool reloadIfChanged();
    const std::string& lastError() const { return m_lastError; }

    void use() const;
    GLint uniformLocation(const char* name) const;
    void setInt(const char* name, int value) const;
    void setMat4(const char* name, const glm::mat4& value) const;
    static void setMat4(GLint location, const glm::mat4& value);
};
