#include "nodedb.h"

#include "esphome/core/helpers.h"

#include <cstdint>

namespace esphome {
namespace meshtastic {

static constexpr size_t MIN_FREE_HEAP_TO_GROW = 8192;

meshtastic_NodeInfoLite *NodeDb::find(uint32_t num) {
  for (auto &n : this->nodes_) {
    if (n.num == num)
      return &n;
  }
  return nullptr;
}

meshtastic_NodeInfoLite *NodeDb::get_or_create(uint32_t num, bool *is_new) {
  meshtastic_NodeInfoLite *existing = this->find(num);
  if (existing != nullptr) {
    if (is_new != nullptr)
      *is_new = false;
    return existing;
  }
  if (is_new != nullptr)
    *is_new = true;

  meshtastic_NodeInfoLite fresh = meshtastic_NodeInfoLite_init_zero;
  fresh.num = num;

  const bool room =
      this->nodes_.size() < this->max_nodes_ && RAMAllocator<uint8_t>().get_free_heap_size() > MIN_FREE_HEAP_TO_GROW;
  if (!room && !this->nodes_.empty()) {
    // Full (count cap reached or heap low): evict the least-recently-heard non-favorite node (favorites,
    // e.g. YAML known_nodes, are pinned), reuse its slot. Only if every entry is a favorite fall back to
    // the oldest overall.
    size_t oldest = SIZE_MAX;
    for (size_t i = 0; i < this->nodes_.size(); i++) {
      if (this->nodes_[i].is_favorite)
        continue;
      if (oldest == SIZE_MAX || this->nodes_[i].last_heard < this->nodes_[oldest].last_heard)
        oldest = i;
    }
    if (oldest == SIZE_MAX) {
      oldest = 0;
      for (size_t i = 1; i < this->nodes_.size(); i++) {
        if (this->nodes_[i].last_heard < this->nodes_[oldest].last_heard)
          oldest = i;
      }
    }
    this->nodes_[oldest] = fresh;
    return &this->nodes_[oldest];
  }

  this->nodes_.push_back(fresh);
  return &this->nodes_.back();
}

}  // namespace meshtastic
}  // namespace esphome
