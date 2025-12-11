/**
 * @file epix.core-schedule.cppm
 * @brief Schedule partition for system scheduling
 */

export module epix.core:schedule;

import :fwd;
import :label;
import :system;

#include <concepts>
#include <functional>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

export namespace epix::core::schedule {
    // System set label
    struct SystemSetLabel {
        Label label;
        
        SystemSetLabel(const char* name) : label(name) {}
        SystemSetLabel(Label l) : label(l) {}
        
        bool operator==(const SystemSetLabel& other) const {
            return label == other.label;
        }
    };
    
    // System configuration
    struct SetConfig {
        std::vector<SystemSetLabel> before;
        std::vector<SystemSetLabel> after;
        std::vector<SystemSetLabel> in_set;
    };
    
    // Execution configuration
    struct ExecuteConfig {
        bool run_if_condition = true;
    };
    
    // Schedule for organizing systems
    struct Schedule {
       private:
        std::vector<std::shared_ptr<system::System>> systems;
        std::unordered_map<Label, std::vector<size_t>> sets;
        
       public:
        Schedule& add_system(std::shared_ptr<system::System> sys);
        
        template <typename F>
        Schedule& add_system(F&& func) {
            return add_system(system::make_system_shared(std::forward<F>(func)));
        }
        
        void run(World& world);
        
        Schedule& configure_set(SystemSetLabel label, SetConfig config);
    };
    
    // System dispatcher for parallel execution
    struct SystemDispatcher {
        void dispatch(Schedule& schedule, World& world);
    };
}  // namespace epix::core::schedule

export namespace std {
    template<>
    struct hash<epix::core::schedule::SystemSetLabel> {
        size_t operator()(const epix::core::schedule::SystemSetLabel& label) const {
            return label.label.hash();
        }
    };
}
