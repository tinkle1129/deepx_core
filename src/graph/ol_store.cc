// Copyright 2020 the deepx authors.
// Author: Yafei Zhang (kimmyzhang@tencent.com)
// Author: Chunchen Su (hillsu@tencent.com)
// Author: Shuting Guo (tinkleguo@tencent.com)
//

#include <deepx_core/common/stream.h>
#include <deepx_core/dx_log.h>
#include <deepx_core/graph/feature_kv_util.h>
#include <deepx_core/graph/ol_store.h>

namespace deepx_core {

void OLStore::Init(const Graph* graph, const TensorMap* param) noexcept {
  graph_ = graph;
  param_ = param;
}

bool OLStore::InitParam() {
  DXINFO("Initializing OLStore...");
  prev_param_ = *param_;
  DXINFO("Done.");
  return true;
}

void OLStore::Update(TensorMap* param) {
  for (const auto& entry : *param) {
    const std::string& name = entry.first;
    auto it = param_->find(name);
    if (it == param_->end()) {
      continue;
    }

    const Any& Wany = entry.second;
    if (it->second.is<srm_t>() && Wany.is<srm_t>()) {
      srm_state_t& srm_state = srm_state_map_[name];
      const auto& W = Wany.unsafe_to_ref<srm_t>();
      for (const auto& _entry : W) {
        ++srm_state[_entry.first].update;
      }
    }
  }
}

auto OLStore::Collect() -> id_set_t {
  DXINFO("Collecting ids...");
  id_set_t id_set;
  size_t updated = 0;
  size_t collected = 0;

  for (auto& entry : srm_state_map_) {
    const std::string& name = entry.first;
    srm_state_t& srm_state = entry.second;
    const Any& Wany = param_->at(name);
    Any& prev_Wany = prev_param_.at(name);

    if (Wany.is<srm_t>() && prev_Wany.is<srm_t>()) {
      const auto& W = Wany.unsafe_to_ref<srm_t>();
      auto& prev_W = prev_Wany.unsafe_to_ref<srm_t>();
      auto first = srm_state.begin();
      auto last = srm_state.end();
      for (; first != last;) {
        int_t id = first->first;
        const State& state = first->second;
        const float_t* embedding = W.get_row_no_init(id);
        const float_t* prev_embedding =
            ((const srm_t&)prev_W).get_row_no_init(id);
        ++updated;
        if (Collect(state, W.col(), embedding, prev_embedding)) {
          ++collected;
          id_set.emplace(id);
          prev_W.assign(id, embedding);
          first = srm_state.erase(first);
        } else {
          ++first;
        }
      }
    }
  }

  DXINFO("Updated %zu ids.", updated);
  DXINFO("Collected %zu ids.", collected);
  DXINFO("Collected %zu unique ids.", id_set.size());
  return id_set;
}

bool OLStore::Collect(const State& state, int n, const float_t* embedding,
                      const float_t* prev_embedding) const {
  if (prev_embedding == nullptr) {
    return true;
  }

  if (state.update > update_threshold_) {
    return true;
  }

  if (ll_math_t::euclidean_distance(n, embedding, prev_embedding) >
      distance_threshold_) {
    return true;
  }
  return false;
}

bool OLStore::SaveFeatureKVModel(const std::string& file,
                                 int feature_kv_protocol_version) {
  AutoOutputFileStream os;
  if (!os.Open(file)) {
    DXERROR("Failed to open: %s.", file.c_str());
    return false;
  }
  DXINFO("Saving feature kv model to %s...", file.c_str());
  id_set_t id_set = Collect();
  if (!FeatureKVUtil::WriteVersion(os, feature_kv_protocol_version) ||
      !FeatureKVUtil::WriteGraph(os, *graph_) ||
      !FeatureKVUtil::WriteDenseParam(os, *param_) ||
      !FeatureKVUtil::WriteSparseParam(os, *param_, *graph_, id_set,
                                       feature_kv_protocol_version)) {
    return false;
  }
  DXINFO("Done.");
  return true;
}

}  // namespace deepx_core