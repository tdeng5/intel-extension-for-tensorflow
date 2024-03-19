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

#ifndef ITEX_CORE_UTILS_DEVICE_TYPES_H_
#define ITEX_CORE_UTILS_DEVICE_TYPES_H_

#include "itex/core/utils/macros.h"

namespace itex {

// Convenient constants that can be passed to a DeviceType constructor
TF_EXPORT extern const char* const DEVICE_DEFAULT;  // "DEFAULT"
TF_EXPORT extern const char* const DEVICE_CPU;      // "CPU"
TF_EXPORT extern const char* const DEVICE_GPU;      // "GPU"
TF_EXPORT extern const char* const DEVICE_XPU;
TF_EXPORT extern const char* const DEVICE_AUTO;  // "AUTO"

}  // namespace itex
#endif  // ITEX_CORE_UTILS_DEVICE_TYPES_H_
