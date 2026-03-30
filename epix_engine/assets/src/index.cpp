module;

#include <spdlog/spdlog.h>

module epix.assets;

import :index;

namespace epix::assets {

AssetIndexAllocator::AssetIndexAllocator() {
    std::tie(m_free_indices_sender, m_free_indices_receiver) = core::make_channel<AssetIndex>();
    std::tie(m_reserved_sender, m_reserved)                  = core::make_channel<AssetIndex>();
}
AssetIndex AssetIndexAllocator::reserve() const {
    if (auto index = m_free_indices_receiver.try_receive()) {
        spdlog::trace("[assets] AssetIndexAllocator::reserve: recycled index={}/gen={}.", index->index(),
                      index->generation() + 1u);
        m_reserved_sender.send(AssetIndex(index->index(), index->generation() + 1u));
        return AssetIndex(index->index(), index->generation() + 1);
    } else {
        uint32_t i = m_next.fetch_add(1, std::memory_order_relaxed);
        spdlog::trace("[assets] AssetIndexAllocator::reserve: new index={}/gen=0.", i);
        m_reserved_sender.send(AssetIndex(i, 0));
        return AssetIndex(i, 0);
    }
}
core::Receiver<AssetIndex> AssetIndexAllocator::reserved_receiver() const { return m_reserved; }
void AssetIndexAllocator::release(const AssetIndex& index) const {
    spdlog::trace("[assets] AssetIndexAllocator::release: index={}/gen={}.", index.index(), index.generation());
    m_free_indices_sender.send(index);
}
}  // namespace epix::assets
