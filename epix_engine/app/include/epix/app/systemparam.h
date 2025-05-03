#pragma once

#include "commands.h"
#include "query.h"
#include "res.h"

namespace epix::app {
template <typename T>
struct Extract : public T {
    using value_type = T;
    using T::T;
    Extract(const T& t) : T(t) {}
    Extract(T&& t) : T(std::move(t)) {}
    Extract(const Extract& other) : T(other) {}
    Extract(Extract&& other) : T(std::move(other)) {}
    Extract& operator=(const T& other) {
        T::operator=(other);
        return *this;
    }
    Extract& operator=(T&& other) {
        T::operator=(std::move(other));
        return *this;
    }
    Extract& operator=(const Extract& other) {
        T::operator=(other);
        return *this;
    }
    Extract& operator=(Extract&& other) {
        T::operator=(std::move(other));
        return *this;
    }
};
template <>
struct Extract<World> {
    using value_type = World&;
    World& world;
    Extract(World& world) : world(world) {}

    World& operator*() { return world; }
    World* operator->() { return &world; }
    World& get() { return world; }
};

template <typename T>
struct Local {
    using value_type = T;
    Local(T* t) : t(t) {}
    T& operator*() { return *t; }
    T* operator->() { return t; }
    const T& operator*() const { return *t; }
    const T* operator->() const { return t; }

   private:
    T* t;
};

struct LocalData {
   private:
    entt::dense_map<std::type_index, std::shared_ptr<void>> m_locals;

   public:
    template <typename T>
    auto get() {
        auto it = m_locals.find(typeid(T));
        if (it == m_locals.end()) {
            m_locals.emplace(typeid(T), std::make_shared<T>());
            return m_locals.at(typeid(T));
        }
        return it->second;
    }
};

/**
 * @brief GetParam<T> struct is for getting system parameter from `World` and
 * from `LocalData`.
 *
 * The accepted types are types that have a constructor with `World&` argument
 * or `const World&` or types that have a static function `from_world(World&)`
 * that returns `std::optional<T>` or `T`.
 *
 * It should be guaranteed that T is decayed type.
 *
 * @tparam T The param type.
 */
template <typename T>
// requires(std::same_as<World> || OptFromWorld || FromWorld)
struct GetParam {};
template <>
struct GetParam<World> {
    using type = World&;
    static type get(World& src, World& dst, LocalData& locals) { return dst; }
};
template <OptFromWorld T>
struct GetParam<T> {
    using type = T;
    static type get(World& src, World& dst, LocalData& locals) {
        return *T::from_world(dst);
    }
};
template <FromWorld T>
struct GetParam<T> {
    using type = T;
    static type get(World& src, World& dst, LocalData& locals) {
        return T::from_world(dst);
    }
};
template <std::constructible_from<World&> T>
struct GetParam<T> {
    using type = T;
    static type get(World& src, World& dst, LocalData& locals) {
        return type(dst);
    }
};
template <typename T>
struct GetParam<Local<T>> {
    using type = Local<T>;
    static type get(World& src, World& dst, LocalData& locals) {
        return Local(reinterpret_cast<T*>(locals.get<T>().get()));
    }
};
template <typename T>
struct GetParam<Extract<T>> {
    using type = Extract<T>;
    static type get(World& src, World& dst, LocalData& locals) {
        return GetParam<T>::get(dst, src, locals);
    }
};

template <typename T>
concept ValidGetParam = requires {
    typename GetParam<T>::type;
    {
        GetParam<T>::get(
            std::declval<World&>(), std::declval<World&>(),
            std::declval<LocalData&>()
        )
    } -> std::same_as<typename GetParam<T>::type>;
};

template <typename T>
struct AddOptionalIfNot {
    using type = std::optional<T>;
};
template <typename T>
struct AddOptionalIfNot<std::optional<T>> {
    using type = std::optional<T>;
};

template <typename T>
struct FromParamT : std::false_type {};
template <typename F>
struct FunctionParam;
template <typename Ret, typename... Args>
struct FunctionParam<Ret(Args...)> {
    using return_type        = Ret;
    using param_type         = std::tuple<Args...>;
    using decayed_param_type = std::tuple<std::decay_t<Args>...>;
    using opt_param_type =
        std::tuple<typename AddOptionalIfNot<std::decay_t<Args>>::type...>;
    using ref_param_type = std::tuple<std::decay_t<Args>&...>;
    static constexpr bool all_from_param =
        ((FromParamT<typename AddOptionalIfNot<
              std::decay_t<Args>>::type::value_type>::value ||
          ValidGetParam<typename AddOptionalIfNot<
              std::decay_t<Args>>::type::value_type>) &&
         ...);
};
template <typename T>
    requires requires(T t) {
        std::same_as<
            std::optional<T>,
            typename FunctionParam<decltype(T::from_param)>::return_type> ||
            std::same_as<
                T,
                typename FunctionParam<decltype(T::from_param)>::return_type>;
    } && FunctionParam<decltype(T::from_param)>::all_from_param
struct FromParamT<T> : std::true_type {
    static constexpr bool opt = std::same_as<
        std::optional<T>,
        typename FunctionParam<decltype(T::from_param)>::return_type>;
};
template <typename T>
struct FromParamT<Extract<T>> : FromParamT<T> {};
template <typename T>
concept FromParam = FromParamT<T>::value;
template <typename T>
concept OptFromParam =
    epix::util::type_traits::specialization_of<T, std::optional> &&
    FromParam<typename T::value_type>;

template <typename T>
struct PrepareParam {
    static void prepare(T& t) {}
    static void unprepare(T& t) {}
};
template <typename T>
struct PrepareParam<Res<T>> {
    static void prepare(Res<T>& t) { t.lock(); }
    static void unprepare(Res<T>& t) { t.unlock(); }
};
template <typename T>
struct PrepareParam<ResMut<T>> {
    static void prepare(ResMut<T>& t) { t.lock(); }
    static void unprepare(ResMut<T>& t) { t.unlock(); }
};

struct SystemParamInfo {
    bool has_world    = false;
    bool has_commands = false;
    std::vector<std::tuple<
        entt::dense_set<std::type_index>,   // mutable types
        entt::dense_set<std::type_index>,   // const types
        entt::dense_set<std::type_index>>>  // exclude types
        query_types;
    entt::dense_set<std::type_index> resource_types;  // const resources
    entt::dense_set<std::type_index> resource_muts;   // mutable resources

