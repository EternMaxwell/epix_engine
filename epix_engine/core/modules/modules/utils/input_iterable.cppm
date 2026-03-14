export module epix.utils:input_iterable;

import std;

export namespace utils {
template <typename T>
class input_iterable : public std::ranges::view_interface<input_iterable<T>> {
    struct iter_base {
        virtual ~iter_base()      = default;
        virtual bool done() const = 0;
        virtual T get() const     = 0;
        virtual void advance()    = 0;
    };

    struct factory_base {
        virtual ~factory_base()                         = default;
        virtual std::unique_ptr<iter_base> make() const = 0;
    };

    template <typename I, typename S>
    struct iter_model final : iter_base {
        I it;
        [[no_unique_address]] S sent;

        iter_model(I i, S s) : it(std::move(i)), sent(std::move(s)) {}
        bool done() const override { return it == sent; }
        T get() const override { return static_cast<T>(*it); }
        void advance() override { ++it; }
    };

    template <typename R>
    struct owned_factory final : factory_base {
        R range;
        explicit owned_factory(R r) : range(std::move(r)) {}
        std::unique_ptr<iter_base> make() const override {
            using I = decltype(std::ranges::begin(range));
            using S = decltype(std::ranges::end(range));
            return std::make_unique<iter_model<I, S>>(std::ranges::begin(range), std::ranges::end(range));
        }
    };

    template <typename RR>
    struct borrowed_factory final : factory_base {
        RR* rng;
        explicit borrowed_factory(RR* r) : rng(r) {}
        std::unique_ptr<iter_base> make() const override {
            using I = decltype(std::ranges::begin(*rng));
            using S = decltype(std::ranges::end(*rng));
            return std::make_unique<iter_model<I, S>>(std::ranges::begin(*rng), std::ranges::end(*rng));
        }
    };

    std::shared_ptr<factory_base> factory_;

   public:
    using value_type = T;

    input_iterable()                                 = default;
    input_iterable(const input_iterable&)            = default;
    input_iterable& operator=(const input_iterable&) = default;

    template <std::ranges::input_range R>
        requires std::convertible_to<std::ranges::range_reference_t<std::remove_cvref_t<R>>, T>
    input_iterable(R&& range) {
        using RR = std::remove_cvref_t<R>;
        if constexpr (std::is_lvalue_reference_v<R>) {
            factory_ = std::make_shared<borrowed_factory<RR>>(std::addressof(range));
        } else {
            factory_ = std::make_shared<owned_factory<RR>>(std::forward<R>(range));
        }
    }

    struct sentinel {};

    struct iterator {
        using value_type       = T;
        using difference_type  = std::ptrdiff_t;
        using iterator_concept = std::input_iterator_tag;

        std::unique_ptr<iter_base> impl_;

        iterator() = default;
        explicit iterator(std::unique_ptr<iter_base> p) : impl_(std::move(p)) {}

        T operator*() const { return impl_->get(); }

        iterator& operator++() {
            impl_->advance();
            return *this;
        }
        void operator++(int) { impl_->advance(); }
        bool operator==(const sentinel&) const { return !impl_ || impl_->done(); }
    };

    iterator begin() const { return factory_ ? iterator{factory_->make()} : iterator{}; }
    sentinel end() const { return {}; }
};

}  // namespace utils