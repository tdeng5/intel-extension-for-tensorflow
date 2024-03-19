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

#ifndef ITEX_CORE_KERNELS_GPU_SPARSE_SEGMENT_REDUCTION_UTIL_H_
#define ITEX_CORE_KERNELS_GPU_SPARSE_SEGMENT_REDUCTION_UTIL_H_

#include <algorithm>

#include "itex/core/kernels/gpu/segment_reduction_ops.h"
#include "itex/core/utils/op_kernel.h"
#include "itex/core/utils/plugin_tensor.h"

namespace itex {

// Type of SparseSegmentReduction operation to perform gradient of.
enum class SparseSegmentReductionOperation { kSum, kMean, kSqrtN };

namespace functor {
template <typename T, typename Index, typename SegmentId, typename ReductionF,
          typename AtomicReductionF>
struct SparseSegmentReductionFunctor {
  Status operator()(OpKernelContext* context, bool is_mean, bool is_sqrtn,
                    T default_value, typename TTypes<T, 2>::ConstTensor input,
                    Index data_size, typename TTypes<Index>::ConstVec indices,
                    typename TTypes<SegmentId>::ConstVec segment_ids,
                    typename TTypes<float, 2>::Tensor output);
};

template <typename T, typename Index, typename SegmentId, typename ReductionF,
          typename AtomicReductionF>
struct SparseSegmentGradFunctor {
  void operator()(OpKernelContext* context,
                  SparseSegmentReductionOperation operation,
                  typename TTypes<T>::ConstMatrix input_flat,
                  typename TTypes<Index>::ConstVec indices_vec,
                  typename TTypes<SegmentId>::ConstVec segment_vec,
                  typename TTypes<float>::Matrix output_flat);
};

}  // namespace functor

namespace impl {

template <typename T, typename Index, typename Tsegmentids>
struct SegmentWeightsKernel {
  SegmentWeightsKernel(Tsegmentids nsegments,
                       SparseSegmentReductionOperation operation,
                       const Index* segment_offsets, T* weights_ptr)
      : nsegments_(nsegments),
        operation_(operation),
        segment_offsets_(segment_offsets),
        weights_(weights_ptr) {}
  void operator()(sycl::nd_item<1> item) const {
    auto i = item.get_global_linear_id();
    if (i >= nsegments_) return;

    Index segment_size = segment_offsets_[i + 1] - segment_offsets_[i];
    segment_size = sycl::max(segment_size, Index(1));  // Avoid division by zero
    if (operation_ == SparseSegmentReductionOperation::kMean) {
      weights_[i] = T(1) / static_cast<T>(segment_size);
    } else if (operation_ == SparseSegmentReductionOperation::kSqrtN) {
      weights_[i] = T(1) / Eigen::numext::sqrt(static_cast<T>(segment_size));
    }
  }

 private:
  Tsegmentids nsegments_;
  SparseSegmentReductionOperation operation_;
  const Index* segment_offsets_;  // [nsegments + 1]
  T* weights_;
};

template <typename T, typename Index, typename SegmentId, typename ReductionF,
          typename AtomicReductionF, int OuterDimTileSize, typename = void>
struct SortedSegmentKernel {
  SortedSegmentKernel(Index input_outer_dim_size, Index inner_dim_size,
                      Index output_outer_dim_size, const Index* indices,
                      const T* weights, const SegmentId* segment_ids,
                      const Index* segment_offsets, const T* input,
                      float* output, Index total_stripe_count,
                      float initial_value, bool is_mean, bool is_sqrtn)
      : input_outer_dim_size(input_outer_dim_size),
        inner_dim_size(inner_dim_size),
        output_outer_dim_size(output_outer_dim_size),
        weights(weights),
        segment_ids(segment_ids),
        segment_offsets(segment_offsets),
        indices(indices),
        input(input),
        output(output),
        total_stripe_count(total_stripe_count),
        initial_value(initial_value),
        is_mean(is_mean),
        is_sqrtn(is_sqrtn) {}
  void operator()(sycl::nd_item<1> item) const {
    auto id = item.get_global_id(0);
    if (id >= total_stripe_count) return;
    auto segment_offset = id % inner_dim_size;
    auto input_outer_dim_index_base =
        id / inner_dim_size * Index(OuterDimTileSize);
    float reduce_res = initial_value;
    Index first_segment_id = segment_ids[input_outer_dim_index_base];
    Index last_output_segment_id = output_outer_dim_size;
    auto actual_stripe_height =
        Index(OuterDimTileSize) <
                (input_outer_dim_size - input_outer_dim_index_base)
            ? Index(OuterDimTileSize)
            : (input_outer_dim_size - input_outer_dim_index_base);
    ReductionF reduction_op;
    AtomicReductionF atom_reduction_op;
    for (Index j = 0; j < actual_stripe_height; j++) {
      Index current_output_segment_id =
          segment_ids[input_outer_dim_index_base + j];
      if (current_output_segment_id > last_output_segment_id) {
        if (is_mean) {
          const Index total_segment_number =
              segment_offsets[last_output_segment_id + 1] -
              segment_offsets[last_output_segment_id];
          if (total_segment_number) {
            reduce_res /= total_segment_number;
          }
        } else if (is_sqrtn) {
          const Index total_segment_number =
              segment_offsets[last_output_segment_id + 1] -
              segment_offsets[last_output_segment_id];
          if (total_segment_number) {
            reduce_res /=
                Eigen::numext::sqrt(static_cast<float>(total_segment_number));
          }
        }
        auto output_index =
            last_output_segment_id * inner_dim_size + segment_offset;
        if (last_output_segment_id == first_segment_id) {
          atom_reduction_op(output + output_index, &reduce_res);
        } else {
          reduction_op(output + output_index, &reduce_res);
        }
        reduce_res = initial_value;
      }
      float fetch_add;
      fetch_add = static_cast<float>(
          input[indices[input_outer_dim_index_base + j] * inner_dim_size +
                segment_offset]);
      // Apply weights if provided.
      if (weights) {
        fetch_add *= static_cast<float>(
            weights[indices[input_outer_dim_index_base + j]]);
      }
      reduction_op(&reduce_res, &fetch_add);

      last_output_segment_id = current_output_segment_id;
    }
    auto output_index =
        last_output_segment_id * inner_dim_size + segment_offset;

    if (is_mean) {
      const Index total_segment_number =
          segment_offsets[last_output_segment_id + 1] -
          segment_offsets[last_output_segment_id];

      if (total_segment_number) {
        reduce_res /= total_segment_number;
      }
    } else if (is_sqrtn) {
      const Index total_segment_number =
          segment_offsets[last_output_segment_id + 1] -
          segment_offsets[last_output_segment_id];

      if (total_segment_number) {
        reduce_res /=
            Eigen::numext::sqrt(static_cast<float>(total_segment_number));
      }
    }
    atom_reduction_op(output + output_index, &reduce_res);
  }

