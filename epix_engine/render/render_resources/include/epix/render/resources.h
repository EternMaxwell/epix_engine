#pragma once

#include <epix/app.h>
#include <epix/wgpu.h>

namespace epix::render::resources {
using RenderAdapter     = wgpu::Adapter;
using RenderAdapterInfo = wgpu::AdapterInfo;
using RenderInstance    = wgpu::Instance;
using RenderDevice      = wgpu::Device;
using RenderQueue       = wgpu::Queue;

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