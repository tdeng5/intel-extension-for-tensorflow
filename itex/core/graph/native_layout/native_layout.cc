/* Copyright (c) 2021-2022 Intel Corporation

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

#include "itex/core/graph/native_layout/native_layout.h"

#include <string>
#include <utility>
#include <vector>

#include "google/protobuf/text_format.h"
#include "itex/core/graph/utils/graph_properties.h"
#include "itex/core/graph/utils/op_types.h"
#include "itex/core/graph/utils/utils.h"
#include "itex/core/utils/attr_value_util.h"
#include "itex/core/utils/types.h"

#ifdef USING_NEXTPLUGGABLE_DEVICE
#include "third_party/build_option/dpcpp/runtime/itex_gpu_runtime.h"
#endif

namespace itex {
namespace graph {

namespace {
namespace protobuf = ::google::protobuf;

std::vector<NativeFormatInfo>* GetCPUEigenNativeFormatInfo() {
  static std::vector<NativeFormatInfo> rinfo{
      // Proper OP
      // {"AddN", "_ITEXAddN", CopyAttrsAll, AlwaysRewrite},
      {"AvgPool", "_ITEXAvgPool", CopyAttrsAll, RewritePool},
      {"AvgPool3D", "_ITEXAvgPool3D", CopyAttrsAll, RewritePool},
      {"AvgPool3DGrad", "_ITEXAvgPool3DGrad", CopyAttrsAll, AlwaysRewrite},
      {"AvgPoolGrad", "_ITEXAvgPoolGrad", CopyAttrsAll, AlwaysRewrite},
      {"BatchMatMul", "_ITEXBatchMatMul", CopyAttrsAll, AlwaysRewrite},
      {"BatchMatMulV2", "_ITEXBatchMatMulV2", CopyAttrsAll, AlwaysRewrite},
      {"Conv2D", "_ITEXConv2D", CopyAttrsAll, AlwaysRewrite},
      {"Conv2DBackpropFilter", "_ITEXConv2DBackpropFilter", CopyAttrsAll,
       RewriteBackwardDataType},
      {"Conv2DBackpropInput", "_ITEXConv2DBackpropInput", CopyAttrsAll,
       RewriteConv2DBackprop},
      {"Conv3D", "_ITEXConv3D", CopyAttrsAll, AlwaysRewrite},
      {"Conv3DBackpropFilterV2", "_ITEXConv3DBackpropFilterV2", CopyAttrsAll,
       RewriteBackwardDataType},
      {"Conv3DBackpropInput", "_ITEXConv3DBackpropInput", CopyAttrsAll,
       RewriteBackwardDataType},
      {"Conv3DBackpropInputV2", "_ITEXConv3DBackpropInputV2", CopyAttrsAll,
       RewriteBackwardDataType},
      {"DepthwiseConv2dNative", "_ITEXDepthwiseConv2dNative", CopyAttrsAll,
       AlwaysRewrite},
      {"DepthwiseConv2dNativeBackpropFilter",
       "_ITEXDepthwiseConv2dNativeBackpropFilter", CopyAttrsAll,
       RewriteBackwardDataType},
      {"DepthwiseConv2dNativeBackpropInput",
       "_ITEXDepthwiseConv2dNativeBackpropInput", CopyAttrsAll,
       RewriteBackwardDataType},
      {"Einsum", "_ITEXEinsum", CopyAttrsAll, AlwaysRewrite},
      {"FusedBatchNorm", "_ITEXFusedBatchNorm", CopyAttrsAll, AlwaysRewrite},
      {"FusedBatchNormGrad", "_ITEXFusedBatchNormGrad", CopyAttrsAll,
       RewriteBackwardDataType},
      {"FusedBatchNormGradV2", "_ITEXFusedBatchNormGradV2", CopyAttrsAll,
       RewriteBackwardDataType},
      {"FusedBatchNormGradV3", "_ITEXFusedBatchNormGradV3", CopyAttrsAll,
       RewriteBackwardDataType},
      {"FusedBatchNormV2", "_ITEXFusedBatchNormV2", CopyAttrsAll,
       AlwaysRewrite},
      {"FusedBatchNormV3", "_ITEXFusedBatchNormV3", CopyAttrsAll,
       AlwaysRewrite},
      {"FusedInstanceNorm", "_ITEXFusedInstanceNorm", CopyAttrsAll,
       AlwaysRewrite},
      {"FusedPadConv2D", "_ITEXPadWithConv2D", CopyAttrsAll, AlwaysRewrite,
       AdjustInputOrder},
      {"GRUBlockCell", "_ITEXGRUCell", CopyAttrsAll, AlwaysRewrite},
      {"Gelu", "ITEXGelu", CopyAttrsAll, AlwaysRewrite},
      {"GeluGrad", "ITEXGeluGrad", CopyAttrsAll, RewriteBackwardDataType},
      {"LayerNorm", "ITEXLayerNorm", CopyAttrsAll, RewriteLayerNorm},
      {"LayerNormGrad", "ITEXLayerNormGrad", CopyAttrsAll,
       RewriteLayerNormGrad},
      {"MatMul", "_ITEXMatMul", CopyAttrsAll, AlwaysRewrite},
      {"MaxPool", "_ITEXMaxPool", CopyAttrsAll, RewritePool},
      {"MaxPool3D", "_ITEXMaxPool3D", CopyAttrsAll, RewritePool},
      {"MaxPoolGrad", "_ITEXMaxPoolGrad", CopyAttrsAll, RewriteMaxPoolGrad},
      {"MaxPool3DGrad", "_ITEXMaxPool3DGrad", CopyAttrsAll, RewriteMaxPoolGrad},
      {"RandomUniform", "_ITEXRandomUniform", CopyAttrsAll,
       RewriteRandomUniform},
      {"Softmax", "_ITEXSoftmax", CopyAttrsAll, AlwaysRewrite},
      {"Transpose", "_ITEXTranspose", CopyAttrsAll, AlwaysRewrite},
      {"_FusedBatchNormEx", "_ITEXFusedBatchNormEx", CopyAttrsAll,
       RewriteFusedBatchNormEx},
      {"_FusedMatMul", "_ITEXFusedMatMul", CopyAttrsAll, AlwaysRewrite},
      // Intel-TF ops. Usually these ops should always be rewritten.
      // This part is for compatibility of legacy Intel-TF models, it will be
      // removed in future.
      {"MklAUGRU", "_ITEXForwardAUGRU", CopyAttrsAll, AlwaysRewrite},
      {"MklGRU", "_ITEXForwardGRU", CopyAttrsAll, AlwaysRewrite},
      {"_MklFusedBatchMatMulV2", "_ITEXFusedBatchMatMulV2", CopyAttrsAll,
       AlwaysRewrite},
      {"_MklLayerNorm", "_ITEXMklLayerNorm", CopyAttrsAll, AlwaysRewrite},

      // INT8 op
      {"Dequantize", "_ITEXDequantize", CopyAttrsAll, RewriteQuantize},
      {"QuantizedAvgPool", "ITEXQuantizedAvgPool", CopyAttrsAll, AlwaysRewrite},
      // Disable concat rewrite, since we don't support concat int8 with
      // different scales
      // {"QuantizedConcatV2", "_ITEXQuantizedConcatV2", CopyAttrsAll,
      //  AlwaysRewrite},
      {"QuantizedConv2D", "_ITEXQuantizedConv2D", CopyAttrsQuantizedConv2D,
       AlwaysRewrite},
      {"QuantizedConv2DAndRequantize", "_ITEXQuantizedConv2DAndRequantize",
       CopyAttrsQuantizedConv2D, AlwaysRewrite},
      {"QuantizedConv2DPerChannel", "_ITEXQuantizedConv2DPerChannel",
       CopyAttrsQuantizedConv2D, AlwaysRewrite},
      {"QuantizedConv2DWithBias", "_ITEXQuantizedConv2DWithBias",
       CopyAttrsQuantizedConv2D, AlwaysRewrite},
      {"QuantizedConv2DWithBiasAndRelu", "_ITEXQuantizedConv2DWithBiasAndRelu",
       CopyAttrsQuantizedConv2D, AlwaysRewrite},
      {"QuantizedConv2DWithBiasAndReluAndRequantize",
       "_ITEXQuantizedConv2DWithBiasAndReluAndRequantize",
       CopyAttrsQuantizedConv2D, AlwaysRewrite},
      {"QuantizedConv2DWithBiasAndRequantize",
       "_ITEXQuantizedConv2DWithBiasAndRequantize", CopyAttrsQuantizedConv2D,
       AlwaysRewrite},
      {"QuantizedConv2DWithBiasSignedSumAndReluAndRequantize",
       "_ITEXQuantizedConv2DWithBiasSignedSumAndReluAndRequantize",
       CopyAttrsQuantizedConv2D, AlwaysRewrite},
      {"QuantizedConv2DWithBiasSumAndRelu",
       "_ITEXQuantizedConv2DWithBiasSumAndRelu", CopyAttrsQuantizedConv2D,
       AlwaysRewrite},
      {"QuantizedConv2DWithBiasSumAndReluAndRequantize",
       "_ITEXQuantizedConv2DWithBiasSumAndReluAndRequantize",
       CopyAttrsQuantizedConv2D, AlwaysRewrite},
      {"QuantizedDepthwiseConv2D", "_ITEXQuantizedDepthwiseConv2D",
       CopyAttrsQuantizedConv2D, AlwaysRewrite},
      {"QuantizedDepthwiseConv2DWithBias",
       "_ITEXQuantizedDepthwiseConv2DWithBias", CopyAttrsQuantizedConv2D,
       AlwaysRewrite},
      {"QuantizedDepthwiseConv2DWithBiasAndRelu",
       "_ITEXQuantizedDepthwiseConv2DWithBiasAndRelu", CopyAttrsQuantizedConv2D,
       AlwaysRewrite},
      {"QuantizedDepthwiseConv2DWithBiasAndReluAndRequantize",
       "_ITEXQuantizedDepthwiseConv2DWithBiasAndReluAndRequantize",
       CopyAttrsQuantizedConv2D, AlwaysRewrite},
      {"QuantizedMatMulWithBias", "_ITEXQuantizedMatMulWithBias",
       CopyAttrsQuantizedMatMul, AlwaysRewrite},
      {"QuantizedMatMulWithBiasAndDequantize",
       "_ITEXQuantizedMatMulWithBiasAndDequantize", CopyAttrsQuantizedMatMul,
       AlwaysRewrite},
      {"QuantizedMatMulWithBiasAndRelu", "_ITEXQuantizedMatMulWithBiasAndRelu",
       CopyAttrsQuantizedMatMul, AlwaysRewrite},
      {"QuantizedMatMulWithBiasAndReluAndRequantize",
       "_ITEXQuantizedMatMulWithBiasAndReluAndRequantize",
       CopyAttrsQuantizedMatMul, AlwaysRewrite},
      {"QuantizedMatMulWithBiasAndRequantize",
       "_ITEXQuantizedMatMulWithBiasAndRequantize", CopyAttrsQuantizedMatMul,
       AlwaysRewrite},
      {"QuantizedMaxPool", "_ITEXQuantizedMaxPool", CopyAttrsAll,
       AlwaysRewrite},
      {"QuantizedMaxPool3D", "_ITEXQuantizedMaxPool3D", CopyAttrsAll,
       AlwaysRewrite},
      {"QuantizedReshape", "_ITEXQuantizedReshape", CopyAttrsAll,
       RewriteQuantizeReshape},
      {"QuantizeV2", "_ITEXQuantizeV2", CopyAttrsQuantize, RewriteQuantize},
      {"_QuantizedBatchMatMul", "_ITEXQuantizedBatchMatMul", CopyAttrsAll,
       AlwaysRewrite},
      {"_QuantizedBatchMatMulV2AndDequantize",
       "_ITEXQuantizedBatchMatMulV2AndDequantize", CopyAttrsAll, AlwaysRewrite},
      {"_QuantizedConv2D", "_ITEXQuantizedConv2DV2", CopyAttrsAll,
       AlwaysRewrite},
      {"_QuantizedConv3D", "_ITEXQuantizedConv3DV2", CopyAttrsAll,
       AlwaysRewrite},
      {"_QuantizedDepthwiseConv2D", "_ITEXQuantizedDepthwiseConv2DV2",
       CopyAttrsAll, AlwaysRewrite},
      {"_QuantizedFusedBatchMatMulV2AndDequantize",
       "_ITEXQuantizedFusedBatchMatMulV2AndDequantize", CopyAttrsAll,
       AlwaysRewrite},
      {"_QuantizedFusedBatchNorm", "_ITEXQuantizedFusedBatchNorm", CopyAttrsAll,
       AlwaysRewrite},
      {"_QuantizedFusedMatMul", "_ITEXQuantizedFusedMatMul",
       CopyAttrsQuantizedMatMul, AlwaysRewrite},
      {"_QuantizedFusedMatMulAndDequantize",
       "_ITEXQuantizedFusedMatMulAndDequantize", CopyAttrsQuantizedMatMul,
       AlwaysRewrite},
      {"_QuantizedFusedMatMulAndRequantize",
       "_ITEXQuantizedFusedMatMulAndRequantize", CopyAttrsQuantizedMatMul,
       AlwaysRewrite},
      {"_QuantizedMatMul", "_ITEXQuantizedMatMul", CopyAttrsAll, AlwaysRewrite},
      {"_QuantizedTranspose", "_ITEXQuantizedTranspose", CopyAttrsAll,
       AlwaysRewrite}};
  return &rinfo;
}

std::vector<NativeFormatInfo>* GetCPUNativeFormatInfo() {
  static std::vector<NativeFormatInfo> rinfo{
      {"AddN", "_ITEXAddN", CopyAttrsAll, AlwaysRewrite},
      {"Elu", "_ITEXElu", CopyAttrsAll, AlwaysRewrite},
      {"EluGrad", "_ITEXEluGrad", CopyAttrsAll, RewriteBackwardDataType},
      {"LeakyRelu", "_ITEXLeakyRelu", CopyAttrsAll, AlwaysRewrite},
      {"LeakyReluGrad", "_ITEXLeakyReluGrad", CopyAttrsAll,
       RewriteBackwardDataType},
      {"Relu6Grad", "_ITEXRelu6Grad", CopyAttrsAll, RewriteBackwardDataType},
      {"ReluGrad", "_ITEXReluGrad", CopyAttrsAll, RewriteBackwardDataType},
      {"ResizeBilinear", "_ITEXResizeBilinear", CopyAttrsAll, RewriteResize},
      {"ResizeBilinearGrad", "_ITEXResizeBilinearGrad", CopyAttrsAll,
       RewriteResize},
      {"Slice", "_ITEXSlice", CopyAttrsAll, AlwaysRewrite}};
  return &rinfo;
}

std::vector<NativeFormatInfo>* GetGPUNativeFormatInfo() {
  static std::vector<NativeFormatInfo> rinfo{
#ifndef USING_NEXTPLUGGABLE_DEVICE
      {"MaxPool", "_ITEXMaxPool", CopyAttrsAll, RewritePool},
      {"MaxPool3D", "_ITEXMaxPool3D", CopyAttrsAll, RewritePool},
      {"MaxPoolGrad", "_ITEXMaxPoolGrad", CopyAttrsAll, RewriteMaxPoolGrad},
      {"MaxPool3DGrad", "_ITEXMaxPool3DGrad", CopyAttrsAll, RewriteMaxPoolGrad},
      {"MaxPoolV2", "_ITEXMaxPoolV2", CopyAttrsAll, AlwaysRewrite},
      {"MaxPoolGradV2", "_ITEXMaxPoolGradV2", CopyAttrsAll, RewriteMaxPoolGrad},
#endif  // USING_NEXTPLUGGABLE_DEVICE
      {"TensorArray", "_ITEXTensorArray", CopyAttrsForTensorArray,
       AlwaysRewrite},
      {"TensorArrayClose", "_ITEXTensorArrayClose", CopyAttrsForTensorArray,
       AlwaysRewrite},
      {"TensorArrayCloseV2", "_ITEXTensorArrayClose", CopyAttrsForTensorArray,
       AlwaysRewrite},
      {"TensorArrayCloseV3", "_ITEXTensorArrayClose", CopyAttrsForTensorArray,
       AlwaysRewrite},
      {"TensorArrayConcat", "_ITEXTensorArrayConcat", CopyAttrsForTensorArray,
       AlwaysRewrite},
      {"TensorArrayConcatV2", "_ITEXTensorArrayConcat", CopyAttrsForTensorArray,
       AlwaysRewrite},
      {"TensorArrayConcatV3", "_ITEXTensorArrayConcat", CopyAttrsForTensorArray,
       AlwaysRewrite},
      {"TensorArrayGather", "_ITEXTensorArrayGather", CopyAttrsForTensorArray,
       AlwaysRewrite},
      {"TensorArrayGatherV2", "_ITEXTensorArrayGather", CopyAttrsForTensorArray,
       AlwaysRewrite},
      {"TensorArrayGatherV3", "_ITEXTensorArrayGather", CopyAttrsForTensorArray,
       AlwaysRewrite},
      {"TensorArrayGrad", "_ITEXTensorArrayGrad", CopyAttrsForTensorArray,
       AlwaysRewrite},
      {"TensorArrayGradV2", "_ITEXTensorArrayGrad", CopyAttrsForTensorArray,
       AlwaysRewrite},
      {"TensorArrayGradV3", "_ITEXTensorArrayGrad", CopyAttrsForTensorArray,
       AlwaysRewrite},
      {"TensorArrayGradWithShape", "_ITEXTensorArrayGradWithShape",
       CopyAttrsForTensorArray, AlwaysRewrite},
      {"TensorArrayPack", "_ITEXTensorArrayPack", CopyAttrsForTensorArray,
       AlwaysRewrite},
      {"TensorArrayRead", "_ITEXTensorArrayRead", CopyAttrsForTensorArray,
       AlwaysRewrite},
      {"TensorArrayReadV2", "_ITEXTensorArrayRead", CopyAttrsForTensorArray,
       AlwaysRewrite},
      {"TensorArrayReadV3", "_ITEXTensorArrayRead", CopyAttrsForTensorArray,
       AlwaysRewrite},
      {"TensorArrayScatter", "_ITEXTensorArrayScatter", CopyAttrsForTensorArray,
       AlwaysRewrite},
      {"TensorArrayScatterV2", "_ITEXTensorArrayScatter",
       CopyAttrsForTensorArray, AlwaysRewrite},
      {"TensorArrayScatterV3", "_ITEXTensorArrayScatter",
       CopyAttrsForTensorArray, AlwaysRewrite},
      {"TensorArraySize", "_ITEXTensorArraySize", CopyAttrsForTensorArray,
       AlwaysRewrite},
      {"TensorArraySizeV2", "_ITEXTensorArraySize", CopyAttrsForTensorArray,
       AlwaysRewrite},
      {"TensorArraySizeV3", "_ITEXTensorArraySize", CopyAttrsForTensorArray,
       AlwaysRewrite},
      {"TensorArraySplit", "_ITEXTensorArraySplit", CopyAttrsForTensorArray,
       AlwaysRewrite},
      {"TensorArraySplitV2", "_ITEXTensorArraySplit", CopyAttrsForTensorArray,
       AlwaysRewrite},
      {"TensorArraySplitV3", "_ITEXTensorArraySplit", CopyAttrsForTensorArray,
       AlwaysRewrite},
      {"TensorArrayUnPack", "_ITEXTensorArrayUnPack", CopyAttrsForTensorArray,
       AlwaysRewrite},
      {"TensorArrayV2", "_ITEXTensorArray", CopyAttrsForTensorArray,
       AlwaysRewrite},
      {"TensorArrayV3", "_ITEXTensorArray", CopyAttrsForTensorArray,
       AlwaysRewrite},
      {"TensorArrayWrite", "_ITEXTensorArrayWrite", CopyAttrsForTensorArray,
       AlwaysRewrite},
      {"TensorArrayWriteV2", "_ITEXTensorArrayWrite", CopyAttrsForTensorArray,
       AlwaysRewrite},
      {"TensorArrayWriteV3", "_ITEXTensorArrayWrite", CopyAttrsForTensorArray,
       AlwaysRewrite},
  };
#ifdef USING_NEXTPLUGGABLE_DEVICE
  static bool is_initialized = false;
  if (!is_initialized) {
    ITEXNpdConfig& npdConfig = ITEXNpdConfig::getNpdConfig();
    if (!npdConfig.isXlaAutoJitEnabled()) {
      rinfo.push_back({"MaxPool", "_ITEXMaxPool", CopyAttrsAll, RewritePool});
      rinfo.push_back(
          {"MaxPool3D", "_ITEXMaxPool3D", CopyAttrsAll, RewritePool});
      rinfo.push_back({"MaxPoolGrad", "_ITEXMaxPoolGrad", CopyAttrsAll,
                       RewriteMaxPoolGrad});
      rinfo.push_back({"MaxPool3DGrad", "_ITEXMaxPool3DGrad", CopyAttrsAll,
                       RewriteMaxPoolGrad});
      rinfo.push_back(
          {"MaxPoolV2", "_ITEXMaxPoolV2", CopyAttrsAll, AlwaysRewrite});
      rinfo.push_back({"MaxPoolGradV2", "_ITEXMaxPoolGradV2", CopyAttrsAll,
                       RewriteMaxPoolGrad});
    }
    is_initialized = true;
  }
#endif

  return &rinfo;
}

// Rewrite non-oneDNN op which is optimized in a customized manner.
std::vector<NativeFormatInfo>* GetCustomNativeFormatInfo() {
  static std::vector<NativeFormatInfo> rinfo{
      {"RandomUniform", "_ITEXRandomUniform", CopyAttrsAll,
       RewriteRandomUniform},
  };
  return &rinfo;
}

}  // namespace

const NativeFormatInfo* CheckForNodeNativeFormat(
    OptimizerContext* opt_ctx, const utils::MutableNodeView& node_view) {
  NodeDef& node_def = *(node_view.node());

  if (!IsLayoutRewriteSupportedDataType(node_def)) return nullptr;

  // We now check if rewrite rule applies for this op. If rewrite rule passes
  // for this op, then we rewrite it to Native op.
  // Find matching NativeFormatInfo and then check that rewrite rule applies.
  std::vector<NativeFormatInfo>* rinfo;
  if (absl::StrContains("CPU", opt_ctx->device_name)) {
    bool enable_omp = true;
    ITEX_CHECK_OK(ReadBoolFromEnvVar("ITEX_OMP_THREADPOOL", true, &enable_omp));
#ifdef CC_THREADPOOL_BUILD
    enable_omp = false;
#endif
    if (opt_ctx->enable_complete_opt) {
      if (enable_omp) {
        rinfo = GetCPUEigenNativeFormatInfo();
        rinfo->insert(rinfo->end(), GetCPUNativeFormatInfo()->begin(),
                      GetCPUNativeFormatInfo()->end());
      } else {
        rinfo = GetCPUEigenNativeFormatInfo();
      }
    } else {
      rinfo = GetCustomNativeFormatInfo();
    }
  } else if (absl::StrContains("GPU", opt_ctx->device_name)) {
    rinfo = GetGPUNativeFormatInfo();
  } else if (absl::StrContains("XPU", opt_ctx->device_name)) {
    rinfo = GetGPUNativeFormatInfo();
  } else {
    ITEX_LOG(WARNING) << "invalid device name, expected CPU/GPU/XPU, got "
                      << opt_ctx->device_name;
    return nullptr;
  }

  for (auto ri = rinfo->cbegin(); ri != rinfo->cend(); ++ri) {
    if (node_def.op() == ri->name && ri->rewrite_rule(node_view)) {
      return &*ri;
    }
  }

  // Else return not found.
  return nullptr;
}

// Rewrites input node to a new node specified by its matching rewrite info.
//
// Method first searches matching rewrite info for input node and then
// uses that info to rewrite.
//
// Input node may be deleted in case of rewrite. Attempt to use the node
// after the call can result in undefined behaviors.
Status RewriteNode(NativeFormatContext* ctx, const int node_index,
                   const NativeFormatInfo* ri) {
  const auto* node_view = ctx->graph_view.GetNode(node_index);
  const auto* node_def = node_view->node();

  NodeDef new_node_def;
  // Let's copy all inputs (TF tensors) of original node to new node.
  for (int idx = 0; idx < node_view->NumRegularFanins(); idx++) {
    new_node_def.add_input(node_def->input(idx));
  }

  new_node_def.set_name(node_def->name());
  new_node_def.set_op(ri->new_name);
  new_node_def.set_device(node_def->device());

  ri->copy_attrs(node_view, &new_node_def);

  if (ri->post_process != nullptr) ri->post_process(&new_node_def);

  // Add workspace inputs if needed.
  AddWorkspace(node_view, &new_node_def);

  // Incoming data edges from 'orig_node' node to new 'new_node' node are
  // already copied in BuildNode. We need to handle control edges now.
  for (int idx = 0; idx < node_view->NumControllingFanins(); idx++) {
    new_node_def.add_input(
        node_def->input(node_view->NumRegularFanins() + idx));
  }

  utils::Mutation* mutation = ctx->graph_view.GetMutationBuilder();

  // apply mutation
  Status status;
  mutation->AddNode(std::move(new_node_def), &status);
  TF_ABORT_IF_ERROR(std::move(status));
  TF_ABORT_IF_ERROR(mutation->Apply());
  return Status::OK();
}

Status RunNativeLayout(OptimizerContext* opt_ctx, const GrapplerItem& item,
                       const GraphDef& graph_def, GraphDef* optimized_graph) {
  Status status;
  GraphDef multable_graph_def = graph_def;
  NativeFormatContext ctx(item, &multable_graph_def, &status);

  // Processing graph in reverse-topological sorted order allows to remap
  // longer chains of dependent ops in one pass.
  TF_ABORT_IF_ERROR(
      ctx.graph_view.SortTopologically(/*ignore_cycles=*/false, {}));

  // Skip nodes that were invalidated
  int num_nodes = multable_graph_def.node_size();

  ITEX_VLOG(1) << "NativeLayoutPass: Start to rewrite nodes.";

  for (int node_index = 0; node_index < num_nodes; ++node_index) {
    const auto* node_view = ctx.graph_view.GetNode(node_index);
    const auto* node_def = node_view->node();

    // Check if node can run on current optimizer device.
    if (!NodeIsOnDevice(opt_ctx->device_name, node_def)) continue;

    // Check if node is fp16 and supported on device.
    if (NodeIsOnCpu(node_def) &&
        GetDataTypeFromAttr(*node_def, "T") == DT_HALF &&
        !port::HasCpuFP16Support())
      continue;

    const NativeFormatInfo* ri = nullptr;
    // We will first search if node is to be rewritten.
    if ((ri = CheckForNodeNativeFormat(opt_ctx, *node_view)) != nullptr) {
      const string& node_name = node_def->name();
      const string& op_name = node_def->op();

      if (RewriteNode(&ctx, node_index, ri) == Status::OK()) {
        ITEX_VLOG(2) << "NativeLayoutPass: rewrote node " << node_name
                     << " with op " << op_name
                     << " for Native layout optimization.";
      } else {
        ITEX_VLOG(2) << "NativeLayoutPass: found node " << node_name
                     << " with op " << op_name << " but rewrite failed.";
      }
    }
  }

  *optimized_graph = std::move(multable_graph_def);
  return Status::OK();
}

}  // namespace graph
}  // namespace itex
