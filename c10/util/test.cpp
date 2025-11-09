#include "intrusive_ptr.h"
#include <iostream>

using namespace c10::intrusive_ptr;

struct MyStruct : intrusive_ptr_target {
    
    int id;

    MyStruct(int x) : id(x) {}

    ~MyStruct() {
        // release_resources(); // probably should call this
        std::cout << "destructing" << id << "..." << std::endl;
    }

    void release_resources() {
        std::cout << "releasing" << id << "..." << std::endl;
    }
};

int main() {
    intrusive_ptr x = make_intrusive<MyStruct>(1);
    x.getStrong();
    {
        intrusive_ptr y = make_intrusive<MyStruct>(2);
        y = x;
        x.getStrong();
        y.getStrong();
    }
    x.getStrong();
}
