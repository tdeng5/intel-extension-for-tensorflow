/* Copyright (c) 2022 Intel Corporation

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

#include "itex/core/graph/remapper/constant_names.h"
#include "itex/core/graph/remapper/fusion.h"
#include "itex/core/graph/remapper/remapper.h"
#include "itex/core/graph/utils/layout_utils.h"
#include "itex/core/graph/utils/pattern_utils.h"
#include "itex/core/graph/utils/symbolic_shapes.h"
#include "itex/core/graph/utils/utils.h"
#include "itex/core/utils/op_kernel.h"

namespace itex {
namespace graph {

class BatchMatMulFusion : public Fusion {
 public:
  bool CheckMul(RemapperContext* ctx, int index) const {
    auto properties = GetOutputProperties(ctx, index);
    return !properties.empty() && NumCoefficients(properties[0].shape()) == 1;
  }

  bool CheckBatchMatmul(RemapperContext* ctx, int index) const {
    auto properties = GetOutputProperties(ctx, index);
    auto node_def = ctx->graph_view.GetNode(index)->node();
    // TODO(itex) only support for CPU now due to performance issue on GPU.
    return !properties.empty() && Rank(properties[0].shape()) == 4 &&
           !NodeIsOnGpu(node_def);
  }

  bool CheckAdd(RemapperContext* ctx, int index) const {
    auto properties = GetOutputProperties(ctx, index);
    return !properties.empty() && Rank(properties[0].shape()) == 4 &&
           properties[0].shape().dim(1).size() == 1;
  }
};

class BatchMatMulWithMulAndAddV2Fusion : public BatchMatMulFusion {
 public:
  BatchMatMulWithMulAndAddV2Fusion() : BatchMatMulFusion() {
    using utils::NodeStatus;
    using utils::OpTypePattern;

    OpTypePattern multiplicand = {kAny, "multiplicand", NodeStatus::kRemain};
    OpTypePattern batch_matmul = {kBatchMatMulV2, "batch_matmul",
                                  NodeStatus::kRemove};
    OpTypePattern mul = {kMul, "mul", NodeStatus::kRemove};
    OpTypePattern addend = {kAny, "addend", NodeStatus::kRemain};
    OpTypePattern output = {kAddV2, "output", NodeStatus::kReplace};

    mul.AddInput(batch_matmul).AddInput(multiplicand);
    output.AddInput(mul).AddInput(addend);

    pattern_ = InternalPattern(std::move(output));
  }

  ~BatchMatMulWithMulAndAddV2Fusion() {}

  std::string Name() override { return "batchmatmulv2-with-mul-addv2"; }

  MatchedProperties Check(RemapperContext* ctx,
                          const int node_index) const override {
    auto& graph_view = ctx->graph_view;
    MatchedProperties ret =
        FillProperties(&graph_view, graph_view.GetNode(node_index), pattern_);

    bool is_ok = !ret.Empty() && CheckMul(ctx, ret.map.at("multiplicand")) &&
                 CheckBatchMatmul(ctx, ret.map.at("batch_matmul")) &&
                 CheckAdd(ctx, ret.map.at("addend"));

    if (!is_ok) return ret.ToEmpty();

    return ret;
  }

  Status Update(RemapperContext* ctx /** in and out **/,
                const MatchedProperties& properties) const override {
    auto* output_node =
        ctx->graph_view.GetNode(properties.map.at("output"))->node();
    auto* batch_matmul_node =
        ctx->graph_view.GetNode(properties.map.at("batch_matmul"))->node();
    auto* multiplicand_node =
        ctx->graph_view.GetNode(properties.map.at("multiplicand"))->node();
    auto* addend_node =
        ctx->graph_view.GetNode(properties.map.at("addend"))->node();

    NodeDef fused_node;
    fused_node.set_name(output_node->name());
    fused_node.set_op(kFusedBatchMatMul);
    fused_node.set_device(batch_matmul_node->device());
    fused_node.add_input(batch_matmul_node->input(0));
    fused_node.add_input(batch_matmul_node->input(1));
    fused_node.add_input(multiplicand_node->name());
    fused_node.add_input(addend_node->name());

    CopyAllAttrs(*batch_matmul_node, &fused_node);
    SetFusedOpAttributes(&fused_node, {kBinaryMul, kBinaryAdd}, /*num_args=*/2);

    utils::Mutation* mutation = ctx->graph_view.GetMutationBuilder();
    Status status;
    mutation->AddNode(std::move(fused_node), &status);
    TF_RETURN_IF_ERROR(status);
    TF_RETURN_IF_ERROR(mutation->Apply());
    return Status::OK();
  }
};

