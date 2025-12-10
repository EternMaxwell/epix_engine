// Simple module interface that can compile to verify module support
module;

#include <string>
#include <iostream>

export module test_module;

export namespace test {

class HelloWorld {
public:
    HelloWorld(std::string message) : message_(std::move(message)) {}
    
    void say_hello() const {
        std::cout << "Hello from module: " << message_ << std::endl;
    }
    
private:
    std::string message_;
};

void greet(const std::string& name) {
    HelloWorld hw(name);
    hw.say_hello();
}

} // namespace test
