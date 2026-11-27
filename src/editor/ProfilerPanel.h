#pragma once

class GpuProfiler;

// Per-pass GPU and CPU times; opened from Debug > Profiler
class ProfilerPanel
{
private:
    bool m_open = false;

public:
    bool& open() { return m_open; }
    void draw(GpuProfiler& profiler);
};
