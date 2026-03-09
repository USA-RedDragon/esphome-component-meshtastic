#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "esphome/core/helpers.h"
#include "deviceonly.pb.h"

namespace esphome {
namespace meshtastic {

// Records live in external RAM (PSRAM) when available, falling back to internal heap otherwise.
using NodeVector = std::vector<meshtastic_NodeInfoLite, RAMAllocator<meshtastic_NodeInfoLite>>;

// Bounded table of heard nodes. Stops growing past the configured cap or when
// free heap runs low, evicting the least-recently-heard entry
class NodeDb {
 public:
  void set_max_nodes(size_t max_nodes) { this->max_nodes_ = max_nodes; }
  bool enabled() const { return this->max_nodes_ > 0; }
  meshtastic_NodeInfoLite *get_or_create(uint32_t num, bool *is_new);
  meshtastic_NodeInfoLite *find(uint32_t num);
  size_t size() const { return this->nodes_.size(); }
  const NodeVector &nodes() const { return this->nodes_; }

  size_t count_active(uint32_t now, uint32_t window_ms) const {
    size_t n = 0;
    for (const auto &nd : this->nodes_)
      if (now - nd.last_heard <= window_ms)
        n++;
    return n;
  }

  size_t count_neighbors() const {
    size_t n = 0;
    for (const auto &nd : this->nodes_)
      if (nd.has_hops_away && nd.hops_away == 0)
        n++;
    return n;
  }

 protected:
  size_t max_nodes_{80};
  NodeVector nodes_;
};

}  // namespace meshtastic
}  // namespace esphome
