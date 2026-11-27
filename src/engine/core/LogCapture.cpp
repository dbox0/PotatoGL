#include "LogCapture.h"

#include <cstdio>
#include <mutex>
#include <thread>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#define LC_PIPE(fds) _pipe(fds, 4096, _O_BINARY)
#define LC_DUP _dup
#define LC_DUP2 _dup2
#define LC_CLOSE _close
#define LC_READ _read
#define LC_WRITE _write
#else
#include <unistd.h>
#define LC_PIPE(fds) pipe(fds)
#define LC_DUP dup
#define LC_DUP2 dup2
#define LC_CLOSE close
#define LC_READ read
#define LC_WRITE write
#endif

namespace logcapture
{
namespace
{
struct Stream
{
    int fd = -1;
    int saved = -1;
    bool error = false;
    std::thread reader;
};

std::mutex g_mutex;
std::vector<Line> g_pending;
Stream g_streams[2];
bool g_running = false;

void push(std::string& partial, bool error)
{
    if (!partial.empty() && partial.back() == '\r')
        partial.pop_back();
    std::lock_guard lock(g_mutex);
    g_pending.push_back({std::move(partial), error});
    partial.clear();
}

void readLoop(int readFd, Stream& stream)
{
    char buffer[4096];
    std::string partial;
    for (;;) {
        const auto n = LC_READ(readFd, buffer, sizeof(buffer));
        if (n <= 0)
            break;
        LC_WRITE(stream.saved, buffer, static_cast<unsigned>(n));
        for (int i = 0; i < n; ++i) {
            if (buffer[i] == '\n')
                push(partial, stream.error);
            else
                partial += buffer[i];
        }
    }
    if (!partial.empty())
        push(partial, stream.error);
    LC_CLOSE(readFd);
}

bool redirect(Stream& stream, int fd, bool error)
{
    int fds[2];
    if (LC_PIPE(fds) != 0)
        return false;
    stream.fd = fd;
    stream.error = error;
    stream.saved = LC_DUP(fd);
    LC_DUP2(fds[1], fd);
    LC_CLOSE(fds[1]);
    stream.reader = std::thread(readLoop, fds[0], std::ref(stream));
    return true;
}

void restore(Stream& stream)
{
    if (stream.fd < 0)
        return;
    LC_DUP2(stream.saved, stream.fd);  // closes the pipe's last write end, the reader sees EOF
    if (stream.reader.joinable())
        stream.reader.join();
    LC_CLOSE(stream.saved);
    stream.fd = -1;
}
}  // namespace

void start()
{
    if (g_running)
        return;
    std::fflush(stdout);
    std::fflush(stderr);
    // A pipe makes stdout fully buffered; keep lines arriving as they're printed
    std::setvbuf(stdout, nullptr, _IOLBF, 1024);
    redirect(g_streams[0], 1, false);
    redirect(g_streams[1], 2, true);
    g_running = true;
}

void stop()
{
    if (!g_running)
        return;
    std::fflush(stdout);
    std::fflush(stderr);
    for (Stream& stream : g_streams)
        restore(stream);
    g_running = false;
}

void drain(std::vector<Line>& out)
{
    std::lock_guard lock(g_mutex);
    for (Line& line : g_pending)
        out.push_back(std::move(line));
    g_pending.clear();
}
}  // namespace logcapture
