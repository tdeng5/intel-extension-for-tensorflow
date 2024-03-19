/* Copyright (c) 2023 Intel Corporation

Copyright 2017 The TensorFlow Authors. All Rights Reserved.

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
#include "itex/core/kernels/gpu/ragged_fill_empty_rows_op.h"

#include <limits>

#include "itex/core/kernels/common/fill_functor.h"
#include "itex/core/kernels/gpu/fill_empty_rows_functor.h"
#include "itex/core/kernels/gpu/scan_ops_gpu.h"
#include "itex/core/kernels/gpu/sparse_fill_empty_rows_op.h"
#include "itex/core/kernels/gpu/unique_op.h"
#include "itex/core/utils/bits.h"
#include "itex/core/utils/bounds_check.h"
#include "itex/core/utils/op_kernel.h"
#include "itex/core/utils/op_requires.h"
#include "itex/core/utils/plugin_tensor.h"
#include "itex/core/utils/register_types.h"
#include "itex/core/utils/tensor_types.h"
#include "third_party/build_option/dpcpp/runtime/eigen_itex_gpu_runtime.h"
#include "third_party/eigen3/unsupported/Eigen/CXX11/Tensor"

namespace itex {

using GPUDevice = Eigen::GpuDevice;

namespace functor {
template <typename T, typename Tindex, bool RaggedOperands>
struct FillEmptyRows<GPUDevice, T, Tindex, RaggedOperands> {
  Status operator()(OpKernelContext* context, const Tensor& default_value_t,
                    const Tensor& indices_t, const Tensor& values_t,
                    const Tensor& dense_shape_t) {
    const int kEmptyRowIndicatorOutput = 2;
    const auto default_value = default_value_t.scalar<T>();
    const auto indices = indices_t.matrix<Tindex>();
    const auto values = values_t.vec<T>();
    const auto dense_shape = dense_shape_t.vec<Tindex>();

    const Tindex indices_num = indices_t.shape().dim_size(0);
    const int rank = indices_t.shape().dim_size(1);
    const Tindex dense_rows = dense_shape(0);  // Must be on the host

    if (dense_rows == 0) {
      Tindex* output_indices;
      T* output_values;
      Tindex* reverse_index_map;
      TF_RETURN_IF_ERROR(AllocateOutputsExceptEmptyRowIndicator(
          context, indices_num, rank, /*num_empty_rows=*/0, &output_indices,
          &output_values, &reverse_index_map));
      // TODO(itex): Check whether output is required when output_required API
      // can be supported.
      Tensor* unused = nullptr;
      TF_RETURN_IF_ERROR(context->allocate_output(kEmptyRowIndicatorOutput,
                                                  TensorShape({0}), &unused));
      return Status::OK();
    }
    auto device = context->eigen_gpu_device();
    auto stream = device.stream();

    Tensor elements_per_row_t;
    DataType index_type = DataTypeToEnum<Tindex>::value;

    TF_RETURN_IF_ERROR(context->allocate_temp(
        index_type, TensorShape({dense_rows}), &elements_per_row_t));

    auto elements_per_row = elements_per_row_t.flat<Tindex>();
    functor::SetZeroFunctor<GPUDevice, Tindex>()(context->eigen_gpu_device(),
                                                 elements_per_row);

    Tensor rows_are_not_ordered_t;
    TF_RETURN_IF_ERROR(context->allocate_temp(index_type, TensorShape({1}),
                                              &rows_are_not_ordered_t));
    auto rows_are_not_ordered = rows_are_not_ordered_t.flat<Tindex>();
    functor::SetZeroFunctor<GPUDevice, Tindex>()(context->eigen_gpu_device(),
                                                 rows_are_not_ordered);

    Tensor first_invalid_index_t;
    TF_RETURN_IF_ERROR(context->allocate_temp(index_type, TensorShape({1}),
                                              &first_invalid_index_t));
    auto first_invalid_index = first_invalid_index_t.flat<Tindex>();
    const Tindex kAllIndicesValid = std::numeric_limits<Tindex>::max();
    stream->fill<Tindex>(first_invalid_index.data(), kAllIndicesValid, 1);

    if (indices_num > 0) {
      TF_RETURN_IF_ERROR(LaunchCountElementsPerRowKernel<Tindex>(
          /*device=*/device, /*size=*/indices_num, dense_rows, rank,
          indices.data(), elements_per_row.data(), rows_are_not_ordered.data(),
          first_invalid_index.data()));
    }

    Tensor input_row_ends_t;
    TF_RETURN_IF_ERROR(context->allocate_temp(
        index_type, TensorShape({dense_rows}), &input_row_ends_t));
    auto input_row_ends = input_row_ends_t.flat<Tindex>();

    TF_RETURN_IF_ERROR(launchFullScan(context,
                                      /*input=*/elements_per_row.data(),
                                      /*output=*/input_row_ends.data(),
                                      /*init=*/Tindex(0),
                                      /*operator*/ sycl::plus<Tindex>(),
                                      /*is_exclusive*/ false,
                                      /*is_reverse*/ false,
                                      /*size=*/dense_rows));

    bool* empty_row_indicator;
    Tensor* empty_row_indicator_t_ptr = nullptr;
    TF_RETURN_IF_ERROR(context->allocate_output(kEmptyRowIndicatorOutput,
                                                TensorShape({dense_rows}),
                                                &empty_row_indicator_t_ptr));
    empty_row_indicator = empty_row_indicator_t_ptr->vec<bool>().data();
    if (dense_rows > 0) {
      TF_RETURN_IF_ERROR(LaunchComputeEmptyRowIndicatorKernel<Tindex>(
          /*device=*/device, /*size=*/dense_rows, elements_per_row.data(),
          empty_row_indicator));
    }

    // For each row, the number of empty rows up to and including that row.
    Tensor num_empty_rows_through_t;
    TF_RETURN_IF_ERROR(context->allocate_temp(
        index_type, TensorShape({dense_rows}), &num_empty_rows_through_t));
    auto num_empty_rows_through = num_empty_rows_through_t.flat<Tindex>();

    TF_RETURN_IF_ERROR(launchFullScan(context, /*input=*/empty_row_indicator,
                                      /*output=*/num_empty_rows_through.data(),
                                      /*init=*/Tindex(0),
                                      /*operator*/ sycl::plus<Tindex>(),
                                      /*is_exclusive*/ false,
                                      /*is_reverse*/ false,
                                      /*size=*/dense_rows));

    Tindex num_empty_rows_host;
    stream
        ->memcpy(&num_empty_rows_host,
                 num_empty_rows_through.data() + (dense_rows - 1),
                 sizeof(Tindex))
        .wait();

    Tindex rows_are_not_ordered_host;
    stream
        ->memcpy(&rows_are_not_ordered_host, rows_are_not_ordered.data(),
                 sizeof(Tindex))
        .wait();

    Tindex first_invalid_index_host;
    stream
        ->memcpy(&first_invalid_index_host, first_invalid_index.data(),
                 sizeof(Tindex))
        .wait();

    OP_REQUIRES_RETURN_STATUS(
        context, first_invalid_index_host == kAllIndicesValid,
        errors::InvalidArgument("indices(", first_invalid_index_host,
                                ", 0) is invalid."));

    Tindex* output_indices;
    T* output_values;
    Tindex* reverse_index_map;

    TF_RETURN_IF_ERROR(AllocateOutputsExceptEmptyRowIndicator(
        context, indices_num, rank, num_empty_rows_host, &output_indices,
        &output_values, &reverse_index_map));
    Tindex* input_index_map = nullptr;
    Tensor input_index_map_t;
    if (rows_are_not_ordered_host) {
      TF_RETURN_IF_ERROR(ArgSortByRows(context, device, indices_num, rank,
                                       dense_rows, indices,
                                       &input_index_map_t));
      input_index_map = input_index_map_t.vec<Tindex>().data();
    }
    if (indices_num > 0) {
      TF_RETURN_IF_ERROR(LaunchScatterInputElementsKernel<T, Tindex>(
          /*device=*/device, /*size=*/indices_num, dense_rows, rank,
          input_index_map, indices.data(), values.data(),
          num_empty_rows_through.data(), output_indices, output_values,
          reverse_index_map));
    }
    if (dense_rows > 0) {
      TF_RETURN_IF_ERROR(LaunchScatterNewElementsKernel<T, Tindex>(
          device, dense_rows, rank, default_value.data(), input_index_map,
          num_empty_rows_through.data(), input_row_ends.data(),
          empty_row_indicator, output_indices, output_values));
    }
    return Status::OK();
  }

 private:
  Status AllocateOutputsExceptEmptyRowIndicator(
      OpKernelContext* context, Tindex N, int rank, Tindex num_empty_rows,
      Tindex** output_indices, T** output_values, Tindex** reverse_index_map) {
    Tensor* output_indices_t;
    const int kOutputIndices = 0;
    const int kOutputValues = 1;
    const Tindex N_full = N + num_empty_rows;
    if constexpr (RaggedOperands) {
      TensorShape output_indices_shape({N_full});
      TF_RETURN_IF_ERROR(context->allocate_output(
          kOutputIndices, output_indices_shape, &output_indices_t));
    } else {
      TensorShape output_indices_shape({N_full, rank});
      TF_RETURN_IF_ERROR(context->allocate_output(
          kOutputIndices, output_indices_shape, &output_indices_t));
    }
    *output_indices = output_indices_t->matrix<Tindex>().data();

    Tensor* output_values_t;
    TF_RETURN_IF_ERROR(context->allocate_output(
        kOutputValues, TensorShape({N_full}), &output_values_t));
    *output_values = output_values_t->vec<T>().data();

    // TODO(itex): Check whether output is required when output_required API can
    // be supported.
    const int kReverseIndexMapOutput = 3;
    Tensor* reverse_index_map_t = nullptr;
    TF_RETURN_IF_ERROR(context->allocate_output(
        kReverseIndexMapOutput, TensorShape({N}), &reverse_index_map_t));
    *reverse_index_map = reverse_index_map_t->vec<Tindex>().data();
    return Status::OK();
  }

  Status ArgSortByRows(OpKernelContext* context, const GPUDevice& device,
                       Tindex N, int rank, Tindex dense_rows,
                       typename TTypes<Tindex>::ConstMatrix indices,
                       Tensor* input_index_map_t) {
    DataType index_type = DataTypeToEnum<Tindex>::value;
    // Extract row indices into separate array for use as keys for sorting.
    Tensor row_indices_t;
    TF_RETURN_IF_ERROR(
        context->allocate_temp(index_type, TensorShape({N}), &row_indices_t));
    auto row_indices = row_indices_t.flat<Tindex>();
    if (N > 0) {
      TF_RETURN_IF_ERROR(LaunchCopyRowIndicesKernel<Tindex>(
          /*device=*/device, /*size=*/N, rank, indices.data(),
          row_indices.data()));
    }
    // Allocate input_index_map.
    TF_RETURN_IF_ERROR(context->allocate_temp(index_type, TensorShape({N}),
                                              input_index_map_t));
    Tindex* input_index_map = input_index_map_t->flat<Tindex>().data();
    return itex::impl::DispatchRadixSort<Tindex, Tindex, /*KEYS_PER_ITEM=*/8,
                                         /*GROUP_SIZE=*/256,
                                         /*SUBGROUP_SIZE*/ 16>(
        context, N,
        /*keys_in = */ row_indices.data(),
        /*indices_in = */ static_cast<Tindex*>(nullptr),
        /*keys_out = */ static_cast<Tindex*>(nullptr),
        /*indices_out = */ input_index_map,
        /*num_bits = */ Log2Ceiling64(dense_rows));
  }
};

}  // namespace functor

