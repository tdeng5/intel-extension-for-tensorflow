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

#ifndef ITEX_CORE_GRAPH_CONFIG_UTIL_H_
#define ITEX_CORE_GRAPH_CONFIG_UTIL_H_

#include <string>

#include "itex/core/utils/protobuf/config.pb.h"

namespace itex {
void itex_set_config(const ConfigProto& config);
ConfigProto itex_get_config();
extern bool isxehpc_value;
ConfigProto itex_get_isxehpc();
extern bool hasxmx_value;
ConfigProto itex_get_hasxmx();
}  // namespace itex

#endif  // ITEX_CORE_GRAPH_CONFIG_UTIL_H_
