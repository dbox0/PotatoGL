#pragma once

#include <entt/entt.hpp>

#include <cstdint>
#include <memory>
#include <string_view>
#include <type_traits>
#include <utility>

// Disconnects on destruction. Move-only, so it can live in a component.
class Connection
{
private:
    entt::connection m_connection;
    std::shared_ptr<void> m_function;  // a lambda listener's storage, which the signal points into

public:
    Connection() = default;
    Connection(entt::connection connection, std::shared_ptr<void> function = {})
        : m_connection(connection), m_function(std::move(function)) {}
    ~Connection() { release(); }

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    Connection(Connection&& other) noexcept
        : m_connection(std::exchange(other.m_connection, {})), m_function(std::move(other.m_function)) {}
    Connection& operator=(Connection&& other) noexcept
    {
        if (this != &other) {
            release();
            m_connection = std::exchange(other.m_connection, {});
            m_function = std::move(other.m_function);
        }
        return *this;
    }

    explicit operator bool() const { return static_cast<bool>(m_connection); }

    void release()
    {
        m_connection.release();
        m_function.reset();
    }
};

// Typed events are plain structs; named events carry a hashed name and a value, so data
// can reach listeners that know only a string (UI components). Lives in registry.ctx().
// Listeners must not destroy entities or remove components mid-delivery: use deferred destruction.
class EventBus
{
private:
    entt::dispatcher m_dispatcher;

    template<typename E, typename F>
    static void invoke(F& fn, const E& e) { fn(e); }

public:
    template<typename E>
    void emit(const E& e) { m_dispatcher.trigger(e); }

    template<typename E>
    void enqueue(E e) { m_dispatcher.enqueue(std::move(e)); }

    void flush() { m_dispatcher.update(); }
    void clearQueue() { m_dispatcher.clear(); }

    // instance must outlive the connection and keep its address: systems, not components
    template<typename E, auto Fn, typename T>
    [[nodiscard]] Connection on(T& instance)
    {
        return Connection(m_dispatcher.sink<E>().template connect<Fn>(instance));
    }

    template<typename E, typename F>
    [[nodiscard]] Connection on(F&& fn)
    {
        using Fn = std::decay_t<F>;
        auto function = std::make_shared<Fn>(std::forward<F>(fn));
        entt::connection connection = m_dispatcher.sink<E>().template connect<&EventBus::invoke<E, Fn>>(*function);
        return Connection(connection, std::move(function));
    }
};

struct NamedEvent
{
    std::uint32_t name = 0;  // entt::hashed_string of the name
    entt::entity sender = entt::null;
    float value = 0.0f;
    float max = 1.0f;

    static std::uint32_t hash(std::string_view name)
    {
        return entt::hashed_string::value(name.data(), name.size());
    }

    static NamedEvent make(std::string_view name, float value = 0.0f, float max = 1.0f,
                           entt::entity sender = entt::null)
    {
        return {hash(name), sender, value, max};
    }
};
