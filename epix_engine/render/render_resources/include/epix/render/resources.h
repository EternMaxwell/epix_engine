#pragma once

#include <epix/app.h>
#include <epix/wgpu.h>

namespace epix::render::resources {
template <typename T>
class ArrayProxy {
   public:
    constexpr ArrayProxy() noexcept : m_count(0), m_ptr(nullptr) {}

    constexpr ArrayProxy(std::nullptr_t) noexcept
        : m_count(0), m_ptr(nullptr) {}

    ArrayProxy(T const& value) noexcept : m_count(1), m_ptr(&value) {}

    ArrayProxy(uint32_t count, T const* ptr) noexcept
        : m_count(count), m_ptr(ptr) {}

    template <std::size_t C>
    ArrayProxy(T const (&ptr)[C]) noexcept : m_count(C), m_ptr(ptr) {}

#if __GNUC__ >= 9
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winit-list-lifetime"
#endif

    ArrayProxy(std::initializer_list<T> const& list) noexcept
        : m_count(static_cast<uint32_t>(list.size())), m_ptr(list.begin()) {}

    template <
        typename B                                                  = T,
        typename std::enable_if<std::is_const<B>::value, int>::type = 0>
    ArrayProxy(
        std::initializer_list<typename std::remove_const<T>::type> const& list
    ) noexcept
        : m_count(static_cast<uint32_t>(list.size())), m_ptr(list.begin()) {}

#if __GNUC__ >= 9
#pragma GCC diagnostic pop
#endif

    // Any type with a .data() return type implicitly convertible to T*, and a
    // .size() return type implicitly convertible to size_t. The const version
    // can capture temporaries, with lifetime ending at end of statement.
    template <
        typename V,
        typename std::enable_if<
            std::is_convertible<decltype(std::declval<V>().data()), T*>::
                value &&
            std::is_convertible<
                decltype(std::declval<V>().size()),
                std::size_t>::value>::type* = nullptr>
    ArrayProxy(V const& v) noexcept
        : m_count(static_cast<uint32_t>(v.size())), m_ptr(v.data()) {}

    const T* begin() const noexcept { return m_ptr; }

    const T* end() const noexcept { return m_ptr + m_count; }

    const T& front() const noexcept {
        VULKAN_HPP_ASSERT(m_count && m_ptr);
        return *m_ptr;
    }

    const T& back() const noexcept {
        assert(m_count && m_ptr);
        return *(m_ptr + m_count - 1);
    }

    bool empty() const noexcept { return (m_count == 0); }

    uint32_t size() const noexcept { return m_count; }

    T const* data() const noexcept { return m_ptr; }

   private:
    uint32_t m_count;
    T const* m_ptr;
};
struct RenderInstance : public wgpu::Instance {
    RenderInstance() = default;
    RenderInstance(wgpu::Instance instance) : wgpu::Instance(instance) {}
    RenderInstance(const RenderInstance&)            = default;
    RenderInstance(RenderInstance&&)                 = default;
    RenderInstance& operator=(const RenderInstance&) = default;
    RenderInstance& operator=(RenderInstance&&)      = default;
    ~RenderInstance()                                = default;
};
struct RenderAdapterInfo : public wgpu::AdapterInfo {
    RenderAdapterInfo() = default;
    RenderAdapterInfo(wgpu::AdapterInfo info) : wgpu::AdapterInfo(info) {}
    RenderAdapterInfo(const RenderAdapterInfo&)            = default;
    RenderAdapterInfo(RenderAdapterInfo&&)                 = default;
    RenderAdapterInfo& operator=(const RenderAdapterInfo&) = default;
    RenderAdapterInfo& operator=(RenderAdapterInfo&&)      = default;
    ~RenderAdapterInfo()                                   = default;
};
struct RenderAdapter : public wgpu::Adapter {
    RenderAdapter() = default;
    RenderAdapter(wgpu::Adapter adapter) : wgpu::Adapter(adapter) {}
    RenderAdapter(const RenderAdapter&)            = default;
    RenderAdapter(RenderAdapter&&)                 = default;
    RenderAdapter& operator=(const RenderAdapter&) = default;
    RenderAdapter& operator=(RenderAdapter&&)      = default;
    ~RenderAdapter()                               = default;
};
using BindGroup = wgpu::BindGroup;
struct BindGroupDescriptor;
struct BindingResouce;
struct BindGroupEntry;
struct RenderDevice : public wgpu::Device {
    RenderDevice() = default;
    RenderDevice(wgpu::Device device) : wgpu::Device(device) {}
    RenderDevice(const RenderDevice&)            = default;
    RenderDevice(RenderDevice&&)                 = default;
    RenderDevice& operator=(const RenderDevice&) = default;
    RenderDevice& operator=(RenderDevice&&)      = default;
    ~RenderDevice()                              = default;

