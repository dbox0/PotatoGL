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

using Includes = std::vector<std::pair<std::string, std::filesystem::file_time_type>>;

// Replaces #include "file" lines with the file, read from dir; #line keeps error line numbers
static bool expandIncludes(std::string& source, const std::filesystem::path& dir, Includes& includes,
                           std::string& error, int depth = 0)
{
    std::string out;
    std::istringstream in(source);
    std::string line;
    int number = 0;
    while (std::getline(in, line)) {
        ++number;
        const std::size_t hash = line.find_first_not_of(" \t");
        if (hash == std::string::npos || line.compare(hash, 8, "#include") != 0) {
            out += line;
            out += '\n';
            continue;
        }
        const std::size_t open = line.find('"', hash);
        const std::size_t close = open == std::string::npos ? open : line.find('"', open + 1);
        if (close == std::string::npos || depth > 8) {
            error = "Bad #include: " + line;
            return false;
        }
        const std::string file = (dir / line.substr(open + 1, close - open - 1)).string();
        std::optional<std::string> included = readFile(file);
        if (!included) {
            error = "Failed to open included " + file;
            return false;
        }
        includes.emplace_back(file, modifiedTime(file));
        if (!expandIncludes(*included, dir, includes, error, depth + 1))
            return false;
        out += "#line 1\n" + *included + "\n#line " + std::to_string(number + 1) + "\n";
    }
    source = std::move(out);
    return true;
}

static GLuint compileShader(GLenum type, const std::string& path, std::string& error, Includes& includes)
{
    std::optional<std::string> src = readFile(path);
    if (!src) {
        error = "Failed to open " + path;
        return 0;
    }
    if (!expandIncludes(*src, std::filesystem::path(path).parent_path(), includes, error))
        return 0;

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

bool Shader::loadCompute(const std::string& path)
{
    m_compute = true;
    m_vertPath = path;
    m_fragPath.clear();
    m_vertTime = modifiedTime(path);

    std::string error;
    Includes includes;
    const GLuint cs = compileShader(GL_COMPUTE_SHADER, path, error, includes);
    m_includes = std::move(includes);
    if (!cs) {
        m_lastError = error;
        std::fprintf(stderr, "%s\n", error.c_str());
        return false;
    }
    GLuint program = glCreateProgram();
    glAttachShader(program, cs);
    glLinkProgram(program);
    glDeleteShader(cs);

    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        m_lastError = "Link error (" + path + "):\n" + log;
        std::fprintf(stderr, "%s\n", m_lastError.c_str());
        glDeleteProgram(program);
        return false;
    }
    glDeleteProgram(m_program);
    m_program = program;
    m_lastError.clear();
    return true;
}

bool Shader::load(const std::string& vertPath, const std::string& fragPath)
{
    m_compute = false;
    m_vertPath = vertPath;
    m_fragPath = fragPath;
    m_vertTime = modifiedTime(vertPath);
    m_fragTime = modifiedTime(fragPath);

    std::string error;
    Includes includes;
    GLuint vs = compileShader(GL_VERTEX_SHADER, vertPath, error, includes);
    GLuint fs = vs ? compileShader(GL_FRAGMENT_SHADER, fragPath, error, includes) : 0;
    m_includes = std::move(includes);
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
    if (m_vertPath.empty())
        return false;
    return m_compute ? loadCompute(m_vertPath) : load(m_vertPath, m_fragPath);
}

bool Shader::reloadIfChanged()
{
    if (m_vertPath.empty())
        return false;
    bool changed = modifiedTime(m_vertPath) != m_vertTime || (!m_compute && modifiedTime(m_fragPath) != m_fragTime);
    for (const auto& [file, time] : m_includes)
        changed = changed || modifiedTime(file) != time;
    if (!changed)
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
