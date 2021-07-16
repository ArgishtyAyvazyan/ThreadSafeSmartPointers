#ifndef THREADSAFESMARTPOINTERS_TS_MEMORY_H
#define THREADSAFESMARTPOINTERS_TS_MEMORY_H

/**
 * @file        ts_memory.h
 * @author      Argishti Ayvazyan (ayvazyan.argishti@gmail.com)
 * @brief       Declaration and implementations of thread-safe smart pointers.
 * @date        7/14/2021
 * @copyright   Copyright (c) 2021
 */

#include <memory>
#include <mutex>
#include <type_traits>

////////////////////////////////////////////////////////////////////////////////////////////////////
namespace ts
{
////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief           ts::unique_ptr is a thread-safe smart pointer that owns and manages and
 *                  provides thread safety of another object through a pointer and disposes
 *                  of that object when the ts::unique_ptr goes out of scope.
 *
 * @details         The ts::unique_ptr implemented using Execute Around Pointer Idiom.
 *                  The all methods called using structure dereference or subscript
 *                  operators will be executed under the protection of a mutex.
 *
 * @example         // Below shown 2 examples for simple use.
 *                  ts::unique_ptr<std::vector<int>> p_vec { new std::vector<int>{} };
 *                  p_vec->push_back(13);
 *
 * @example         ts::unique_ptr<std::vector<int>> p_vec = ts::make_unique<std::vector<int>>();
 *                  p_vec->push_back(13);
 *
 * @example         // Below shown use example for array.
 *                  auto ptr = ts::make_unique<int32_t[]>(100);
 *                  (*ptr)[1] = 12;
 *                  auto val = (*ptr)[2];
 *
 * @warning         Do not take any reference using subscript operator.
 *                  All saved references are not thread-safe.
 *
 * @example         // Below shown example for use with custom mutex.
 *                  ts::unique_ptr<std::vector<int>, boost::sync::spin_mutex>
 *                      p_vec { new std::vector<int>{} };
 *                  p_vec->push_back(13);
 *
 * @example         Below shown example for use with custom deleter.
 *                  ts::unique_ptr<std::vector<int>, std::mutex, std::function<void(std::vector<int>*)>>
 *                      p_vec { new std::vector<int>{}, [](auto* vec) { delete vec; } };
 *                  p_vec->push_back(13);
 *
 * @warning         Structure dereference or subscript operators cannot protect object from
 *                  API races. To safe object from API races, you can use lock, unlock and get
 *                  API's. The examples below.
 *
 * @example         auto queue = ts::make_unique<std::queue<int32_t>>();
 *                  // do something
 *                  {
 *                      queue.lock();
 *                      if (queue.get()->empty())
 *                      {
 *                          queue.get()->pop();
 *                      }
 *                      queue->unlock();
 *                  }
 *
 * @example         auto queue = ts::make_unique<std::queue<int32_t>>();
 *                  // do something
 *                  {
 *                      std::lock_guard lock{queue};
 *                      if (queue.get()->empty())
 *                      {
 *                          queue.get()->pop();
 *                      }
 *                  }
 *
 * @tparam T        The type of element or array of elements.
 * @tparam TMutex   The type of mutex (optional by default std::mutex)
 * @tparam TDeleter The type of deleter (optional by default std::default_delete<T>)
 */
template <typename T, typename TMutex = std::mutex, typename TDeleter = std::default_delete<T>>
class unique_ptr
{
    using t_unique_ptr = std::unique_ptr<T, TDeleter>;
    using t_mutex = TMutex;
    using t_unique_lock = std::unique_lock<t_mutex>;
    using t_element_type = typename t_unique_ptr::element_type;

public:
    /**
     * std::remove_reference<Deleter>::type::pointer if that type exists, otherwise T*.
     * Must satisfy NullablePointer
     */
    using pointer = typename t_unique_ptr::pointer;

    /**
     * T, the type of the object managed by this unique_ptr.
     */
    using element_type = typename t_unique_ptr::element_type;

