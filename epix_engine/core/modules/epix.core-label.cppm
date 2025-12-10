/**
 * @file epix.core-label.cppm
 * @brief Label partition for system and schedule labels
 */

export module epix.core:label;

#include <cstdint>
#include <functional>
#include <string_view>

export namespace epix::core {
    struct Label {
       private:
        const char* _name;
        size_t _hash;

       public:
        constexpr Label(const char* name) : _name(name), _hash(hash_string(name)) {}

        constexpr std::string_view name() const { return _name; }
        constexpr size_t hash() const { return _hash; }
        
        constexpr bool operator==(const Label& other) const {
            return _hash == other._hash;
        }

       private:
        static constexpr size_t hash_string(const char* str) {
            size_t hash = 0;
            while (*str) {
                hash = hash * 31 + static_cast<size_t>(*str++);
            }
            return hash;
        }
    };
}  // namespace epix::core

export namespace std {
    template<>
    struct hash<epix::core::Label> {
        size_t operator()(const epix::core::Label& label) const {
            return label.hash();
        }
    };
}
