#pragma once

#include <atomic>
#include <memory>
#include <type_traits>
#include <iostream>

// Use this as a friend class later
namespace pybind11 {
template <typename, typename...>
class class_;
}

namespace c10 {
class intrusive_ptr_target;
namespace raw {
struct DontIncreaseRefCount {};
} // namespace raw
namespace intrusive_ptr {

namespace detail {
// TODO look line 30, they have some constants stored here
constexpr uint64_t kReferenceCountOne = 1; // to increment the reference count by 1
constexpr uint64_t kWeakReferenceCountOne = (kReferenceCountOne << 32);
constexpr uint64_t kUniqueRef = (kReferenceCountOne | kWeakReferenceCountOne); // one strong ref, strong refs add a weak ref

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

inline uint64_t combined_refcount_incrememt(std::atomic<uint64_t>& combined_refcount, uint64_t inc) {
    return combined_refcount.fetch_add(inc, std::memory_order_relaxed) + inc;
}

inline uint64_t combined_refcount_decrement(std::atomic<uint64_t>& combined_refcount, uint64_t dec) {
    // decrementing may lead to destroying the object, we need prevent reordering accesses around this
    return combined_refcount.fetch_sub(dec, std::memory_order_acq_rel) - dec;
}

inline uint32_t atomic_refcount_increment(std::atomic<uint64_t>& combined_refcount) {
    return detail::refcount(combined_refcount_incrememt(combined_refcount, kReferenceCountOne));
}

inline uint32_t atomic_weakcount_increment(std::atomic<uint64_t>& combined_refcount) {
    return detail::weakcount(combined_refcount_incrememt(combined_refcount, kWeakReferenceCountOne));
}

inline uint32_t atomic_weakcount_decrement(std::atomic<uint64_t>& combined_refcount) {
    return detail::weakcount(combined_refcount_decrement(combined_refcount, kWeakReferenceCountOne));
}

} // namespace detail

// TODO define import/export macros, figure out what those do

// So e.g. how you have a shared_ptr<T>, we will have an intrusive_ptr<T>
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

template <class TTarget, class NullType>
class weak_intrusive_ptr; // declare here so we can reference ahead

/**
 * Intrusive ptr stores the count inside the object itself(target)
 * so less indirection so more efficiency
 */
template <class TTarget, class NullType=detail::intrusive_target_default_null_type<TTarget>>
class intrusive_ptr final { 
    private:
        TTarget* target_;

        template <class TT2, class NT2>
        friend class intrusive_ptr;
        friend class weak_intrusive_ptr<TTarget, NullType>;

        // Require for pybind https://pybind11.readthedocs.io/en/stable/advanced/smart_ptrs.html#custom-smart-pointers
        template <typename, typename...>
        friend class pybind11::class_;

        void retain_() {
            if (target_ != NullType::singleton()) {
                detail::atomic_refcount_increment(target_->combined_refcount_);
            }
        }

        void reset_() {
            if (target_ != NullType::singleton()) {
                if (target_->combined_refcount_.load(std::memory_order_acquire) == detail::kUniqueRef) {
                    // No weak references and we're releasing the last strong reference
                    // No other references to this thing, so we can safely destroy it and return
                    target_->combined_refcount_.store(0, std::memory_order_relaxed);
                    delete target_; // automatically releases resources
                    return;
                }

                auto combined_refcount = detail::combined_refcount_decrement(target_->combined_refcount_, detail::kReferenceCountOne);
                if (detail::refcount(combined_refcount) == 0) {
                    // no more strong refs, release the resources to start
                    bool should_delete = (combined_refcount == detail::kWeakReferenceCountOne); // this was the last strong ref
                    if (!should_delete) {
                        // remove_const_t removes const from the type, const_cast removes it from the var
                        const_cast<std::remove_const_t<TTarget>*>(target_)->release_resources();
                        should_delete = detail::atomic_weakcount_decrement(target_->combined_refcount_) == 0; // another thread may concurrently decrement the count
                    }
                    if (should_delete) {
                        delete target_;
                    }
                }
            }
        }

        // Private constructor. Explicit means don't use it for implicit conversions e.g. int x = 5 then x+4.0f
        explicit intrusive_ptr(TTarget* target) : intrusive_ptr(target, raw::DontIncreaseRefCount{}) {
            if (target_ != NullType::singleton()) {
                target->combined_refcount_.store(detail::kUniqueRef, std::memory_order_relaxed); // initialize with 1 weak and 1 strong ref since strong>0 --> weak>0
            }
        }

    public:
        using element_type = TTarget;

        // Constructor + implicit constructor
        intrusive_ptr() noexcept : intrusive_ptr(NullType::singleton(), raw::DontIncreaseRefCount{}) {}

        intrusive_ptr(std::nullptr_t) noexcept : intrusive_ptr(NullType::singleton(), raw::DontIncreaseRefCount{}) {}

        // BASE constructor
        explicit intrusive_ptr(TTarget *target, raw::DontIncreaseRefCount) noexcept : target_(target) {} // won't increase ref count for you

        // Release ownership of the unique ptr and then initialize the pointer
        explicit intrusive_ptr(std::unique_ptr<TTarget> rhs) noexcept : intrusive_ptr(rhs.release()) {}

        intrusive_ptr(intrusive_ptr& rhs) noexcept : target_(rhs.target_) {
            retain_();
        }

        // steal resources
        intrusive_ptr(intrusive_ptr&& rhs) noexcept : target_(rhs.target_) {
            rhs.target_ = NullType::singleton();
        }

        // there's many other constructors here

        ~intrusive_ptr() noexcept {
            reset_();
        }

        // Copy =
        intrusive_ptr& operator=(const intrusive_ptr& rhs) & noexcept {
            return this->template operator=<TTarget>(rhs);
        }

        template <typename T2>
        intrusive_ptr& operator=(intrusive_ptr<T2>& rhs) & noexcept {
            static_assert(std::is_convertible_v<T2*, TTarget*>, "Invalid conversion");
            intrusive_ptr tmp = rhs; // copy constructor
            swap(tmp); // so we're holding the RHS thing
            return *this;
            // tmp will destruct now, calling reset() on whatever we had before
        }

        // Compiler does reference collapsing so T& && --> T&, T&& & --> T&&
        template <class ...Args>
        static intrusive_ptr make(Args&&... args) {
            return intrusive_ptr(new TTarget(std::forward<Args>(args)...));
        }

        TTarget* get() const noexcept {
            return target_;
        }

        void swap(intrusive_ptr rhs) {
            std::swap(target_, rhs.target_);
        }

        void getStrong() const {
            std::cout << target_->refcount() << std::endl;
        }
};

template <class TTarget, class NullType = detail::intrusive_target_default_null_type<TTarget>, class... Args>
inline intrusive_ptr<TTarget, NullType> make_intrusive(Args&&... args) {
    return intrusive_ptr<TTarget, NullType>::make(std::forward<Args>(args)...); // forwarding so no unnecessary move
}

} // namespace intrusive_ptr
} // namespace c10