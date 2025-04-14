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

template <typename T>
struct BufferTrackedVector {
   private:
    std::vector<T> data;
    wgpu::Buffer buffer;
    wgpu::BufferUsage usage;
    size_t capacity;
    std::optional<std::string> label;
    bool label_changed;

    std::vector<size_t> dirty_indices;
    std::vector<std::pair<size_t, size_t>> dirty_ranges;
    std::deque<size_t> free_dirty_indices;

    void mark_dirty(size_t index) {
        // check if index - 1 and index + 1 are dirty
        if (index > 0 && dirty_indices[index - 1] != -1 &&
            index + 1 < data.size() && dirty_indices[index + 1] != -1) {
            // both sides are dirty, merge the two ranges
            auto idx1                 = dirty_indices[index - 1];
            auto idx2                 = dirty_indices[index + 1];
            dirty_ranges[idx1].second = dirty_ranges[idx2].second;
            std::fill(
                dirty_indices.begin() + dirty_ranges[idx2].first,
                dirty_indices.begin() + dirty_ranges[idx2].second, idx1
            );
            dirty_ranges[idx2].first  = 0;
            dirty_ranges[idx2].second = 0;
            free_dirty_indices.push_back(idx2);
            return;
        } else if (index > 0 && dirty_indices[index - 1] != -1) {
            // left dirty
            dirty_ranges[dirty_indices[index - 1]].second = index + 1;
            dirty_indices[index] = dirty_indices[index - 1];
        } else if (index + 1 < data.size() && dirty_indices[index + 1] != -1) {
            // right dirty
            dirty_ranges[dirty_indices[index + 1]].first = index;
            dirty_indices[index] = dirty_indices[index + 1];
        } else {
            // new range
            if (free_dirty_indices.empty()) {
                dirty_ranges.emplace_back(index, index + 1);
                dirty_indices[index] = dirty_ranges.size() - 1;
            } else {
                auto idx = free_dirty_indices.front();
                free_dirty_indices.pop_front();
                dirty_ranges[idx]    = {index, index + 1};
                dirty_indices[index] = idx;
            }
        }
    }
    void mark_dirty(size_t start, size_t end) {  // end not inclusive
        if (start >= end) return;
        size_t cover_idx = 0;
        size_t cover_min = 0;
        size_t cover_max = 0;
        bool cover_any   = false;
        for (size_t x1 = start > 0 ? start - 1 : 0;
             x1 <= std::min(data.size(), end);) {
            if (dirty_indices[x1] != -1) {
                if (!cover_any) {
                    cover_idx = dirty_indices[x1];
                    cover_min = dirty_ranges[cover_idx].first;
                    cover_max = dirty_ranges[cover_idx].second;
                    end       = std::max(end, cover_max);
                    cover_any = true;
                } else {
                    end = std::max(end, dirty_ranges[dirty_indices[x1]].second);
                    dirty_ranges[dirty_indices[x1]].first  = 0;
                    dirty_ranges[dirty_indices[x1]].second = 0;
                    free_dirty_indices.push_back(dirty_indices[x1]);
                }
                x1 = dirty_ranges[dirty_indices[x1]].second;
            } else {
                x1++;
            }
        }
        if (!cover_any) {
            // no cover, create new range
            if (free_dirty_indices.empty()) {
                dirty_ranges.emplace_back(start, end);
                std::fill(
                    dirty_indices.begin() + start, dirty_indices.begin() + end,
                    dirty_ranges.size() - 1
                );
            } else {
                auto idx = free_dirty_indices.front();
                free_dirty_indices.pop_front();
                dirty_ranges[idx] = {start, end};
                std::fill(
                    dirty_indices.begin() + start, dirty_indices.begin() + end,
                    idx
                );
            }
        } else {
            cover_max = dirty_ranges[cover_idx].second;
            if (start < cover_min) {
                std::fill(
                    dirty_indices.begin() + start,
                    dirty_indices.begin() + cover_min, cover_idx
                );
                dirty_ranges[cover_idx].first = start;
            }
            if (end > cover_max) {
                std::fill(
                    dirty_indices.begin() + cover_max,
                    dirty_indices.begin() + end, cover_idx
                );
                dirty_ranges[cover_idx].second = end;
            }
        }
    }

