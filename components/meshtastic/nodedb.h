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
  meshtastic_NodeInfoLite *get_or_create(uint32_t num, bool *is_new);
  meshtastic_NodeInfoLite *find(uint32_t num);
  size_t size() const { return this->nodes_.size(); }
  const NodeVector &nodes() const { return this->nodes_; }

 protected:
  size_t max_nodes_{80};
  NodeVector nodes_;
};

}  // namespace meshtastic
}  // namespace esphome