    EPIX_API std::string to_string() const;
    EPIX_API bool conflict_with(const SystemParamInfo& other) const;
};

template <typename T>
struct WriteSystemParamInfo {
    static void write(SystemParamInfo&) {}
};
template <>
struct WriteSystemParamInfo<Commands> {
    static void write(SystemParamInfo& info) { info.has_commands = true; }
};
template <>
struct WriteSystemParamInfo<World> {
    static void write(SystemParamInfo& info) { info.has_world = true; }
};
template <typename T>
struct WriteSystemParamInfo<Res<T>> {
    static void write(SystemParamInfo& info) {
        info.resource_types.emplace(typeid(std::decay_t<T>));
    }
};
template <typename T>
struct WriteSystemParamInfo<ResMut<T>> {
    static void write(SystemParamInfo& info) {
        if constexpr (std::is_const_v<T>)
            info.resource_types.emplace(typeid(std::decay_t<T>));
        else
            info.resource_muts.emplace(typeid(std::decay_t<T>));
    }
};
template <typename G, typename F>
struct WriteSystemParamInfo<Query<G, F>> {
    template <size_t I>
    static void write_access_i(SystemParamInfo& info) {
        using access_type = typename Query<G, F>::access_type;
        using item_type   = std::tuple_element_t<I, access_type>;
        using decay_t     = std::decay_t<item_type>;
        if constexpr (std::is_const_v<item_type>) {
            std::get<1>(info.query_types.back()).emplace(typeid(decay_t));
        } else {
            std::get<0>(info.query_types.back()).emplace(typeid(decay_t));
        }
    }
    template <size_t... I>
    static void write_access_tuple(
        std::index_sequence<I...>, SystemParamInfo& info
    ) {
        (write_access_i<I>(info), ...);
    }
    template <size_t... I>
    static void write_exclude_tuple(
        std::index_sequence<I...>, SystemParamInfo& info
    ) {
        (std::get<2>(info.query_types.back())
             .emplace(typeid(std::decay_t<std::tuple_element_t<
                                 I, typename Query<G, F>::must_exclude>>)),
         ...);
    }
    static void write(SystemParamInfo& info) {
        info.query_types.emplace_back(
            entt::dense_set<std::type_index>{},
            entt::dense_set<std::type_index>{},
            entt::dense_set<std::type_index>{}
        );
        write_access_tuple(
            std::make_index_sequence<
                std::tuple_size<typename Query<G, F>::access_type>::value>{},
            info
        );
        write_exclude_tuple(
            std::make_index_sequence<
                std::tuple_size<typename Query<G, F>::must_exclude>::value>{},
            info
        );
    }
};

struct BadParamAccess : public std::exception {
    std::string msg;
    BadParamAccess() { msg = "Error when trying to access system parameter!"; }
    const char* what() const noexcept override { return msg.c_str(); }
};

template <typename T>
struct BadSystemParamParseException : public BadParamAccess {
    BadSystemParamParseException() {
        msg = "Error when trying to get system parameter of type " +
              std::string(typeid(T).name()) +
              "! Can not create param from world!";
    }
    const char* what() const noexcept override { return msg.c_str(); }
};

template <typename T>
struct SystemParam {
    static_assert(false, "Invalid type for SystemParam.");
};
template <typename T>
    requires FromWorld<T> || OptFromWorld<T>
struct SystemParam<T> {
    std::optional<T> param;
    void reset() { param.reset(); }
    void complete(World& src, World& dst, LocalData& locals) {
        if (!param) {
            if constexpr (std::constructible_from<T, World&>) {
                param.emplace(dst);
            } else {
                param = T::from_world(dst);
            }
        }
    }
    T& get() {
        if (!param) {
            throw BadSystemParamParseException<T>();
        }
        return param.value();
    }
    bool valid() { return param.has_value(); }
    void prepare() {
        if (valid()) {
            PrepareParam<T>::prepare(param.value());
        }
    }
    void unprepare() {
        if (valid()) {
            PrepareParam<T>::unprepare(param.value());
        }
    }
    static void write_info(SystemParamInfo& src, SystemParamInfo& dst) {
        WriteSystemParamInfo<T>::write(dst);
    }
};
template <typename T>
struct SystemParam<Local<T>> {
    std::optional<Local<T>> param;
    void reset() { param.reset(); }
    void complete(World& src, World& dst, LocalData& locals) {
        if (!param) {
            param = Local(reinterpret_cast<T*>(locals.get<T>().get()));
        }
    }
    bool valid() { return param.has_value(); }
    Local<T>& get() {
        if (!param) {
            throw BadSystemParamParseException<Local<T>>();
        }
        return param.value();
    }
    void prepare() {
        if (valid()) {
            PrepareParam<Local<T>>::prepare(param.value());
        }
    }
    void unprepare() {
        if (valid()) {
            PrepareParam<Local<T>>::unprepare(param.value());
        }
    }
    static void write_info(SystemParamInfo& src, SystemParamInfo& dst) {
        WriteSystemParamInfo<Local<T>>::write(dst);
    }
};
template <typename... Args>
struct SystemParam<std::tuple<Args...>>
    : public std::tuple<SystemParam<Args>...> {
    template <size_t... I>
    std::tuple<Args&...> get(std::index_sequence<I...>) {
        return std::tuple<Args&...>(std::get<I>(*this).get()...);
    }
    std::tuple<Args&...> get() {
        return get(std::index_sequence_for<Args...>{});
    }
    template <size_t... I>
    void
    complete(World& src, World& dst, LocalData& locals, std::index_sequence<I...>) {
        (std::get<I>(*this).complete(src, dst, locals), ...);
    }
    void complete(World& src, World& dst, LocalData& locals) {
        complete(src, dst, locals, std::index_sequence_for<Args...>{});
    }
    template <size_t... I>
    void reset(std::index_sequence<I...>) {
        (std::get<I>(*this).reset(), ...);
    }
    void reset() { reset(std::index_sequence_for<Args...>{}); }
    template <size_t... I>
    bool valid(std::index_sequence<I...>) {
        return (std::get<I>(*this).valid() && ...);
    }
    bool valid() { return valid(std::index_sequence_for<Args...>{}); }
    template <size_t... I>
    void prepare(std::index_sequence<I...>) {
        (std::get<I>(*this).prepare(), ...);
    }
    void prepare() { prepare(std::index_sequence_for<Args...>{}); }
    template <size_t... I>
    void unprepare(std::index_sequence<I...>) {
        (std::get<I>(*this).unprepare(), ...);
    }
    void unprepare() { unprepare(std::index_sequence_for<Args...>{}); }
    static void write_info(SystemParamInfo& src, SystemParamInfo& dst) {
        (SystemParam<Args>::write_info(src, dst), ...);
    }
};
template <typename T>
    requires FromParam<T>
struct SystemParam<T> {
    SystemParam<
        typename FunctionParam<decltype(T::from_param)>::decayed_param_type>
        from;
    std::optional<T> param;
    T& get() {
        if (!param) {
            throw BadSystemParamParseException<std::optional<T>>();
        }
        return param.value();
    }
    void complete(World& src, World& dst, LocalData& locals) noexcept {
        from.complete(src, dst, locals);
        if (!param && from.valid()) {
            static_assert(
                std::move_constructible<T>,
                "T must be move constructible to be used as system parameter"
            );
            using return_type = decltype(std::apply(T::from_param, from.get()));
            if constexpr (epix::util::type_traits::specialization_of<
                              return_type, std::optional>) {
                auto res = std::apply(T::from_param, from.get());
                if (res) {
                    param.emplace(std::move(res.value()));
                }
            } else {
                param.emplace(std::apply(T::from_param, from.get()));
            }
        }
    }
    void reset() {
        param.reset();
        from.reset();
    }
    bool valid() { return param.has_value(); }
    void prepare() {
        from.prepare();
        if (valid()) {
            PrepareParam<T>::prepare(param.value());
        }
    }
    void unprepare() {
        from.unprepare();
        if (valid()) {
            PrepareParam<T>::unprepare(param.value());
        }
    }
    static void write_info(SystemParamInfo& src, SystemParamInfo& dst) {
        WriteSystemParamInfo<T>::write(dst);
        SystemParam<typename FunctionParam<decltype(T::from_param
        )>::decayed_param_type>::write_info(src, dst);
    }
};
template <typename T>
struct SystemParam<Extract<T>> : public SystemParam<T> {
    std::optional<Extract<T>> param;
    void complete(World& src, World& dst, LocalData& locals) {
        SystemParam<T>::complete(dst, src, locals);
        if (!param && SystemParam<T>::valid()) {
            if constexpr (std::copyable<T>) {
                param.emplace(SystemParam<T>::get());
            } else if constexpr (std::movable<T>) {
                param.emplace(std::move(SystemParam<T>::get()));
            } else {
                static_assert(
                    false, "T is not copyable or movable, cannot extract"
                );
            }
        }
    }
    Extract<T>& get() {
        if (!param) {
            throw BadSystemParamParseException<Extract<T>>();
        }
        return param.value();
    }
    static void write_info(SystemParamInfo& src, SystemParamInfo& dst) {
        SystemParam<T>::write_info(dst, src);
    }
};
template <>
struct SystemParam<World> {
    World* param;
    void reset() { param = nullptr; }
    void complete(World& src, World& dst, LocalData& locals) {
        if (!param) {
            param = &dst;
        }
    }
    World& get() { return *param; }
    bool valid() { return param != nullptr; }
    void prepare() {}
    void unprepare() {}
    static void write_info(SystemParamInfo& src, SystemParamInfo& dst) {
        WriteSystemParamInfo<World>::write(dst);
    }
};
template <>
struct SystemParam<Extract<World>> : public SystemParam<World> {
    std::optional<Extract<World>> param;
    void complete(World& src, World& dst, LocalData& locals) {
        SystemParam<World>::complete(dst, src, locals);
        if (!param && SystemParam<World>::valid()) {
            param.emplace(SystemParam<World>::get());
        }
    }
    Extract<World>& get() {
        if (!param) {
            throw BadSystemParamParseException<Extract<World>>();
        }
        return param.value();
    }
    static void write_info(SystemParamInfo& src, SystemParamInfo& dst) {
        SystemParam<World>::write_info(dst, src);
    }
};
template <typename T>
struct SystemParam<Extract<Extract<T>>> : public SystemParam<T> {
    std::optional<Extract<Extract<T>>> param;
    void complete(World& src, World& dst, LocalData& locals) {
        SystemParam<T>::complete(dst, src, locals);
        if (!param && SystemParam<T>::valid()) {
            param.emplace(SystemParam<T>::get());
        }
    }
    Extract<Extract<T>>& get() {
        if (!param) {
            throw BadSystemParamParseException<Extract<Extract<T>>>();
        }
        return param.value();
    }
    bool valid() { return param.has_value(); }
};
template <typename T>
struct SystemParam<std::optional<T>> : public SystemParam<T> {
    bool valid() { return true; }
    std::optional<T>& get() { return SystemParam<T>::param; }
    static void write_info(SystemParamInfo& src, SystemParamInfo& dst) {
        SystemParam<T>::write_info(src, dst);
    }
};
}  // namespace epix::app