    /**
     * Deleter, the function object or lvalue reference to function or to function object,
     * to be called from the destructor.
     */
    using deleter_type = typename t_unique_ptr::deleter_type;

private:
    ////////////////////////////////////////////////////////////////////////////////////////////////
    /**
     * @internal
     *
     * @class       proxy_locker
     * @brief       The class proxy_locker is a proxy object and wrapper for mutex and pointer
     *              that provides a convenient RAII-style mechanism for owning a mutex for the
     *              duration of an object lifetime.
     *
     * @details     When a proxy_locker object is created, it attempts to take ownership of the
     *              mutex it is given. When control leaves the scope in which the proxy_locker
     *              object was created, the proxy_locker is destructed and the mutex is released.
     */
    class proxy_locker
    {
    public:
        /**
         * @brief       Construct object from mutex and object pointer.
         *              During the construction proxy_locker object locked the given mutex until
         *              object destruction.
         *
         * @param mtx   The mutex reference for locking.
         * @param ptr   The object pointer for giving to a user.
         */
        proxy_locker(t_mutex& mtx, T* ptr) noexcept
            : m_lock(mtx)
            , m_ptr(ptr)
        {
        }

        proxy_locker(proxy_locker&& o) noexcept = default;
        ~proxy_locker() = default;

        proxy_locker() = delete;
        proxy_locker(const proxy_locker&) = delete;
        proxy_locker& operator=(proxy_locker&&) = delete;
        proxy_locker& operator=(const proxy_locker&) = delete;

        t_element_type* operator->() noexcept
        {
            return m_ptr;
        }

        const t_element_type* operator->() const noexcept
        {
            return m_ptr;
        }

    private:
        t_unique_lock m_lock;
        t_element_type* m_ptr = nullptr;
    }; // class proxy_locker
    ////////////////////////////////////////////////////////////////////////////////////////////////


    ////////////////////////////////////////////////////////////////////////////////////////////////
    /**
     * @internal
     *
     * @class       proxy_locker_for_subscript
     * @brief       The class proxy_locker_for_subscript is a proxy object and wrapper for mutex
     *              and pointer that provides a convenient RAII-style mechanism for owning a mutex
     *              for the duration of an object lifetime.
     *
     * @details     When a proxy_locker_for_subscript object is created, it attempts to take
     *              ownership of the mutex it is given. When control leaves the scope in which the
     *              proxy_locker_for_subscript object was created, the proxy_locker_for_subscript
     *              is destructed and the mutex is released.
     */
    class proxy_locker_for_subscript
    {
    public:
        /**
         * @brief       Construct object from mutex and object pointer.
         *              During the construction proxy_locker_for_subscript object locked the
         *              given mutex until object destruction.
         *
         * @param mtx   The mutex reference for locking.
         * @param ptr   The object pointer for giving to a user.
         */
        proxy_locker_for_subscript(t_mutex& mtx, t_element_type* ptr) noexcept
            : m_lock(mtx)
            , m_ptr(ptr)
        {
        }

        proxy_locker_for_subscript(proxy_locker_for_subscript&& o) noexcept = default;
        ~proxy_locker_for_subscript() = default;

        proxy_locker_for_subscript() = delete;
        proxy_locker_for_subscript(const proxy_locker_for_subscript&) = delete;
        proxy_locker_for_subscript& operator=(proxy_locker_for_subscript&&) = delete;
        proxy_locker_for_subscript& operator=(const proxy_locker_for_subscript&) = delete;

    public:
        /**
         * @brief           The subscript operator for working with arrays.
         *
         * @tparam TIndex   The array index type.
         * @param index     The array index.
         * @return          The reference to the object.
         */
        template <typename TIndex>
        t_element_type& operator[](TIndex index) noexcept
        {
            return m_ptr[index];
        }

    private:
        t_unique_lock m_lock;
        t_element_type* m_ptr = nullptr;
    }; // class proxy_locker_for_subscript
    ////////////////////////////////////////////////////////////////////////////////////////////////

public:
    /**
     * @brief   Locks the mutex, blocks if the mutex is not available.
     *          Using for solve API races.
     *
     * @example std::lock_guard lock{queue};
     *          if (!queue.get()->empty())
     *          {
     *              (void) queue.get()->pop();
     *          }
     */
    void lock()
    {
        m_mtx.lock();
    }

    /**
     * @brief   Unlocks the mutex.
     *          Using for solve API races.
     */
    void unlock()
    {
        m_mtx.unlock();
    }

    /**
     * @brief   Gets raw pointer to object.
     *
     * @warning This API is not thread-safe. Use it only then unique_ptr
     *          locked (\refitem ts::unique_ptr::lock).
     *
     * @return  The raw pointer.
     */
    [[nodiscard]] t_element_type* get() const noexcept
    {
        return m_value.get();
    }

public:
    unique_ptr() = default;