template <typename T, typename Tindex>
class RaggedFillEmptyRowsGPUOp : public OpKernel {
 public:
  explicit RaggedFillEmptyRowsGPUOp(OpKernelConstruction* context)
      : OpKernel(context) {}

  void Compute(OpKernelContext* context) override {
    const int kValueRowidsInput = 0;
    const int kValuesInput = 1;
    const int kNRowsInput = 2;
    const int kDefaultValueInput = 3;

    const Tensor& value_rowids_t = context->input(kValueRowidsInput);
    const Tensor& values_t = context->input(kValuesInput);
    const Tensor& nrows_t = context->input(kNRowsInput);
    const Tensor& default_value_t = context->input(kDefaultValueInput);

    OP_REQUIRES(context, TensorShapeUtils::IsScalar(nrows_t.shape()),
                errors::InvalidArgument("nrows must be a scalar, saw: ",
                                        nrows_t.shape().DebugString()));
    OP_REQUIRES(context, TensorShapeUtils::IsVector(value_rowids_t.shape()),
                errors::InvalidArgument("value_rowids must be a vector, saw: ",
                                        value_rowids_t.shape().DebugString()));
    OP_REQUIRES(context, TensorShapeUtils::IsVector(values_t.shape()),
                errors::InvalidArgument("values must be a vector, saw: ",
                                        values_t.shape().DebugString()));
    OP_REQUIRES(context, value_rowids_t.dim_size(0) == values_t.dim_size(0),
                errors::InvalidArgument(
                    "The length of `values` (", values_t.dim_size(0),
                    ") must match the first dimension of `value_rowids` (",
                    value_rowids_t.dim_size(0), ")."));
    OP_REQUIRES(context, TensorShapeUtils::IsScalar(default_value_t.shape()),
                errors::InvalidArgument("default_value must be a scalar, saw: ",
                                        default_value_t.shape().DebugString()));

    using FunctorType =
        functor::FillEmptyRows<GPUDevice, T, Tindex, /*RaggedOperands=*/true>;
    OP_REQUIRES_OK(context, FunctorType()(context, default_value_t,
                                          value_rowids_t, values_t, nrows_t));
  }
};

