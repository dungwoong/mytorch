#pragma once

#include <atomic>
#include <memory>
#include <type_traits>

// Use this as a friend class later
namespace pybind11 {
template <typename, typename...>
class class_;
}

namespace c10 {
class intrusive_ptr_target;
namespace raw {
namespace intrusive_ptr {

namespace detail {
// TODO look line 30, they have some constants stored here

// Default NullType for pointer, for specialized cases
template <class TTarget>
struct intrusive_target_default_null_type final {
    static constexpr TTarget* singleton() noexcept {
        return nullptr;
    }
};

// The refcount is a 64 bit int, split into ref count(first 32 bits) and then weak ref count(last 32 bits)
inline uint32_t refcount(uint64_t combined_refcount) {
  return static_cast<uint32_t>(combined_refcount);
}

inline uint32_t weakcount(uint64_t combined_refcount) {
  return static_cast<uint32_t>(combined_refcount >> 32);
}

} // namespace detail

// TODO define import/export macros, figure out what those do
class intrusive_ptr_target{
    mutable std::atomic<uint64_t> combined_refcount_;
    static_assert(sizeof(std::atomic<uint64_t>) == 8);
    static_assert(alignof(std::atomic<uint64_t>) == 8);
    static_assert(std::atomic<uint64_t>::is_always_lock_free); // no mutexes, guarantees highest performance

    template <typename T, typename N>
    friend class intrusive_ptr;

    protected:
        virtual ~intrusive_ptr_target() {}

        // constexpr: can init at compile-time
        // noexcept: don't compile for that case
        constexpr intrusive_ptr_target() noexcept : combined_refcount_(0) {}

        // disallow moving by overriding the move constructor
        intrusive_ptr_target(intrusive_ptr_target&&) noexcept : intrusive_ptr_target() {}

        // e.g. ptr1 = std::move(ptr2), ptr1 will stay the same and ptr2 is ignored
        intrusive_ptr_target& operator=(intrusive_ptr_target&&) noexcept {
            return *this;
        }

        intrusive_ptr_target(const intrusive_ptr_target&) noexcept : intrusive_ptr_target() {}

        // copy assignment e.g. ptr1=ptr2, just ignore it.
        intrusive_ptr_target& operator=(const intrusive_ptr_target&) noexcept {
            return *this;
        }
    private:
        virtual void release_resources() {}

        // memory order is for atomic load
        uint32_t refcount(std::memory_order order = std::memory_order_relaxed) const {
            return detail::refcount(combined_refcount_.load(order));
        }

        uint32_t weakcount(std::memory_order order = std::memory_order_relaxed) const {
            return detail::weakcount(combined_refcount_.load(order));
        }
};

using weak_intrusive_ptr_target = intrusive_ptr_target; // to help distinguish

// TODO still not sure when you use the weak target or pointer

template <class TTarget, class NullType=detail::intrusive_target_default_null_type<TTarget>>
struct intrusive_ptr final { 
};

} // namespace intrusive_ptr
} // namespace raw
} // namespace c10