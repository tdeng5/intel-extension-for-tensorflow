/* Copyright (c) 2023 Intel Corporation

Copyright 2021 The TensorFlow Authors. All Rights Reserved.

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
#ifndef ITEX_CORE_KERNELS_GPU_SPARSE_SEGMENT_REDUCTION_OPS_H_
#define ITEX_CORE_KERNELS_GPU_SPARSE_SEGMENT_REDUCTION_OPS_H_

#include "itex/core/kernels/gpu/segment_reduction_ops.h"
#include "itex/core/kernels/gpu/sparse_segment_reduction_util.h"
#include "itex/core/utils/bounds_check.h"
#include "itex/core/utils/op_requires.h"

namespace itex {
typedef Eigen::GpuDevice GPUDevice;

template <typename Device, typename T, typename Index, typename SegmentId>
class SparseSegmentReductionOpBase : public OpKernel {
 public:
  explicit SparseSegmentReductionOpBase(OpKernelConstruction* context,
                                        bool is_mean, bool is_sqrtn,
                                        bool has_num_segments, T default_value)
      : OpKernel(context) {}
};

template <typename Device, typename T, typename Index, typename SegmentId>
class SparseSegmentGradOpBase : public OpKernel {
 public:
  explicit SparseSegmentGradOpBase(OpKernelConstruction* context,
                                   SparseSegmentReductionOperation operation)
      : OpKernel(context) {}
};

template <typename T, typename Index, typename SegmentId>
class SparseSegmentReductionOpBase<GPUDevice, T, Index, SegmentId>
    : public OpKernel {
 public:
  explicit SparseSegmentReductionOpBase(OpKernelConstruction* context,
                                        bool is_mean, bool is_sqrtn,
                                        bool has_num_segments, T default_value)
      : OpKernel(context),
        is_mean_(is_mean),
        is_sqrtn_(is_sqrtn),
        has_num_segments_(has_num_segments),
        default_value_(default_value) {}

  void Compute(OpKernelContext* context) override {
    const Tensor& input = context->input(0);
    const Tensor& indices = context->input(1);
    const Tensor& segment_ids = context->input(2);

    OP_REQUIRES_OK(
        context, ValidateSparseSegmentReduction(
                     context, input, indices, segment_ids, has_num_segments_));

    SegmentId last_segment_id_host;
    auto device = context->eigen_gpu_device();
    auto stream = device.stream();

    if (has_num_segments_) {
      const Tensor& num_segments_t = context->input(3);
      SegmentId num_segments =
          internal::SubtleMustCopy(num_segments_t.dtype() == DT_INT32
                                       ? num_segments_t.scalar<int32>()()
                                       : num_segments_t.scalar<int64_t>()());
      last_segment_id_host = num_segments - 1;
    } else {
      const Index num_indices = static_cast<Index>(indices.NumElements());
      auto last_segment_id_device =
          const_cast<Tensor&>(segment_ids).template flat<SegmentId>().data() +
          (num_indices - 1);
      stream
          ->memcpy(&last_segment_id_host, last_segment_id_device,
                   sizeof(SegmentId))
          .wait();
    }

    SegmentId output_rows = last_segment_id_host + 1;
    OP_REQUIRES(context, output_rows > 0,
                errors::InvalidArgument("segment ids must be >= 0"));

    TensorShape output_shape = input.shape();
    output_shape.set_dim(0, output_rows);
    Tensor* output = nullptr;
    OP_REQUIRES_OK(context, context->allocate_output(0, output_shape, &output));

    auto input_flat = input.flat_outer_dims<T>();
    const auto indices_vec = indices.vec<Index>();
    const auto segment_ids_vec = segment_ids.vec<SegmentId>();

    functor::SparseSegmentReductionFunctor<T, Index, SegmentId,
                                           functor::NonAtomicSumOpGpu<float>,
                                           functor::SumOpGpu<float>>
        functor;
    // Atomic operators only support fp32 now.
    if (std::is_same<T, float>::value) {
      auto output_flat = output->flat_outer_dims<float>();
      OP_REQUIRES_OK(context,
                     functor(context, is_mean_, is_sqrtn_, default_value_,
                             input_flat, input.NumElements(), indices_vec,
                             segment_ids_vec, output_flat));
    } else {
      Tensor output_fp32;
      OP_REQUIRES_OK(context,
                     context->allocate_temp(DataTypeToEnum<float>::value,
                                            output_shape, &output_fp32));
      auto output_fp32_flat = output_fp32.flat_outer_dims<float>();
      OP_REQUIRES_OK(context,
                     functor(context, is_mean_, is_sqrtn_, default_value_,
                             input_flat, input.NumElements(), indices_vec,
                             segment_ids_vec, output_fp32_flat));
      ConvertFromFp32<GPUDevice, T>(device, output->NumElements(),
                                    static_cast<float*>(output_fp32.data()),
                                    static_cast<T*>(output->data()));
    }
  }

 private:
  const bool is_mean_;
  const bool is_sqrtn_;
  const bool has_num_segments_;
  const T default_value_;
};

template <typename Device, typename T, typename Index, typename SegmentId>
class SparseSegmentReductionMeanOp
    : public SparseSegmentReductionOpBase<Device, T, Index, SegmentId> {
 public:
  explicit SparseSegmentReductionMeanOp(OpKernelConstruction* context)
      : SparseSegmentReductionOpBase<Device, T, Index, SegmentId>(
            context, true /*is_mean*/, false /*is_sqrtn*/,
            false /* has_num_segments */, T(0) /* default_value */) {}
};

