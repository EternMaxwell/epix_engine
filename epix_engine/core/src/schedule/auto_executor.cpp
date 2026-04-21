module;

#ifndef EPIX_IMPORT_STD
#include <algorithm>
#include <cstddef>
#include <format>
#include <limits>
#include <memory>
#include <random>
#include <ranges>
#include <utility>
#include <vector>
#include <chrono>
#endif
#include <spdlog/spdlog.h>

module epix.core;
#ifdef EPIX_IMPORT_STD
import std;
#endif
namespace epix::core::executors {
struct AutoExecutor::Impl {
    std::vector<std::pair<std::unique_ptr<ScheduleExecutor>, double>> m_executors;
    std::vector<std::size_t> used_time;
    std::size_t last_index = std::numeric_limits<std::size_t>::max();

    Impl() {
        m_executors.emplace_back(std::make_unique<TaskflowExecutor>(), 0.0);
        m_executors.emplace_back(std::make_unique<MultithreadFlatExecutor>(), 0.0);
        m_executors.emplace_back(std::make_unique<MultithreadClassicExecutor>(), 0.0);
        m_executors.emplace_back(std::make_unique<SingleThreadExecutor>(), 0.0);
    }

    std::size_t pick_executor() {
        // random pick, but executors with lower timecost will have more possibility
        static thread_local std::random_device rd{};
        static thread_local std::mt19937_64 rng(rd());

        if (m_executors.empty()) {
            return 0;
        }

        // Build weights inversely proportional to recorded time (lower time -> higher weight)
        const double eps = 1e-6;
        std::vector<double> weights;
        weights.reserve(m_executors.size());
        double min_t = std::min_element(m_executors.begin(), m_executors.end(), [](const auto& a, const auto& b) {
                           return a.second < b.second;
                       })->second;
        for (auto const& p : m_executors) {
            double t = p.second - min_t;
            double w = 1.0 / (t + eps);
            weights.push_back(w);
        }

        std::discrete_distribution<std::size_t> dist(weights.begin(), weights.end());
        return dist(rng);
    }
    void execute(const ScheduleLabel& label, ScheduleSystems& schedule, World& world, const ExecutorConfig& config) {
        auto index = pick_executor();
        if (last_index != index) {
            spdlog::trace("[schedule] AutoExecutor '{}' switching to {}.", label.to_string(),
                          m_executors[index].first->type().name());
            last_index = index;
        }
        if (used_time.size() <= index) {
            used_time.resize(index + 1);
        }
        used_time[index]++;
        auto& exec = m_executors[index].first;
        auto start = std::chrono::high_resolution_clock::now();
        exec->execute(schedule, world, config);
        auto end        = std::chrono::high_resolution_clock::now();
        double duration = std::chrono::duration_cast<std::chrono::duration<double>>(end - start).count();  // in seconds
        // update the average execution time using exponential moving average
        m_executors[index].second = 0.9 * m_executors[index].second + 0.1 * duration;
    }
};

AutoExecutor::AutoExecutor() : m_impl(std::make_unique<Impl>()) {}
AutoExecutor::~AutoExecutor() {
    spdlog::debug("[schedule] AutoExecutor '{}' usage stats: {}", label.to_string(),
                  std::views::transform(std::views::enumerate(m_impl->used_time), [&](auto&& pair) {
                      auto&& [index, count] = pair;
                      return std::format("{}: {} times", index, count);
                  }));
}
AutoExecutor::AutoExecutor(AutoExecutor&&) noexcept            = default;
AutoExecutor& AutoExecutor::operator=(AutoExecutor&&) noexcept = default;
void AutoExecutor::execute(ScheduleSystems& schedule, World& world, const ExecutorConfig& config) {
    if (!m_impl) {
        m_impl = std::make_unique<Impl>();
    }
    m_impl->execute(label, schedule, world, config);
}
}  // namespace epix::core::executors