    EPIX_API BindGroup createBindGroup(
        wgpu::BindGroupLayout layout, ArrayProxy<BindGroupEntry> bindings
    );
};
struct RenderQueue : public wgpu::Queue {
    RenderQueue() = default;
    RenderQueue(wgpu::Queue queue) : wgpu::Queue(queue) {}
    RenderQueue(const RenderQueue&)            = default;
    RenderQueue(RenderQueue&&)                 = default;
    RenderQueue& operator=(const RenderQueue&) = default;
    RenderQueue& operator=(RenderQueue&&)      = default;
    ~RenderQueue()                             = default;
};
struct BufferBinding {
    wgpu::Buffer buffer;
    uint64_t offset = 0;
    uint64_t size   = 0;
};
using Sampler     = wgpu::Sampler;
using TextureView = wgpu::TextureView;
using Buffer      = wgpu::Buffer;
struct BindingResouce {
    std::variant<
        BufferBinding,
        std::vector<Buffer>,
        Sampler,
        std::vector<Sampler>,
        TextureView,
        std::vector<TextureView>>
        resource;

    BindingResouce(Buffer buffer, uint64_t offset = 0, uint64_t size = 0) {
        resource =
            BufferBinding{buffer, offset, size ? size : buffer.getSize()};
    }
    BindingResouce(ArrayProxy<Buffer> buffers) {
        resource = std::vector<Buffer>(
            buffers.data(), buffers.data() + buffers.size()
        );
    }
    BindingResouce(Sampler sampler) { resource = sampler; }
    BindingResouce(ArrayProxy<Sampler> samplers) {
        resource = std::vector<Sampler>(
            samplers.data(), samplers.data() + samplers.size()
        );
    }
    BindingResouce(TextureView view) { resource = view; }
    BindingResouce(ArrayProxy<TextureView> views) {
        resource =
            std::vector<TextureView>(views.data(), views.data() + views.size());
    }
};
struct BindGroupEntry {
    uint32_t binding;
    BindingResouce resource;
};
// Helper for creating bind group entries
struct BindGroupEntries {
    template <typename... Args>
    static std::array<BindGroupEntry, sizeof...(Args)> sequence(Args&&... args
    ) {
        std::array<BindGroupEntry, sizeof...(Args)> entries{
            BindGroupEntry{0, BindingResouce(std::forward<Args>(args))}...
        };
        for (size_t index = 0; index < sizeof...(Args); index++) {
            entries[index].binding = index;
        }
        return std::move(entries);
    }
    template <typename T, typename... Args>
        requires std::constructible_from<BindingResouce, T>
    static std::vector<BindGroupEntry> with_indices(
        uint32_t index, T&& resource, Args&&... args
    ) {
        std::vector<BindGroupEntry> entries{BindGroupEntry{
            static_cast<uint32_t>(index),
            BindingResouce(std::forward<T>(resource))
        }};
        if constexpr (sizeof...(args) > 0) {
            auto appendees = with_indices(std::forward<Args>(args)...);
            entries.insert(entries.end(), appendees.begin(), appendees.end());
        }
        return std::move(entries);
    }
};
}  // namespace epix::render::resources