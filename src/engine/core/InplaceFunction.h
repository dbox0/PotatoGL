#pragma once

#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>

template<typename Signature, std::size_t Capacity = 64>
class InplaceFunction;

// Move-only std::function replacement that never allocates. A callable bigger than
// Capacity is a compile error; capture less, or capture a pointer.
template<typename R, typename... Args, std::size_t Capacity>
class InplaceFunction<R(Args...), Capacity>
{
private:
    struct Ops
    {
        R (*invoke)(void*, Args&&...);
        void (*move)(void* dst, void* src);
        void (*destroy)(void*);
    };

    template<typename F>
    static constexpr Ops OpsFor{
        [](void* p, Args&&... args) -> R { return (*static_cast<F*>(p))(std::forward<Args>(args)...); },
        [](void* dst, void* src) { ::new (dst) F(std::move(*static_cast<F*>(src))); },
        [](void* p) { static_cast<F*>(p)->~F(); },
    };

    alignas(std::max_align_t) unsigned char m_storage[Capacity];
    const Ops* m_ops = nullptr;

    void reset()
    {
        if (m_ops)
            m_ops->destroy(m_storage);
        m_ops = nullptr;
    }

public:
    InplaceFunction() = default;

    template<typename F, typename D = std::decay_t<F>>
        requires(!std::is_same_v<D, InplaceFunction> && std::is_invocable_r_v<R, D&, Args...>)
    InplaceFunction(F&& fn)
    {
        static_assert(sizeof(D) <= Capacity, "callable too big for InplaceFunction; capture less");
        static_assert(alignof(D) <= alignof(std::max_align_t));
        ::new (m_storage) D(std::forward<F>(fn));
        m_ops = &OpsFor<D>;
    }

    InplaceFunction(InplaceFunction&& other) noexcept
    {
        if (other.m_ops) {
            other.m_ops->move(m_storage, other.m_storage);
            m_ops = other.m_ops;
            other.reset();
        }
    }

    InplaceFunction& operator=(InplaceFunction&& other) noexcept
    {
        if (this != &other) {
            reset();
            if (other.m_ops) {
                other.m_ops->move(m_storage, other.m_storage);
                m_ops = other.m_ops;
                other.reset();
            }
        }
        return *this;
    }

    InplaceFunction(const InplaceFunction&) = delete;
    InplaceFunction& operator=(const InplaceFunction&) = delete;

    ~InplaceFunction() { reset(); }

    explicit operator bool() const { return m_ops != nullptr; }

    R operator()(Args... args) { return m_ops->invoke(m_storage, std::forward<Args>(args)...); }
};
