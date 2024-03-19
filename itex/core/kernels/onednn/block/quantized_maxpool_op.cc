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

#include "itex/core/kernels/common/no_ops.h"
#include "itex/core/kernels/common/pooling_ops_common.h"
#include "itex/core/utils/onednn/onednn_util.h"
#include "itex/core/utils/register_types.h"

namespace itex {

using algorithm = dnnl::algorithm;
using pooling_forward = dnnl::pooling_forward;
using prop_kind = dnnl::prop_kind;

template <typename Device, typename T>
class OneDnnQuantizedMaxPoolOp : public OneDnnPoolOpBase<T> {
 public:
  explicit OneDnnQuantizedMaxPoolOp(OpKernelConstruction* context)
      : OneDnnPoolOpBase<T>(context) {}

  void Compute(OpKernelContext* context) override {
    const int kSrcIndex = 0;
    const int kDstIndex = 0;
    const int kDstMinRangeIndex = 1;
    const int kDstMaxRangeIndex = 2;
    const Tensor& src_tensor = context->input(kSrcIndex);
    OneDnnShape src_onednn_shape;
    GetOneDnnShape(context, kSrcIndex, &src_onednn_shape);
    const TensorShape& src_tf_shape = src_onednn_shape.IsOneDnnTensor()
                                          ? src_onednn_shape.GetTfShape()
                                          : src_tensor.shape();

    OP_REQUIRES(context, src_tf_shape.dims() == 4 || src_tf_shape.dims() == 5,
                errors::InvalidArgument("Input must be 4 or 5-dimensional"));

    // Initialize shape variables.
    OneDnnPoolParameters pool_params;
    pool_params.Init(context, this->ksize_, this->stride_, this->padding_,
                     this->padding_list_, this->data_format_tf_, src_tf_shape);
    OP_REQUIRES_OK(context, context->status());
    // Declare output tensor, and set OneDNN dims with NCHW/NCDHW order.
    Tensor* dst_tensor = nullptr;
    OneDnnShape dst_onednn_shape;
    TensorShape dst_tf_shape;
    memory::dims dst_onednn_dims;
    this->GetOutputDims(pool_params, &dst_onednn_dims, &dst_tf_shape);

    // Return with TF format if nothing to compute. Need to change the
    // shape from OneDNN NCHW/NCDHW to original TF format.
    if (src_tf_shape.num_elements() == 0) {
      dst_onednn_shape.SetOneDnnTensor(false);
      AllocateOutputSetOneDnnShape(context, kDstIndex, &dst_tensor,
                                   dst_tf_shape, dst_onednn_shape);
      return;
    }
    // Create primitive and execute op.
    // Since TF MaxPool will be NCHW/NCDHW or NHWC/NDHWC, here use dims and
    // format to describe memory to avoid explicit Reorder.
    try {
      // Create src and dst memory desc.
      auto dst_md = memory::desc(dst_onednn_dims, OneDnnType<T>(),
                                 memory::format_tag::any);
      memory::desc src_md;

      if (src_onednn_shape.IsOneDnnTensor()) {
        src_md = src_onednn_shape.GetOneDnnLayout();
      } else {
        auto src_dims = TFShapeToOneDnnDimsInNC(
            src_tensor.shape(), this->data_format_tf_, this->is_2d_);
        src_md =
            memory::desc(src_dims, OneDnnType<T>(), this->data_format_onednn_);
      }

      memory::dims filter_dims, strides, padding_left, padding_right;
      memory::dims dilation_dims;

      this->PoolParamsToDims(&pool_params, &filter_dims, &dilation_dims,
                             &strides, &padding_left, &padding_right);

      // Create forward primitive.
      auto onednn_engine = CreateDnnlEngine<Device>(*context);

      // Only use "forward_inference" for quantizedMaxPool
      dnnl::primitive_attr attr;
      attr.set_scratchpad_mode(dnnl::scratchpad_mode::user);
      auto fwd_pd = pooling_forward::primitive_desc(
          onednn_engine, prop_kind::forward_inference, algorithm::pooling_max,
          src_md, dst_md, strides, filter_dims, dilation_dims, padding_left,
          padding_right, attr);
      Tensor scratchpad_tensor;
      int64 scratchpad_size = fwd_pd.scratchpad_desc().get_size() / sizeof(T);
      OP_REQUIRES_OK(context,
                     context->allocate_temp(DataTypeToEnum<T>::v(),
                                            TensorShape({scratchpad_size}),
                                            &scratchpad_tensor));
      auto scratchpad_mem =
          dnnl::memory(fwd_pd.scratchpad_desc(), onednn_engine,
                       GetTensorBuffer<T>(&scratchpad_tensor));

      auto fwd_primitive = pooling_forward(fwd_pd);

      // Allocate output.
      SetOutputTensorShape(fwd_pd.dst_desc(), this->tensor_format_onednn_,
                           &dst_tf_shape, &dst_onednn_shape, true);
      AllocateOutputSetOneDnnShape(context, kDstIndex, &dst_tensor,
                                   dst_tf_shape, dst_onednn_shape);
      // Create src and dst memory.
      auto src_mem = CreateDnnlMemory(fwd_pd.src_desc(), onednn_engine,
                                      GetTensorBuffer<T>(&src_tensor));
      auto dst_mem = CreateDnnlMemory(fwd_pd.dst_desc(), onednn_engine,
                                      GetTensorBuffer<T>(dst_tensor));

      // Execute primitive.
      auto onednn_stream = CreateDnnlStream(*context, onednn_engine);
      std::unordered_map<int, memory> fwd_primitive_args = {
          {DNNL_ARG_SRC, src_mem},
          {DNNL_ARG_DST, dst_mem},
          {DNNL_ARG_SCRATCHPAD, scratchpad_mem}};
      fwd_primitive.execute(onednn_stream, fwd_primitive_args);

      // Pass min, max from input to output.
      const Tensor& min_input_t = context->input(1);
      const Tensor& max_input_t = context->input(2);
      const float min_input = min_input_t.flat<float>()(0);
      const float max_input = max_input_t.flat<float>()(0);

      Tensor* output_min = nullptr;
      Tensor* output_max = nullptr;
      OneDnnShape output_min_onednn_shape, output_max_onednn_shape;
      output_min_onednn_shape.SetOneDnnTensor(false);
      output_max_onednn_shape.SetOneDnnTensor(false);

      // Allocate output min as host memory
      AllocateOutputSetOneDnnShape(context, kDstMinRangeIndex, &output_min, {},
                                   output_min_onednn_shape);
      AllocateOutputSetOneDnnShape(context, kDstMaxRangeIndex, &output_max, {},
                                   output_max_onednn_shape);
      output_min->flat<float>()(0) = min_input;
      output_max->flat<float>()(0) = max_input;
    } catch (dnnl::error& e) {
      string error_msg = "Status: " + std::to_string(e.status) +
                         ", message: " + string(e.message) + ", in file " +
                         string(__FILE__) + ":" + std::to_string(__LINE__);
      OP_REQUIRES_OK(
          context,
          errors::Aborted("Operation received an exception:", error_msg));
    }
  }
};

#ifndef INTEL_CPU_ONLY
#define REGISTER_KERNEL(TYPE)                                 \
  REGISTER_KERNEL_BUILDER(Name("_OneDnnQuantizedMaxPool")     \
                              .Device(DEVICE_GPU)             \
                              .TypeConstraint<TYPE>("T")      \
                              .HostMemory("min_input")        \
                              .HostMemory("max_input")        \
                              .HostMemory("min_output")       \
                              .HostMemory("max_output")       \
                              .HostMemory("input_meta")       \
                              .HostMemory("output_meta")      \
                              .HostMemory("min_input_meta")   \
                              .HostMemory("max_input_meta")   \
                              .HostMemory("min_output_meta")  \
                              .HostMemory("max_output_meta"), \
                          OneDnnQuantizedMaxPoolOp<GPUDevice, TYPE>);
TF_CALL_qint8(REGISTER_KERNEL);
TF_CALL_quint8(REGISTER_KERNEL);

#else
#define REGISTER_KERNEL(TYPE)                             \
  REGISTER_KERNEL_BUILDER(Name("_OneDnnQuantizedMaxPool") \
                              .Device(DEVICE_CPU)         \
                              .TypeConstraint<TYPE>("T"), \
                          OneDnnQuantizedMaxPoolOp<CPUDevice, TYPE>)
TF_CALL_qint8(REGISTER_KERNEL);
TF_CALL_quint8(REGISTER_KERNEL);

#endif  // INTEL_CPU_ONLY

}  // namespace itex
