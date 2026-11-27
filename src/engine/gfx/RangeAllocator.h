#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

// First-fit allocator of [first, first + count) ranges in an index space that only grows.
// Freed ranges merge with their neighbours; a free range at the end lowers end().
class RangeAllocator
{
private:
    struct Range
    {
        std::uint32_t first;
        std::uint32_t count;
    };
    std::vector<Range> m_free;  // sorted by first, never adjacent
    std::uint32_t m_end = 0;

public:
    std::uint32_t allocate(std::uint32_t count)
    {
        for (auto it = m_free.begin(); it != m_free.end(); ++it) {
            if (it->count < count)
                continue;
            const std::uint32_t first = it->first;
            it->first += count;
            it->count -= count;
            if (it->count == 0)
                m_free.erase(it);
            return first;
        }
        const std::uint32_t first = m_end;
        m_end += count;
        return first;
    }

    void free(std::uint32_t first, std::uint32_t count)
    {
        if (count == 0)
            return;
        auto it = std::lower_bound(m_free.begin(), m_free.end(), first,
                                   [](const Range& r, std::uint32_t f) { return r.first < f; });
        it = m_free.insert(it, {first, count});
        if (it + 1 != m_free.end() && it->first + it->count == (it + 1)->first) {
            it->count += (it + 1)->count;
            m_free.erase(it + 1);
        }
        if (it != m_free.begin() && (it - 1)->first + (it - 1)->count == it->first) {
            (it - 1)->count += it->count;
            it = m_free.erase(it) - 1;
        }
        if (it->first + it->count == m_end) {
            m_end = it->first;
            m_free.erase(it);
        }
    }

    // One past the highest allocated index
    std::uint32_t end() const { return m_end; }
};
