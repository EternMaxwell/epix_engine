export module epix.utils:func_ref;

import std;

namespace utils {
export template <typename Signature>
class function_ref;

template <typename R, typename... Args>
class function_ref<R(Args...)> {
   private:
    const void* m_callable;
    R (*m_invoke)(void*, Args&&...);

   public:
    template <std::invocable<Args...> F>
        requires(!std::is_same_v<std::remove_cvref_t<F>, function_ref>) &&
                    std::same_as<std::invoke_result_t<F, Args...>, R>
    function_ref(const F& f)
        : m_callable(static_cast<const void*>(&f)), m_invoke([](void* callable, Args&&... args) -> R {
              return std::invoke(*static_cast<const F*>(callable), std::forward<Args>(args)...);
          }) {}
    function_ref(const function_ref&)            = default;
    function_ref& operator=(const function_ref&) = default;

    R operator()(Args... args) const { return m_invoke(m_callable, std::forward<Args>(args)...); }
};
}  // namespace utils