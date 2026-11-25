#include "Shader.h"

#include <glm/gtc/type_ptr.hpp>

#include <cstdio>
#include <fstream>
#include <optional>
#include <sstream>

static std::optional<std::string> readFile(const std::string& path)
{
    std::ifstream f(path);
    if (!f)
        return std::nullopt;
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static std::filesystem::file_time_type modifiedTime(const std::string& path)
{
    std::error_code ec;
    return std::filesystem::last_write_time(path, ec);
}

static GLuint compileShader(GLenum type, const std::string& path, std::string& error)
{
    const std::optional<std::string> src = readFile(path);
    if (!src) {
        error = "Failed to open " + path;
        return 0;
    }

    const char* cstr = src->c_str();
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &cstr, nullptr);
    glCompileShader(shader);

    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        error = "Compile error (" + path + "):\n" + log;
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

Shader::~Shader()
{
    glDeleteProgram(m_program);
}

bool Shader::load(const std::string& vertPath, const std::string& fragPath)
{
    m_vertPath = vertPath;
    m_fragPath = fragPath;
    m_vertTime = modifiedTime(vertPath);
    m_fragTime = modifiedTime(fragPath);

    std::string error;
    GLuint vs = compileShader(GL_VERTEX_SHADER, vertPath, error);
    GLuint fs = vs ? compileShader(GL_FRAGMENT_SHADER, fragPath, error) : 0;
    if (!vs || !fs) {
        glDeleteShader(vs);
        m_lastError = error;
        std::fprintf(stderr, "%s\n", error.c_str());
        return false;
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        m_lastError = "Link error (" + vertPath + ", " + fragPath + "):\n" + log;
        std::fprintf(stderr, "%s\n", m_lastError.c_str());
        glDeleteProgram(program);
        return false;
    }

    glDeleteProgram(m_program);
    m_program = program;
    m_lastError.clear();
    return true;
}

bool Shader::reload()
{
    return !m_vertPath.empty() && load(m_vertPath, m_fragPath);
}

bool Shader::reloadIfChanged()
{
    if (m_vertPath.empty())
        return false;
    if (modifiedTime(m_vertPath) == m_vertTime && modifiedTime(m_fragPath) == m_fragTime)
        return false;
    return reload();
}

void Shader::use() const
{
    glUseProgram(m_program);
}

GLint Shader::uniformLocation(const char* name) const
{
    return glGetUniformLocation(m_program, name);
}

void Shader::setInt(const char* name, int value) const
{
    glUniform1i(uniformLocation(name), value);
}

void Shader::setMat4(const char* name, const glm::mat4& value) const
{
    setMat4(uniformLocation(name), value);
}

void Shader::setMat4(GLint location, const glm::mat4& value)
{
    glUniformMatrix4fv(location, 1, GL_FALSE, glm::value_ptr(value));
}
