#include "epix/assets/index.h"

using namespace epix::assets;

EPIX_API AssetIndex::AssetIndex(uint32_t index, uint32_t generation)
    : index(index), generation(generation) {}

EPIX_API bool AssetIndex::operator==(const AssetIndex& other) const {
    return index == other.index && generation == other.generation;
}
EPIX_API bool AssetIndex::operator!=(const AssetIndex& other) const {
    return !(*this == other);
}

EPIX_API AssetIndexAllocator::AssetIndexAllocator() {
    std::tie(m_free_indices_sender, m_free_indices_receiver) =
        epix::utils::async::make_channel<AssetIndex>();
    std::tie(m_reserved_sender, m_reserved) =
        epix::utils::async::make_channel<AssetIndex>();
}
EPIX_API AssetIndex AssetIndexAllocator::reserve() {
    if (auto index = m_free_indices_receiver.try_receive()) {
        m_reserved_sender.send(AssetIndex(index->index, index->generation + 1u)
        );
        return AssetIndex(index->index, index->generation + 1);
    } else {
        uint32_t i = m_next.fetch_add(1, std::memory_order_relaxed);
        m_reserved_sender.send(AssetIndex(i, 0));
        return AssetIndex(i, 0);
    }
}
EPIX_API Receiver<AssetIndex> AssetIndexAllocator::reserved_receiver() const {
    return m_reserved;
}
EPIX_API void AssetIndexAllocator::release(const AssetIndex& index) {
    m_free_indices_sender.send(index);
}