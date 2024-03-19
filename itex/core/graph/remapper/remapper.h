/* Copyright (c) 2021-2022 Intel Corporation

Copyright 2018 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#ifndef ITEX_CORE_GRAPH_REMAPPER_REMAPPER_H_
#define ITEX_CORE_GRAPH_REMAPPER_REMAPPER_H_

#include <map>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

#include "itex/core/graph/utils/graph_properties.h"
#include "itex/core/graph/utils/graph_view.h"
#include "itex/core/graph/utils/grappler_item.h"
#include "itex/core/graph/utils/utils.h"
#include "itex/core/utils/onednn/onednn_post_op_util.h"
#include "itex/core/utils/tf_buffer.h"
#include "protos/graph.pb.h"

namespace itex {
namespace graph {

/* Indicate remapper fusion level:
 *   BASIC: Basic core op fusion without any variant
 *   ADVANCED: Advanced contracion fusion
 */
enum RemapperLevel : int { BASIC = 0, ADVANCED };

struct RemapperContext {
  explicit RemapperContext(const GrapplerItem& item, GraphDef* g_def,
                           Status* status, RemapperLevel level)
      : nodes_to_preserve(item.NodesToPreserve()),
        graph_view(g_def, status),
        graph_properties(item),
        inferred_graph_properties(false),
        remap_level(level) {}

  std::unordered_set<string> nodes_to_preserve;
  utils::MutableGraphView graph_view;
  GraphProperties graph_properties;
  bool inferred_graph_properties;
  RemapperLevel remap_level;

  GraphProperties& GetGraphProperties() {
    if (!inferred_graph_properties) {
      Status s = graph_properties.InferStatically(
          /*assume_valid_feeds=*/true,
          /*aggressive_shape_inference=*/false,
          /*include_input_tensor_values=*/true,
          /*include_output_tensor_values=*/true);

      // TODO(itex) Is there any case that InferStatically will return an
      // unsuccessful state?
      TF_ABORT_IF_ERROR(s);
      inferred_graph_properties = true;
    }

    return graph_properties;
  }
};

namespace {  // NOLINT

[[maybe_unused]] bool IsInPreserveSet(const RemapperContext& ctx,
                                      const NodeDef* node) {
  return ctx.nodes_to_preserve.count(node->name()) > 0;
}

[[maybe_unused]] bool HaveSameDataType(const NodeDef* lhs, const NodeDef* rhs,
                                       const string& type_attr = "T") {
  DataType lhs_attr = GetDataTypeFromAttr(*lhs, type_attr);
  DataType rhs_attr = GetDataTypeFromAttr(*rhs, type_attr);

  return lhs_attr != DT_INVALID && rhs_attr != DT_INVALID &&
         lhs_attr == rhs_attr;
}

// Returns true if the given pattern is supported on the assigned device.
// TODO(itex): Add device check for CPU/GPU/XPU
template <typename Pattern>
bool IsDeviceCompatible(const RemapperContext& ctx, const Pattern& matched) {
  return true;
}

[[maybe_unused]] bool IsSupportedActivation(const NodeDef& node) {
  return PostOpUtil::IsSupportedActivation(node.op());
}

[[maybe_unused]] bool HasControlFanin(const utils::MutableNodeView& node_view) {
  return node_view.NumControllingFanins() > 0;
}

[[maybe_unused]] bool HasControlFanout(
    const utils::MutableNodeView& node_view) {
  return node_view.NumControlledFanouts() > 0;
}

[[maybe_unused]] bool HasControlFaninOrFanout(
    const utils::MutableNodeView& node_view) {
  return node_view.NumControllingFanins() > 0 ||
         node_view.NumControlledFanouts() > 0;
}

// Returns true if at most one fanout reads output at port 0 (output used once).
inline bool HasAtMostOneFanoutAtPort0(const utils::MutableNodeView& node_view) {
  return node_view.GetRegularFanout(0).size() <= 1;
}

}  // namespace

bool HasDataType(const NodeDef* node, const DataType& expected,
                 const string& type_attr = "T");

void SetFusedOpAttributes(NodeDef* fused,
                          const absl::Span<const absl::string_view> fused_ops,
                          int num_args);

// Helper function to remove all regular Fanin from given node.
void RemoveAllRegularFanin(RemapperContext* ctx, int node_idx);

// `is_full` is true by default. It will be set as false if this pass runs
// before oneDNN Graph, that means only a few necessary fusions
// (InstanceNorm/LayerNorm) will be enabled to keep the original graph as
// complete as possible for oneDNN graph.
// `level` is to indicate current remapper fusion level. Simple fusions without
// any variant will be checked under BASIC(0) level only.
Status RunRemapper(OptimizerContext* opt_ctx, const GrapplerItem& item,
                   const GraphDef& graph_def, GraphDef* optimized_graph,
                   bool is_full = true,
                   RemapperLevel level = RemapperLevel::BASIC);

}  // namespace graph
}  // namespace itex

#endif  // ITEX_CORE_GRAPH_REMAPPER_REMAPPER_H_
