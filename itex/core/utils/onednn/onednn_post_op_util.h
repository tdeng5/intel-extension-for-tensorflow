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

#ifndef ITEX_CORE_UTILS_ONEDNN_ONEDNN_POST_OP_UTIL_H_
#define ITEX_CORE_UTILS_ONEDNN_ONEDNN_POST_OP_UTIL_H_

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "dnnl.h"  // NOLINT(build/include_subdir)
#include "itex/core/utils/onednn/onednn_util.h"

namespace itex {

using kind = dnnl::primitive::kind;
using algorithm = dnnl::algorithm;
using memory = dnnl::memory;

// Helper data struct to record necessary info for post op fusion.
struct PostOpInfo {
  string name;
  // Right now it supports 4 different kinds of fusion:
  //   * Eltwise
  //   * Sum
  //   * Depthwise
  //   * Binary
  // Check here for more details:
  // https://oneapi-src.github.io/oneDNN/dev_guide_attributes_post_ops.html
  dnnl::primitive::kind kind;
  dnnl::algorithm alg;
  float alpha;
  float beta;
};

// Helper data struct to record necessary info for output_scales
struct OutputScaleParam {
  int mask;
  std::vector<float> scales;
};

// FusedBNContext storing fused batchnorm messages for contructing oneDNN
// post-ops operations
struct FusedBNContext {
  // FusedBatchNorm related memory
  std::shared_ptr<dnnl::memory> bn_scale_mem;
  std::shared_ptr<dnnl::memory> bn_mean_mem;
  std::shared_ptr<dnnl::memory> bn_rsqrt_mem;
  std::shared_ptr<dnnl::memory> bn_offset_mem;

  // FusedBatchNorm related memory desc
  std::shared_ptr<dnnl::memory::desc> bn_scale_md;
  std::shared_ptr<dnnl::memory::desc> bn_mean_md;
  std::shared_ptr<dnnl::memory::desc> bn_rsqrt_md;
  std::shared_ptr<dnnl::memory::desc> bn_offset_md;
};

class PostOpUtil {
 public:
  PostOpUtil() = default;
  ~PostOpUtil() = default;

  // Add ops to current object, they will be used for creating primitive desc.
  // This ops vector usually come from op attribute `fused_ops`.
  // Return `true` if all ops are supported.
  bool AddOps(const std::vector<string>& fused_ops);

  // Set alpha for `LeakyRelu`.
  // Will report error if no `LeakyRelu` in post ops.
  void SetLeakyReluAlpha(float alpha);

  // Set epsilon for `FusedBatchNorm`
  void set_epsilon(float epsilon) { bn_epsilon_ = epsilon; }

  // Set alpha/beta for `Linear`.
  // Will report error if no `Linear` in post ops.
  void SetLinearAlphaBeta(float alpha, float beta);

  // Set scale for post op. Sometimes the scale is only available during node
  // execution, so we need to set scale to the post op which is created in node
  // construction
  void SetPostOpScale(const absl::string_view name, float scale);

  // Set scale vector for output scale
  // Similar to `SetPostOpScale`, this function set the scale vector to output
  // scale, which is only available in node execution
  void SetOutputScale(const std::vector<float>& scales);

  std::vector<float>& GetOutputScale() { return output_scale_param_.scales; }

  // Set post op and output scale attribution for `attr`.
  // If `HasBinary()`, an extra parameter `md_list` is required.
  void SetPostOpAttr(dnnl::primitive_attr* attr,
                     const std::vector<dnnl::memory::desc>& md_list = {});

  // Set batchnorm and post op attribution for `attr`.
  void SetBNPostOpAttr(dnnl::primitive_attr* attr,
                       const std::vector<dnnl::memory::desc>& md_list = {});

  // Build batchnorm context needed for post-ops primitive construction
  void BuildBNContext(memory::format_tag* data_layout,
                      memory::dims* fuse_bn_dims, dnnl::engine* engine);

  // Set batchnorm post-ops onednn memory pointing to ready buffers
  void SetBNMemory(const Tensor& bn_scale_tensor, const Tensor& bn_mean_tensor,
                   const Tensor& bn_offset_tensor,
                   const Tensor& bn_rsqrt_tensor);

  void AddBNPrimArgs(std::unordered_map<int, memory>* fwd_primitives_args_);

  // Check the given elewise op is supported by oneDNN or not.
  static bool IsSupportedActivation(const absl::string_view op_name);

  // Misc fuctions.
  inline bool HasActivation() { return has_activation_; }
  inline bool HasAdd() { return has_add_; }
  inline bool HasBias() { return has_bias_; }
  inline bool HasBN() { return has_bn_; }
  inline bool HasBinary() { return binary_num_ != 0; }
  inline bool HasLeakyRelu() { return has_leaky_relu_; }
  inline bool HasLinear() { return has_linear_; }

  inline bool HasOutputScales() { return has_output_scales_; }
  inline bool HasRequantize() { return has_requantize_; }

  // Record op number to support multiple Binary post op fusion.
  inline int GetBinaryNum() { return binary_num_; }

 private:
  // Return the read-only table contains supported `PostOpInfo`.
  // This table is converted from oneDNN.
  static const std::vector<PostOpInfo>& GetAllPostOpInfo();

  // Get specific `PostOpInfo` according to `op_name`.
  // Return `nullptr` if find nothing.
  static const PostOpInfo* GetPostOpInfoByName(const absl::string_view op_name);

  // Set "post_op". We now use lazy evaluation, the function is called in
  // "SetPostOpAttr".
  // Reasons for lazy evalution is that once postop attribures are set, OneDnn
  // doesn't allow to change the postop scales. So we have to set scale in
  // Compute(), when all context information is available
  void SetPostOp(dnnl::post_ops* post_op,
                 const std::vector<dnnl::memory::desc>& md_list);

  // Save the post op and its corresponding scale factor
  // The first element is post op name, second element is corresponding scale
  std::vector<std::pair<string, float>> postop_scale_list_;

  // Member to save the output scale parameter
  OutputScaleParam output_scale_param_;

  bool has_activation_ = false;
  bool has_add_ = false;
  // Note `BiasAdd` is a special case, it doesn't have post op info because
  // it will be fused in primitive directly.
  bool has_bias_ = false;
  // Whether have batchnorm fusion
  bool has_bn_ = false;
  // Use this flag to check whether need to set alpha for `LeakyRelu`.
  bool has_leaky_relu_ = false;
  // Use this flag to check whether has linear post op
  bool has_linear_ = false;

  // Flags for INT8.
  bool has_output_scales_ = false;
  bool has_requantize_ = false;

  // Stores fused batchnorm constructing message
  FusedBNContext bn_context_;

  // Helper var for multilpe Binary post op fusion.
  int binary_num_ = 0;
  // Helper vars for post op execution.
  float leaky_relu_alpha_ = NAN;
  float linear_alpha_ = NAN;
  float linear_beta_ = NAN;

  float bn_epsilon_ = 0.0001;
};

}  // namespace itex

#endif  // ITEX_CORE_UTILS_ONEDNN_ONEDNN_POST_OP_UTIL_H_
