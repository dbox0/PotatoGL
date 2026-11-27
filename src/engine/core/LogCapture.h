#pragma once

#include <string>
#include <vector>

// Redirects fds 1 and 2 through pipes so everything written to stdout/stderr (printf,
// iostreams, SDL, drivers) can be shown in-app; still echoed to the original streams
namespace logcapture
{
struct Line
{
    std::string text;
    bool error = false;
};

void start();
void stop();
// Moves lines captured since the last call into out
void drain(std::vector<Line>& out);
}  // namespace logcapture
