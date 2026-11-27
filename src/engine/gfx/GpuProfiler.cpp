#include "GpuProfiler.h"

GpuProfiler::~GpuProfiler()
{
    for (Frame& frame : m_frames) {
        if (!frame.queries.empty())
            glDeleteQueries(static_cast<GLsizei>(frame.queries.size()), frame.queries.data());
    }
}

GLuint GpuProfiler::query(Frame& frame, std::size_t index)
{
    while (frame.queries.size() <= index) {
        GLuint q = 0;
        glGenQueries(1, &q);
        frame.queries.push_back(q);
    }
    return frame.queries[index];
}

void GpuProfiler::beginFrame()
{
    m_inFrame = false;
    m_open.clear();
    if (!m_enabled)
        return;
    m_current = (m_current + 1) % Latency;
    Frame& frame = m_frames[m_current];
    collect(frame);
    frame.scopes.clear();
    m_inFrame = true;
    begin("Frame");
}

void GpuProfiler::endFrame()
{
    while (m_inFrame && !m_open.empty())
        end();
    m_inFrame = false;
}

void GpuProfiler::begin(std::string_view name)
{
    if (!m_inFrame)
        return;
    Frame& frame = m_frames[m_current];
    const std::size_t index = frame.scopes.size();
    Scope scope{name, static_cast<int>(m_open.size()), query(frame, 2 * index), query(frame, 2 * index + 1),
                Clock::now(), {}};
    glQueryCounter(scope.beginQuery, GL_TIMESTAMP);
    frame.scopes.push_back(scope);
    m_open.push_back(index);
}

void GpuProfiler::end()
{
    if (!m_inFrame || m_open.empty())
        return;
    Scope& scope = m_frames[m_current].scopes[m_open.back()];
    m_open.pop_back();
    glQueryCounter(scope.endQuery, GL_TIMESTAMP);
    scope.cpuEnd = Clock::now();
}

// Blocks only if the GPU is more than Latency frames behind
void GpuProfiler::collect(const Frame& frame)
{
    static constexpr double Smoothing = 0.1;
    std::vector<Result> results;
    results.reserve(frame.scopes.size());
    for (std::size_t i = 0; i < frame.scopes.size(); ++i) {
        const Scope& scope = frame.scopes[i];
        GLuint64 begin = 0, end = 0;
        glGetQueryObjectui64v(scope.beginQuery, GL_QUERY_RESULT, &begin);
        glGetQueryObjectui64v(scope.endQuery, GL_QUERY_RESULT, &end);
        Result r{scope.name, scope.depth, static_cast<double>(end - begin) * 1e-6,
                 std::chrono::duration<double, std::milli>(scope.cpuEnd - scope.cpuBegin).count()};
        if (i < m_results.size() && m_results[i].name == r.name && m_results[i].depth == r.depth) {
            r.gpuMs = m_results[i].gpuMs + (r.gpuMs - m_results[i].gpuMs) * Smoothing;
            r.cpuMs = m_results[i].cpuMs + (r.cpuMs - m_results[i].cpuMs) * Smoothing;
        }
        results.push_back(r);
    }
    if (!results.empty())
        m_results = std::move(results);
}
