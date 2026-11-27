#pragma once

#include <glad/gl.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <string_view>
#include <vector>

// GPU and CPU time of named scopes. Timestamps are read Latency frames late so they never stall.
// Scopes nest; names must outlive the profiler (string literals).
class GpuProfiler
{
public:
    struct Result
    {
        std::string_view name;
        int depth;
        double gpuMs;
        double cpuMs;
    };

private:
    using Clock = std::chrono::steady_clock;
    static constexpr std::size_t Latency = 4;

    struct Scope
    {
        std::string_view name;
        int depth;
        GLuint beginQuery, endQuery;
        Clock::time_point cpuBegin, cpuEnd;
    };
    struct Frame
    {
        std::vector<GLuint> queries;  // pool, two per scope
        std::vector<Scope> scopes;
    };

    std::array<Frame, Latency> m_frames;
    std::size_t m_current = 0;
    std::vector<std::size_t> m_open;  // indices into the current frame's scopes
    std::vector<Result> m_results;
    bool m_enabled = false;
    bool m_inFrame = false;

    GLuint query(Frame& frame, std::size_t index);
    void collect(const Frame& frame);

public:
    GpuProfiler() = default;
    ~GpuProfiler();

    GpuProfiler(const GpuProfiler&) = delete;
    GpuProfiler& operator=(const GpuProfiler&) = delete;

    // Takes effect at the next beginFrame
    void setEnabled(bool enabled) { m_enabled = enabled; }
    bool enabled() const          { return m_enabled; }

    void beginFrame();  // opens the root "Frame" scope
    void endFrame();    // closes every open scope
    void begin(std::string_view name);
    void end();

    // Smoothed; the first is the whole frame
    const std::vector<Result>& results() const { return m_results; }
};

class GpuScope
{
private:
    GpuProfiler& m_profiler;

public:
    GpuScope(GpuProfiler& profiler, std::string_view name) : m_profiler(profiler) { profiler.begin(name); }
    ~GpuScope() { m_profiler.end(); }

    GpuScope(const GpuScope&) = delete;
    GpuScope& operator=(const GpuScope&) = delete;
};
