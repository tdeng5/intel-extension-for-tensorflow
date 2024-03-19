/* Copyright (c) 2023 Intel Corporation

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

#include "itex/core/kernels/common/transpose_op.h"
#include "itex/core/utils/register_types.h"

namespace itex {

// For fp8 transpose.
REGISTER_KERNEL_BUILDER(Name("Transpose")
                            .Device(DEVICE_GPU)
                            .TypeConstraint<int8>("T")
                            .HostMemory("perm"),
                        TransposeOp<GPUDevice, int8>);

}  // namespace itex
