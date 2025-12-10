// Example of a simple C++20 module interface
// This file demonstrates module syntax and best practices

module;

// Global module fragment - include traditional headers here
// These are only visible during module compilation, not to importers
#include <memory>
#include <string>
#include <vector>

export module example.simple;

// Module exports - this is what users of the module can see

export namespace example {

// Export a class
class SimpleClass {
public:
    SimpleClass() = default;
    explicit SimpleClass(std::string name) : name_(std::move(name)) {}
    
    const std::string& name() const { return name_; }
    void set_name(std::string name) { name_ = std::move(name); }
    
private:
    std::string name_;
};

// Export a function
void do_something() {
    // Implementation
}

// Export a template
template<typename T>
class Container {
public:
    void add(T item) {
        items_.push_back(std::move(item));
    }
    
    size_t size() const { return items_.size(); }
    
private:
    std::vector<T> items_;
};

// Export a constexpr function
constexpr int calculate_something(int x) {
    return x * 2 + 1;
}

} // namespace example

// Module implementation unit would go in a separate file:
// module example.simple;
// ... implementation of non-inline functions ...