template <typename Device, typename T, typename Index, typename SegmentId>
class SparseSegmentReductionMeanWithNumSegmentsOp
    : public SparseSegmentReductionOpBase<Device, T, Index, SegmentId> {
 public:
  explicit SparseSegmentReductionMeanWithNumSegmentsOp(
      OpKernelConstruction* context)
      : SparseSegmentReductionOpBase<Device, T, Index, SegmentId>(
            context, true /*is_mean*/, false /*is_sqrtn*/,
            true /* has_num_segments */, T(0) /* default_value */) {}
};

template <typename Device, typename T, typename Index, typename SegmentId>
class SparseSegmentReductionSumOp
    : public SparseSegmentReductionOpBase<Device, T, Index, SegmentId> {
 public:
  explicit SparseSegmentReductionSumOp(OpKernelConstruction* context)
      : SparseSegmentReductionOpBase<Device, T, Index, SegmentId>(
            context, false /*is_mean*/, false /*is_sqrtn*/,
            false /* has_num_segments */, T(0) /* default_value */) {}
};

template <typename Device, typename T, typename Index, typename SegmentId>
class SparseSegmentReductionSumWithNumSegmentsOp
    : public SparseSegmentReductionOpBase<Device, T, Index, SegmentId> {
 public:
  explicit SparseSegmentReductionSumWithNumSegmentsOp(
      OpKernelConstruction* context)
      : SparseSegmentReductionOpBase<Device, T, Index, SegmentId>(
            context, false /*is_mean*/, false /*is_sqrtn*/,
            true /* has_num_segments */, T(0) /* default_value */) {}
};

template <typename Device, typename T, typename Index, typename SegmentId>
class SparseSegmentReductionSqrtNOp
    : public SparseSegmentReductionOpBase<Device, T, Index, SegmentId> {
 public:
  explicit SparseSegmentReductionSqrtNOp(OpKernelConstruction* context)
      : SparseSegmentReductionOpBase<Device, T, Index, SegmentId>(
            context, false /*is_mean*/, true /*is_sqrtn*/,
            false /* has_num_segments */, T(0) /* default_value */) {}
};

template <typename Device, typename T, typename Index, typename SegmentId>
class SparseSegmentReductionSqrtNWithNumSegmentsOp
    : public SparseSegmentReductionOpBase<Device, T, Index, SegmentId> {
 public:
  explicit SparseSegmentReductionSqrtNWithNumSegmentsOp(
      OpKernelConstruction* context)
      : SparseSegmentReductionOpBase<Device, T, Index, SegmentId>(
            context, false /*is_mean*/, true /*is_sqrtn*/,
            true /* has_num_segments */, T(0) /* default_value */) {}
};

