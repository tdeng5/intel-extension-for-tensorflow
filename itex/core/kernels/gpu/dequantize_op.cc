/* Copyright (c) 2021-2022 Intel Corporation

Copyright 2015 The TensorFlow Authors. All Rights Reserved.

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

#include "itex/core/kernels/common/dequantize_op.h"

#include "itex/core/utils/register_types.h"

namespace itex {

#define REGISTER_KERNEL(TYPE)                                              \
  REGISTER_KERNEL_BUILDER(Name("Dequantize")                               \
                              .Device(DEVICE_GPU)                          \
                              .TypeConstraint<TYPE>("T")                   \
                              .TypeConstraint<Eigen::bfloat16>("dtype")    \
                              .HostMemory("min_range")                     \
                              .HostMemory("max_range"),                    \
                          DequantizeOp<GPUDevice, TYPE, Eigen::bfloat16>); \
  REGISTER_KERNEL_BUILDER(Name("Dequantize")                               \
                              .Device(DEVICE_GPU)                          \
                              .TypeConstraint<TYPE>("T")                   \
                              .TypeConstraint<Eigen::half>("dtype")        \
                              .HostMemory("min_range")                     \
                              .HostMemory("max_range"),                    \
                          DequantizeOp<GPUDevice, TYPE, Eigen::half>);     \
  REGISTER_KERNEL_BUILDER(Name("Dequantize")                               \
                              .Device(DEVICE_GPU)                          \
                              .TypeConstraint<TYPE>("T")                   \
                              .TypeConstraint<float>("dtype")              \
                              .HostMemory("min_range")                     \
                              .HostMemory("max_range"),                    \
                          DequantizeOp<GPUDevice, TYPE, float>);
TF_CALL_qint8(REGISTER_KERNEL);
TF_CALL_quint8(REGISTER_KERNEL);
#undef REGISTER_KERNEL

}  // namespace itex
