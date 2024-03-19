/* Copyright (c) 2023 Intel Corporation

Copyright 2022 The TensorFlow Authors. All Rights Reserved.

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

#ifndef ITEX_CORE_IR_IMPORTEXPORT_GRAPHDEF_IMPORT_H_
#define ITEX_CORE_IR_IMPORTEXPORT_GRAPHDEF_IMPORT_H_

#include "mlir/IR/BuiltinOps.h"   // from @llvm-project
#include "mlir/IR/MLIRContext.h"  // from @llvm-project
#include "mlir/IR/OwningOpRef.h"  // from @llvm-project
// #include "tensorflow/core/framework/function.h"
#include "protos/graph.pb.h"
// #include "tensorflow/core/graph/graph.h"
#include "itex/core/utils/statusor.h"
#include "protos/graph_debug_info.pb.h"

namespace mlir {
namespace tfg {

// Convert a GraphDef directly to TFG.
itex::StatusOr<OwningOpRef<ModuleOp>> ImportGraphDef(
    MLIRContext* context, const itex::GraphDebugInfo& debug_info,
    const itex::GraphDef& graph_def);

// // Converts a graph and function library to a TFG module.
// itex::StatusOr<OwningOpRef<ModuleOp>> ImportGraphAndFunctionsToMlir(
//     MLIRContext *context, const itex::GraphDebugInfo &debug_info,
//     const itex::Graph &graph,
//     const itex::FunctionLibraryDefinition &flib_def);

}  // namespace tfg
}  // namespace mlir

#endif  // ITEX_CORE_IR_IMPORTEXPORT_GRAPHDEF_IMPORT_H_