// Implements the common logic for the gradients of SparseSegmentReduction
// kernels.
template <typename T, typename Index, typename SegmentId>
class SparseSegmentGradOpBase<GPUDevice, T, Index, SegmentId>
    : public OpKernel {
 public:
  explicit SparseSegmentGradOpBase(OpKernelConstruction* context,
                                   SparseSegmentReductionOperation operation)
      : OpKernel(context), operation_(operation) {}

  void Compute(OpKernelContext* context) override {
    const Tensor& input = context->input(0);
    const Tensor& indices = context->input(1);
    const Tensor& segment_ids = context->input(2);
    const Tensor& output_dim0 = context->input(3);
    const GPUDevice& device = context->eigen_gpu_device();

    OP_REQUIRES(context, TensorShapeUtils::IsVector(indices.shape()),
                errors::InvalidArgument("indices should be a vector."));
    OP_REQUIRES(context, TensorShapeUtils::IsVector(segment_ids.shape()),
                errors::InvalidArgument("segment_ids should be a vector."));
    OP_REQUIRES(context, TensorShapeUtils::IsScalar(output_dim0.shape()),
                errors::InvalidArgument("output_dim0 should be a scalar."));

    const int64_t N = indices.NumElements();
    OP_REQUIRES(context, N == segment_ids.NumElements(),
                errors::InvalidArgument(
                    "segment_ids and indices should have same size."));
    const SegmentId M = internal::SubtleMustCopy(output_dim0.scalar<int32>()());

    auto input_flat = input.flat_outer_dims<T>();
    const auto indices_vec = indices.vec<Index>();
    const auto segment_vec = segment_ids.vec<SegmentId>();

    TensorShape output_shape = input.shape();
    output_shape.set_dim(0, M);
    Tensor* output = nullptr;
    OP_REQUIRES_OK(context, context->allocate_output(0, output_shape, &output));
    if (M == 0 || N == 0) return;

    functor::SparseSegmentGradFunctor<T, Index, SegmentId,
                                      functor::NonAtomicSumOpGpu<float>,
                                      functor::SumOpGpu<float>>
        functor;
    // Atomic operators only support fp32 now.
    if (std::is_same<T, float>::value) {
      auto output_flat = output->flat_outer_dims<float>();
      functor(context, operation_, input_flat, indices_vec, segment_vec,
              output_flat);
    } else {
      Tensor output_fp32;
      OP_REQUIRES_OK(context,
                     context->allocate_temp(DataTypeToEnum<float>::value,
                                            output_shape, &output_fp32));
      auto output_fp32_flat = output_fp32.flat_outer_dims<float>();
      functor(context, operation_, input_flat, indices_vec, segment_vec,
              output_fp32_flat);
      ConvertFromFp32<GPUDevice, T>(device, output->NumElements(),
                                    static_cast<float*>(output_fp32.data()),
                                    static_cast<T*>(output->data()));
    }
  }

 private:
  const SparseSegmentReductionOperation operation_;
};

template <typename Device, class T, typename Index, typename SegmentId>
class SparseSegmentSumGradOp
    : public SparseSegmentGradOpBase<Device, T, Index, SegmentId> {
 public:
  explicit SparseSegmentSumGradOp(OpKernelConstruction* context)
      : SparseSegmentGradOpBase<Device, T, Index, SegmentId>(
            context, SparseSegmentReductionOperation::kSum) {}
};

template <typename Device, class T, typename Index, typename SegmentId>
class SparseSegmentMeanGradOp
    : public SparseSegmentGradOpBase<Device, T, Index, SegmentId> {
 public:
  explicit SparseSegmentMeanGradOp(OpKernelConstruction* context)
      : SparseSegmentGradOpBase<Device, T, Index, SegmentId>(
            context, SparseSegmentReductionOperation::kMean) {}
};

template <typename Device, class T, typename Index, typename SegmentId>
class SparseSegmentSqrtNGradOp
    : public SparseSegmentGradOpBase<Device, T, Index, SegmentId> {
 public:
  explicit SparseSegmentSqrtNGradOp(OpKernelConstruction* context)
      : SparseSegmentGradOpBase<Device, T, Index, SegmentId>(
            context, SparseSegmentReductionOperation::kSqrtN) {}
};

}  // namespace itex

#endif  // ITEX_CORE_KERNELS_GPU_SPARSE_SEGMENT_REDUCTION_OPS_H_