    /**
     * @brief           Constructs ts::unique_ptr from raw pointer.
     *
     * @param value_ptr The raw pointer.
     */
    explicit unique_ptr(t_element_type* value_ptr)
        : m_mtx {}
        , m_value(value_ptr)
    {
    }

    /**
     * @brief           Constructs ts::unique_ptr with custom deleter from the raw
     *                  pointer and deleter.
     *
     * @param value_ptr The raw pointer.
     * @param deleter   The deleter object.
     */
    unique_ptr(t_element_type* value_ptr, deleter_type deleter)
        : m_mtx {}
        , m_value(value_ptr, deleter)
    {
    }

    /**
     * Prevent copying of an object.
     */
    unique_ptr(const unique_ptr&) = delete;
    unique_ptr& operator=(const unique_ptr&) = delete;

    unique_ptr(unique_ptr&& other) noexcept
    {
        std::scoped_lock lock {this->m_mtx, other.m_mtx};
        this->m_value = std::move(other.m_value);
    }

    unique_ptr& operator=(unique_ptr&& other) noexcept
    {
        std::scoped_lock lock {this->m_mtx, other.m_mtx};
        this->m_value = std::move(other.m_value);
        return *this;
    }

    /**
     * @brief   Returns the reference to the object.
     *
     * @details This API working on Execute Around Pointer Idiom.
     *          Before giving the object reference to user locks the mutex,
     *          the mutex still locked until reached ";".
     *
     * @example ts::unique_ptr<std::vector<int>> p_vec = ts::make_unique<std::vector<int>>();
     *          p_vec->push_back(13);
     *
     * @return  Returns a pointer to the object owned by *this.
     */
    proxy_locker operator->() const
    {
        return proxy_locker(m_mtx, m_value.get());
    }

    /**
     * @brief   Returns the object with subscript operator for working with arrays.
     *
     * @details This API working on Execute Around Pointer Idiom.
     *          Before giving the object reference to user locks the mutex,
     *          the mutex still locked until reached ";".
     *
     * @return  Returns a pointer to the object owned by *this.
     */
    proxy_locker_for_subscript operator*() const
    {
        return proxy_locker_for_subscript(m_mtx, m_value.get());
    }

private:
    /**
     * The mutex for providing object thread-safety.
     */
    mutable t_mutex m_mtx;

    /**
     * The non-thread-safe unique pointer for manage object lifetime.
     */
    t_unique_ptr m_value;
};

/**
 * @brief           Constructs an object of type T and wraps it in a
 *                  ts::unique_ptr. (Specialization for a single object.)
 *
 * @example         ts::unique_ptr<std::vector<int>> p_vec = ts::make_unique<std::vector<int>>();
 *                  p_vec->push_back(13);
 *
 * @tparam T        The type of element.
 * @tparam TArgs    The types of list of arguments with which an instance of
 *                  T will be constructed.
 * @param args      List of arguments with which an instance of T will be constructed.
 * @return          ts::unique_ptr of an instance of type T.
 */
template <class T, class... TArgs>
std::enable_if_t<!std::is_array<T>::value, unique_ptr<T>> make_unique(TArgs&&... args)
{
    return unique_ptr<T>(new T(std::forward<TArgs>(args)...));
}

/**
 * @brief           Constructs an object of type T array and wraps it in a
 *                  ts::unique_ptr. (Specialization for a objects array.)
 *
 * @example         auto arr_ptr = ts::make_unique<int32_t[]>(element_count);
 *                  for (int32_t i = 0; i < element_count; ++i)
 *                  {
 *                      (*arr_ptr)[i] = 0;
 *                  }
 *
 * @tparam T        The type of elements array.
 * @param n         The length of the array to construct.
 * @return          ts::unique_ptr of an instance of type T.
 */
template <class T>
std::enable_if_t<std::is_array<T>::value, unique_ptr<T>> make_unique(std::size_t n)
{
    using t_element_type = typename std::remove_extent_t<T>;
    return unique_ptr<T>(new t_element_type[n]);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
} // namespace ts
////////////////////////////////////////////////////////////////////////////////////////////////////

#endif // THREADSAFESMARTPOINTERS_TS_MEMORY_H
