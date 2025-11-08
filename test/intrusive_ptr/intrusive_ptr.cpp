#include <atomic>
#include <memory>
#include <type_traits>
#include <iostream>

struct IntrusiveTarget {
    int strong_count{0};
    int weak_count{0};

    virtual ~IntrusiveTarget() = default;
    virtual void release_resources() {}
};

template <typename T>
class IntrusivePtr {
    T* target_;

    void retain() {
        // call when you get ownership of the object
        target_->strong_count++;
        target_->weak_count++;
    }

    /**
     * Releases ownership of the target object
     */
    void reset() {
        if (!target_) {return;}
        target_->strong_count--;
        target_->weak_count--;
        if (target_->strong_count == 0) {
            target_->release_resources();
        }
        if (target_->weak_count == 0) {
            delete target_;
        }
        target_ = nullptr; // release ownership
    }

    public:
        int strongcount() const {return target_ ? (target_->strong_count) : 0;}

        int weakcount() const {return target_ ? target_->weak_count : 0;}

        explicit IntrusivePtr(T* t = nullptr) : target_(t) {
            std::cout << "construct intrusiveptr" << std::endl;
            if (target_ != nullptr) {
                retain();
            }
        }

        // e.g. IntrusivePtr x = y, this will call this implicit constructor
        IntrusivePtr(const IntrusivePtr& rhs) : target_(rhs.target_) {
            retain();
        }

        ~IntrusivePtr() {reset();}

        // = operators return a reference to the class

        // = with move semantics, lhs must be an lvalue, rhs is rvalue
        // here, IntrusivePtr is implicitly <T>
        IntrusivePtr& operator=(IntrusivePtr&& rhs) & noexcept {
            return this->template operator=<T>(std::move(rhs)); // use forward to avoid copy
        }

        // this is the = with move semantics for a derived class
        template <typename T2>
        IntrusivePtr& operator=(IntrusivePtr<T2>&& rhs) & noexcept {
            static_assert(std::is_convertible_v<T2*, T*>, "type mismatch");
            IntrusivePtr tmp = std::move(rhs); // keep avoiding copy
            swap(tmp);
            return *this;
        }

        IntrusivePtr& operator=(IntrusivePtr& rhs) & noexcept {
            return this->template operator=<T>(rhs);
        }

        // const here so we don't increment refcount via copy
        template <typename T2>
        IntrusivePtr& operator=(const IntrusivePtr<T2>& rhs) & noexcept {
            static_assert(std::is_convertible_v<T2*, T*>, "type mismatch");
            IntrusivePtr tmp = rhs; // make a copy
            swap(tmp); // target_ deconstructor will call after this function
            return *this;
        }

        void swap(IntrusivePtr& rhs) noexcept {
            std::swap(target_, rhs.target_);
        }
};

struct Test : IntrusiveTarget {
    void release_resources() override {
        std::cout << "releasing" << std::endl;
    }
};

template<typename T>
void print_counts(const IntrusivePtr<T>& t) {
    std::cout << t.strongcount() << "-" << t.weakcount() << std::endl;
}

int main() {
    IntrusivePtr<Test> t1(new Test()); // default constructor
    print_counts(t1);
    {
        IntrusivePtr<Test> t2;
        t2 = t1;
        print_counts(t1);
        print_counts(t2);
    }
    print_counts(t1);
}