 private:
  Index input_outer_dim_size;
  Index inner_dim_size;
  Index output_outer_dim_size;
  const T* weights;
  const SegmentId* segment_ids;
  const Index* segment_offsets;
  const Index* indices;
  const T* input;
  float* output;
  Index total_stripe_count;
  float initial_value;
  bool is_mean;
  bool is_sqrtn;
};

}  // namespace impl

template <typename T, typename Index, typename Tsegmentids>
struct LaunchSegmentWeightsKernel {
  Status operator()(const Eigen::GpuDevice& d, Tsegmentids nsegments,
                    SparseSegmentReductionOperation operation,
                    const Index* segment_offsets, T* weights_ptr) {
    auto stream = d.stream();
    auto work_group_size =
        (*stream)
            .get_device()
            .template get_info<sycl::info::device::max_work_group_size>();
    auto total_size = nsegments;
    auto num_work_group = (total_size + work_group_size - 1) / work_group_size;

    sycl::range<1> local_size(work_group_size);
    sycl::range<1> global_size(num_work_group * work_group_size);
    stream->submit([&](sycl::handler& cgh) {
      impl::SegmentWeightsKernel<T, Index, Tsegmentids> task(
          nsegments, operation, segment_offsets, weights_ptr);
      cgh.parallel_for<impl::SegmentWeightsKernel<T, Index, Tsegmentids>>(
          sycl::nd_range<1>(global_size, local_size), task);
    });
    return Status::OK();
  }
};

template <typename T, typename Index, typename SegmentId, int OuterDimTileSize,
          typename ReductionF, typename AtomicReductionF>
struct LaunchSortedSegmentKernel {
  Status operator()(const Eigen::GpuDevice& device,
                    const Index input_outer_dim_size,
                    const Index inner_dim_size,
                    const Index output_outer_dim_size, const Index* indices,
                    const T* weights, const SegmentId* segment_ids,
                    const Index* segment_offset, const T* input, float* output,
                    const Index total_stripe_count, const float initial_value,
                    bool is_mean, bool is_sqrtn) {
    auto stream = device.stream();
    auto work_group_size =
        (*stream)
            .get_device()
            .template get_info<sycl::info::device::max_work_group_size>();
    auto num_work_group =
        (total_stripe_count + work_group_size - 1) / work_group_size;

    sycl::range<1> local_size(work_group_size);

    sycl::range<1> global_size(num_work_group * work_group_size);
    stream->submit([&](sycl::handler& cgh) {
      impl::SortedSegmentKernel<T, Index, SegmentId, ReductionF,
                                AtomicReductionF, OuterDimTileSize>
          task(input_outer_dim_size, inner_dim_size, output_outer_dim_size,
               indices, weights, segment_ids, segment_offset, input, output,
               total_stripe_count, initial_value, is_mean, is_sqrtn);
      cgh.parallel_for<impl::SortedSegmentKernel<
          T, Index, SegmentId, ReductionF, AtomicReductionF, OuterDimTileSize>>(
          sycl::nd_range<1>(global_size, local_size), task);
    });

    return Status::OK();
  }
};

Status ValidateSegmentReduction(OpKernelContext* c, const Tensor& input,
                                const Tensor& segment_ids);
Status ValidateUnsortedSegmentReduction(OpKernel* op_kernel,
                                        OpKernelContext* context,
                                        const Tensor& data,
                                        const Tensor& segment_ids,
                                        const Tensor& num_segments);
Status ValidateSparseSegmentReduction(OpKernelContext* context,
                                      const Tensor& input,
                                      const Tensor& indices,
                                      const Tensor& segment_ids,
                                      bool has_num_segments);

}  // namespace itex
#endif  // ITEX_CORE_KERNELS_GPU_SPARSE_SEGMENT_REDUCTION_UTIL_H_
