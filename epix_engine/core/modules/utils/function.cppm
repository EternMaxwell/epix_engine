export module epix.utils:function;

import std;

namespace epix::utils {
export template <typename Signature>
class function_ref;

template <typename R, typename... Args>
class function_ref<R(Args...)> {
   private:
    const void* m_ref;
    R (*m_invoke)(const void*, Args&&...);

   public:
    template <std::invocable<Args...> F>
        requires(!std::is_same_v<std::remove_cvref_t<F>, function_ref>) &&
                    std::same_as<std::invoke_result_t<F, Args...>, R>
    function_ref(const F& f)
        : m_ref(static_cast<const void*>(&f)), m_invoke([](const void* fn, Args&&... args) -> R {
              return std::invoke(*static_cast<const F*>(fn), std::forward<Args>(args)...);
          }) {}
    function_ref(const function_ref&)            = default;
    function_ref& operator=(const function_ref&) = default;

    R operator()(Args... args) const { return m_invoke(m_ref, std::forward<Args>(args)...); }
};

export template <typename Signature>
class function;

template <typename R, typename... Args>
struct function_base {
    virtual ~function_base()             = default;
    virtual function_base* clone() const = 0;
    virtual R invoke(Args&&... args)     = 0;
};
template <typename F, typename R, typename... Args>
struct function_impl : function_base<R, Args...> {
    F m_fn;
    function_impl(F fn) : m_fn(std::move(fn)) {}
    function_base<R, Args...>* clone() const override {
        if constexpr (std::copy_constructible<F>) {
            return new function_impl(m_fn);
        } else {
            return nullptr;  // cannot clone, return nullptr
        }
    }
    R invoke(Args&&... args) override { return std::invoke(m_fn, std::forward<Args>(args)...); }
};

template <typename R, typename... Args>
class function<R(Args...)> {
   private:
    std::unique_ptr<function_base<R, Args...>> m_impl;  // never empty

    function(std::unique_ptr<function_base<R, Args...>> impl) : m_impl(std::move(impl)) {}

   public:
    template <std::invocable<Args...> F>
        requires(!std::is_same_v<std::remove_cvref_t<F>, function>) && std::same_as<std::invoke_result_t<F, Args...>, R>
    function(F fn) : m_impl(std::make_unique<function_impl<F, R, Args...>>(std::move(fn))) {}
    function(const function& other)            = delete;
    function& operator=(const function& other) = delete;
    function(function&&) noexcept              = default;
    function& operator=(function&&) noexcept   = default;
    std::optional<function> clone() const {
        auto cloned_impl = m_impl->clone();
        if (cloned_impl) {
            return function(std::unique_ptr<function_base<R, Args...>>(cloned_impl));
        } else {
            return std::nullopt;  // cannot clone
        }
    }

    R operator()(Args... args) const {
        if (!m_impl) throw std::bad_function_call();
        return m_impl->invoke(std::forward<Args>(args)...);
    }
};
}  // namespace utils