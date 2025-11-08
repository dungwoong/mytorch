#include <atomic>
#include <memory>
#include <type_traits>
#include <iostream>
#include <string>

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

        // Remove one strong ref(aka 1 strong, 1 weak)
        target_->strong_count--;
        target_->weak_count--;

        // Strong count = 0, then release resources and handle other stuff
        if (target_->strong_count == 0) {
            target_->release_resources();
        }
        if (target_->weak_count == 0) {
            delete target_;
        }
        target_ = nullptr; // release ownership in this specific instance
    }

    public:

        // for debugging
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
            std::cout << "implicit constructor" << std::endl;
            retain();
        }

        IntrusivePtr(IntrusivePtr&&) noexcept = delete;

        ~IntrusivePtr() {
            std::cout << "deconstruct" << std::endl;
            reset();
        }

        // = operators return a reference to the class

        // = with move semantics, lhs must be an lvalue, rhs is rvalue
        // here, IntrusivePtr is implicitly <T>. We just call the other operator= that works with general types
        IntrusivePtr& operator=(IntrusivePtr&& rhs) & noexcept {
            return this->template operator=<T>(std::move(rhs)); // use forward to avoid copy
        }

        // this is the = with move semantics for a derived class
        template <typename T2>
        IntrusivePtr& operator=(IntrusivePtr<T2>&& rhs) & noexcept {
            static_assert(std::is_convertible_v<T2*, T*>, "type mismatch");
            IntrusivePtr tmp = std::move(rhs); // keep avoiding copy, note that after the function exits tmp will be deleted
            swap(tmp);
            return *this;
        }

        // IN the other operator=, we call IntrusivePtr tmp = rhs which invokes this
        IntrusivePtr& operator=(IntrusivePtr& rhs) & noexcept {
            return this->template operator=<T>(rhs);
        }

        // const here so we don't increment refcount via copy
        template <typename T2>
        IntrusivePtr& operator=(const IntrusivePtr<T2>& rhs) & noexcept {
            std::cout << "= with copy semantics called" << std::endl;
            static_assert(std::is_convertible_v<T2*, T*>, "type mismatch");
            IntrusivePtr tmp = rhs; // make a copy
            swap(tmp); // tmp deconstructor will call after this function
            return *this;
        }

        void swap(IntrusivePtr& rhs) noexcept {
            std::swap(target_, rhs.target_);
        }

        T* get() const noexcept {return target_;}
};

struct Test : IntrusiveTarget {
    std::string s;

    Test(std::string s_) : s(s_) {}

    void print_s() {
        std::cout << s << std::endl;
    }

    void release_resources() override {
        std::cout << "releasing" << std::endl;
    }
};

template<typename T>
void print_counts(const IntrusivePtr<T>& t) {
    std::cout << t.strongcount() << "-" << t.weakcount() << std::endl;
}

int main() {
    IntrusivePtr<Test> t1(new Test("test_obj_1")); // default constructor
    print_counts(t1);
    {
        Test* test = new Test("test_obj_2");
        IntrusivePtr<Test> t2(test);
        t2 = t1;
        t2.get()->print_s();
        print_counts(t1);
        print_counts(t2);
        std::cout << "exiting scope" << std::endl;
    }
    t1.get()->print_s();
    print_counts(t1);
}