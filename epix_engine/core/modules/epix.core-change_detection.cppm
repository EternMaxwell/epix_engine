/**
 * @file epix.core-change_detection.cppm
 * @brief Change detection utilities partition
 */

export module epix.core:change_detection;

import :tick;
import :fwd;

#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

export namespace epix::core {
    template <typename T>
    struct Ref {
       private:
        const T* value;
        Tick added;
        Tick last_changed;

       public:
        Ref(const T* value, Tick added, Tick last_changed)
            : value(value), added(added), last_changed(last_changed) {}

        const T* operator->() const { return value; }
        const T& operator*() const { return *value; }
        
        Tick get_added() const { return added; }
        Tick get_last_changed() const { return last_changed; }
    };

    template <typename T>
    struct Mut {
       private:
        T* value;
        Tick* ticks;
        Tick last_run;
        Tick this_run;

       public:
        Mut(T* value, Tick* ticks, Tick last_run, Tick this_run)
            : value(value), ticks(ticks), last_run(last_run), this_run(this_run) {}

        T* operator->() { 
            ticks[1] = this_run;
            return value; 
        }
        T& operator*() { 
            ticks[1] = this_run;
            return *value; 
        }
        
        const T* operator->() const { return value; }
        const T& operator*() const { return *value; }
        
        bool is_added() const { return ticks[0].newer_than(last_run, this_run); }
        bool is_changed() const { return ticks[1].newer_than(last_run, this_run); }
    };

    template <typename T>
    struct Res {
        const T& value;
        
        Res(const T& value) : value(value) {}
        
        const T* operator->() const { return &value; }
        const T& operator*() const { return value; }
    };

    template <typename T>
    struct ResMut {
        T& value;
        
        ResMut(T& value) : value(value) {}
        
        T* operator->() { return &value; }
        T& operator*() { return value; }
        
        const T* operator->() const { return &value; }
        const T& operator*() const { return value; }
    };
}  // namespace epix::core