#define REGISTER_KERNELS(T, Tindex)                    \
  REGISTER_KERNEL_BUILDER(Name("RaggedFillEmptyRows")  \
                              .Device(DEVICE_GPU)      \
                              .HostMemory("nrows")     \
                              .TypeConstraint<T>("T"), \
                          RaggedFillEmptyRowsGPUOp<T, Tindex>)

#define REGISTER_KERNELS_TINDEX(T) REGISTER_KERNELS(T, int64)
TF_CALL_INTEGRAL_TYPES(REGISTER_KERNELS_TINDEX);
TF_CALL_GPU_NUMBER_TYPES(REGISTER_KERNELS_TINDEX);
TF_CALL_bool(REGISTER_KERNELS_TINDEX);
#undef REGISTER_KERNELS_TINDEX
#undef REGISTER_KERNELS

template <typename Device, typename T, typename Tindex>
class RaggedFillEmptyRowsGradOp : public OpKernel {
 public:
  explicit RaggedFillEmptyRowsGradOp(OpKernelConstruction* context)
      : OpKernel(context) {}

  void Compute(OpKernelContext* context) override {
    const Tensor* reverse_index_map_t;
    const Tensor* grad_values_t;
    OP_REQUIRES_OK(context,
                   context->input("reverse_index_map", &reverse_index_map_t));
    OP_REQUIRES_OK(context, context->input("grad_values", &grad_values_t));

    OP_REQUIRES(
        context, TensorShapeUtils::IsVector(reverse_index_map_t->shape()),
        errors::InvalidArgument("reverse_index_map must be a vector, saw: ",
                                reverse_index_map_t->shape().DebugString()));
    OP_REQUIRES(context, TensorShapeUtils::IsVector(grad_values_t->shape()),
                errors::InvalidArgument("grad_values must be a vector, saw: ",
                                        grad_values_t->shape().DebugString()));

    const auto reverse_index_map = reverse_index_map_t->vec<Tindex>();
    const auto grad_values = grad_values_t->vec<T>();

    const Tindex N = reverse_index_map_t->shape().dim_size(0);

    Tensor* d_values_t;
    OP_REQUIRES_OK(context,
                   context->allocate_output(0, TensorShape({N}), &d_values_t));
    auto d_values = d_values_t->vec<T>();
    Tensor* d_default_value_t;
    OP_REQUIRES_OK(context, context->allocate_output(1, TensorShape({}),
                                                     &d_default_value_t));
    auto d_default_value = d_default_value_t->scalar<T>();

    OP_REQUIRES_OK(context, functor::FillEmptyRowsGrad<Device, T, Tindex>()(
                                context, reverse_index_map, grad_values,
                                d_values, d_default_value));
  }
};

#define REGISTER_KERNELS(D, T, Tindex)                    \
  REGISTER_KERNEL_BUILDER(Name("RaggedFillEmptyRowsGrad") \
                              .Device(DEVICE_##D)         \
                              .TypeConstraint<T>("T"),    \
                          RaggedFillEmptyRowsGradOp<D##Device, T, Tindex>)

#define REGISTER_GPU_KERNELS(T) REGISTER_KERNELS(GPU, T, int64)
// TODO(itex): add bf16/fp16 back when eigen::vec is ready
TF_CALL_INTEGRAL_TYPES(REGISTER_GPU_KERNELS);
TF_CALL_float(REGISTER_GPU_KERNELS);
TF_CALL_double(REGISTER_GPU_KERNELS);
#undef REGISTER_GPU_KERNELS
#undef REGISTER_KERNELS

}  // namespace itex