class BatchMatMulWithMulAndAddFusion : public BatchMatMulFusion {
 public:
  BatchMatMulWithMulAndAddFusion() : BatchMatMulFusion() {
    using utils::NodeStatus;
    using utils::OpTypePattern;

    OpTypePattern mul_input0 = {kAny, "mul_input0", NodeStatus::kRemain};
    OpTypePattern multiplicand = {kConst, "multiplicand", NodeStatus::kRemain};
    OpTypePattern mul = {kMul, "mul", NodeStatus::kRemove};
    OpTypePattern bmm_input1 = {kAny, "bmm_input1", NodeStatus::kRemain};
    OpTypePattern addend = {kAny, "addend", NodeStatus::kRemain};
    OpTypePattern batch_matmul = {kBatchMatMulV2, "batch_matmul",
                                  NodeStatus::kRemove};
    OpTypePattern output = {kAdd, "output", NodeStatus::kReplace};

    mul.AddInput(multiplicand).AddInput(mul_input0);
    batch_matmul.AddInput(mul).AddInput(bmm_input1);
    output.AddInput(batch_matmul).AddInput(addend);

    pattern_ = InternalPattern(std::move(output));
  }

  ~BatchMatMulWithMulAndAddFusion() {}

  std::string Name() override { return "mul-batchmatmulv2-with-add"; }

  MatchedProperties Check(RemapperContext* ctx,
                          const int node_index) const override {
    auto& graph_view = ctx->graph_view;
    MatchedProperties ret =
        FillProperties(&graph_view, graph_view.GetNode(node_index), pattern_);

    bool is_ok = !ret.Empty() && CheckMul(ctx, ret.map.at("multiplicand")) &&
                 CheckBatchMatmul(ctx, ret.map.at("batch_matmul")) &&
                 CheckAdd(ctx, ret.map.at("addend"));

    if (!is_ok) return ret.ToEmpty();
    return ret;
  }

  Status Update(RemapperContext* ctx /** in and out **/,
                const MatchedProperties& properties) const override {
    auto* output_node =
        ctx->graph_view.GetNode(properties.map.at("output"))->node();
    auto* batch_matmul_node =
        ctx->graph_view.GetNode(properties.map.at("batch_matmul"))->node();
    auto* multiplicand_node =
        ctx->graph_view.GetNode(properties.map.at("multiplicand"))->node();
    auto* addend_node =
        ctx->graph_view.GetNode(properties.map.at("addend"))->node();
    auto* mul_input0_node =
        ctx->graph_view.GetNode(properties.map.at("mul_input0"))->node();

    NodeDef fused_node;
    fused_node.set_name(output_node->name());
    fused_node.set_op(kFusedBatchMatMul);
    fused_node.set_device(batch_matmul_node->device());
    fused_node.add_input(mul_input0_node->name());
    fused_node.add_input(batch_matmul_node->input(1));
    fused_node.add_input(multiplicand_node->name());
    fused_node.add_input(addend_node->name());

    CopyAllAttrs(*batch_matmul_node, &fused_node);
    SetFusedOpAttributes(&fused_node, {kMul, kBinaryAdd}, /*num_args=*/2);

    utils::Mutation* mutation = ctx->graph_view.GetMutationBuilder();
    Status status;
    mutation->AddNode(std::move(fused_node), &status);
    TF_RETURN_IF_ERROR(status);
    TF_RETURN_IF_ERROR(mutation->Apply());
    return Status::OK();
  }
};

REGISTER_FUSION(BatchMatMulWithMulAndAddV2Fusion)
REGISTER_FUSION(BatchMatMulWithMulAndAddFusion)

}  // namespace graph
}  // namespace itex