   public:
    void reserve(size_t size) {
        if (size > capacity) {
            data.reserve(size);
            dirty_indices.reserve(size);
        }
    }
    void flush(RenderDevice device, RenderQueue queue) {
        if (capacity < data.capacity() || !buffer) {
            capacity = data.capacity();
            buffer   = device.createBuffer(WGPUBufferDescriptor{
                  .label = label ? WGPUStringView{label->c_str(), WGPU_STRLEN}
                                 : WGPUStringView{nullptr, 0},
                  .usage = usage | wgpu::BufferUsage::CopyDst,
                  .size  = capacity * sizeof(T),
            });
            queue.writeBuffer(buffer, 0, data.data(), data.size() * sizeof(T));
            label_changed = false;
            return;
        }
        if (label_changed) {
            buffer.setLabel(
                label ? WGPUStringView{label->c_str(), WGPU_STRLEN}
                      : WGPUStringView{nullptr, 0}
            );
        }
        for (auto&& [start, end] : dirty_ranges) {
            if (start == end) continue;
            queue.writeBuffer(
                buffer, start * sizeof(T), data.data() + start,
                (end - start) * sizeof(T)
            );
        }
        dirty_ranges.clear();
        free_dirty_indices.clear();
        std::fill(
            dirty_indices.begin(), dirty_indices.end(), -1
        );  // reset dirty indices
    }
    void write(size_t index, const T& value) {
        if (index >= data.size()) {
            data.resize(index + 1);
            dirty_indices.resize(index + 1, -1);
        }
        data[index] = value;
        mark_dirty(index);
    }
    void write(size_t index, T&& value) {
        if (index >= data.size()) {
            data.resize(index + 1);
            dirty_indices.resize(index + 1, -1);
        }
        data[index] = std::move(value);
        mark_dirty(index);
    }
    void write(size_t start, const T* values, size_t count) {
        if (start + count > data.size()) {
            data.resize(start + count);
            dirty_indices.resize(start + count, -1);
        }
        std::copy(values, values + count, data.begin() + start);
        mark_dirty(start, start + count);
    }
    void push(const T& value) {
        write(data.size(), value);
        mark_dirty(data.size() - 1);
    }
    void push(T&& value) {
        write(data.size(), std::move(value));
        mark_dirty(data.size() - 1);
    }
    void clear() {
        dirty_ranges.clear();
        free_dirty_indices.clear();
        dirty_ranges.emplace_back(0, data.size());
        data.clear();
        dirty_indices.clear();
    }
    void resize(size_t size) {
        size_t old_size = data.size();
        data.resize(size);
        dirty_indices.resize(size, -1);
        mark_dirty(old_size, size);
    }
    void resize(size_t size, const T& value) {
        size_t old_size = data.size();
        data.resize(size, value);
        dirty_indices.resize(size, -1);
        mark_dirty(old_size, size);
    }
};
template <typename T>
struct BufferVector {
   private:
    std::vector<T> data;
    wgpu::Buffer buffer;
    wgpu::BufferUsage usage;
    size_t capacity;
    std::optional<std::string> label;
    bool label_changed;

   public:
    void reserve(size_t size) {
        if (size > capacity) {
            data.reserve(size);
        }
    }
    void flush(RenderDevice device, RenderQueue queue) {
        if (capacity < data.capacity() || !buffer) {
            capacity = data.capacity();
            buffer   = device.createBuffer(WGPUBufferDescriptor{
                  .label = label ? WGPUStringView{label->c_str(), WGPU_STRLEN}
                                 : WGPUStringView{nullptr, 0},
                  .usage = usage | wgpu::BufferUsage::CopyDst,
                  .size  = capacity * sizeof(T),
            });
            queue.writeBuffer(buffer, 0, data.data(), data.size() * sizeof(T));
            label_changed = false;
            return;
        }
        if (label_changed) {
            buffer.setLabel(
                label ? WGPUStringView{label->c_str(), WGPU_STRLEN}
                      : WGPUStringView{nullptr, 0}
            );
        }
        queue.writeBuffer(buffer, 0, data.data(), data.size() * sizeof(T));
    }
    void write(size_t index, const T& value) {
        if (index >= data.size()) {
            data.resize(index + 1);
        }
        data[index] = value;
    }
    void write(size_t index, T&& value) {
        if (index >= data.size()) {
            data.resize(index + 1);
        }
        data[index] = std::move(value);
    }
    void write(size_t start, const T* values, size_t count) {
        if (start + count > data.size()) {
            data.resize(start + count);
        }
        std::copy(values, values + count, data.begin() + start);
    }
    void push(const T& value) { write(data.size(), value); }
    void push(T&& value) { write(data.size(), std::move(value)); }
    void clear() { data.clear(); }
    void resize(size_t size) { data.resize(size); }
    void resize(size_t size, const T& value) { data.resize(size, value); }
};
}  // namespace epix::render::resources