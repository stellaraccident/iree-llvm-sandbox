//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// ModelRunner.h
// -----------------------------------------------------------------------------
//
// MLIR Model Runner
//
// The ModelRunner exposes relevant core MLIR and LLVM APIs that are sufficient
// to compile an mlir::ModuleOp. This set of classes and APIs encompass:
//  1. an mlir::ExecutionEngine engine
//  2. and llvm::TargetMachine targetMachine;
//  3. a `compile` function that takes optimization levels for the llvm opt and
//  llc tools and produces LLVMIR.
//
// Usage:
// ======
//
// ```
// // Create the builder and build some mlir::FuncOp
// ModelBuilder modelBuilder(...);
//
// // Compile the function.
// ModelRunner runner(modelBuilder.getModuleRef());
// runner.compile(/*llvmOptLevel=*/3, /*llcOptLevel=*/3);
//
// // Allocate data within data structures that interoperate with the MLIR ABI
// // conventions used by codegen.
// auto inputBuffer = ...;
// auto outputBuffer = ...;
//
// // Call the funcOp name `funcName` with arguments.
// runner.engine->invoke(funcName, ...);
// ```

#ifndef IREE_LLVM_SANDBOX_MODELBUILDER_MODELRUNNER_H_
#define IREE_LLVM_SANDBOX_MODELBUILDER_MODELRUNNER_H_

#include <functional>

#include "ModelBuilder/MemRefUtils.h"
#include "llvm/ADT/SmallVector.h"
#include "mlir/Dialect/Vector/VectorTransforms.h"
#include "mlir/ExecutionEngine/ExecutionEngine.h"
#include "mlir/IR/BuiltinOps.h"

namespace llvm {
class TargetMachine;
}  // namespace llvm

namespace mlir {
class PassManager;
class ExecutionEngine;

struct CompilationOptions {
  unsigned llvmOptLevel = 3;
  unsigned llcOptLevel = 3;
  vector::VectorTransformsOptions vectorTransformsOptions =
      vector::VectorTransformsOptions();
  std::function<void(mlir::PassManager &)> loweringPasses = nullptr;
};

class ModelRunner {
 public:
  enum class Target { CPUTarget, GPUTarget };
  // Initialize the runner with an OwningModuleRef, typically constructed with
  // a ModelBiulder.
  ModelRunner(mlir::OwningOpRef<mlir::ModuleOp> &m,
              Target t = Target::CPUTarget)
      : module(m), target(t) {}

  // Get the underlying ModuleOp.
  ModuleOp getOperation() { return *module; }

  // Compile the owned `module` into LLVMIR that can be passed to the buffer.
  // For now, the MLIR passes and transformations are kept to a minimum and only
  // perform straightforward lowering to LLVMIR.
  // An optional CompilationOptions object is passed to control special passes.
  // An optional array of shared runtime support libraries is passed to the
  // execution engine.
  // An optional array of extra symbols can be given.
  void compile(
      CompilationOptions compilationOptions,
      llvm::ArrayRef<const std::string> runtime = None,
      llvm::ArrayRef<std::pair<std::string, void *>> extra_symbols = None);

  // Reference to the compiled module.
  mlir::OwningOpRef<mlir::ModuleOp> &module;

  // Indirect invocation where the caller sets up the proper indirect pointers
  // and passes a void** `args` parameter.
  llvm::Error invokeIndirect(StringRef funcName, void **args) {
    const std::string adapterName =
        std::string("_mlir_ciface_") + funcName.str();
    return engine->invokePacked(adapterName,
                                llvm::MutableArrayRef<void *>{*args});
  }

  // Get the underlying data for a StridedMemRefType wrapped in a unique_ptr.
  // Used with SFINAE.
  template <typename T, typename Fun, int U>
  void *getData(std::unique_ptr<StridedMemRefType<T, U>, Fun> &arg) {
    return arg.get();
  }
  // Get the underlying data for an UnrankedMemRefType wrapped in a unique_ptr.
  // Used with SFINAE.
  template <typename T, typename Fun>
  void *getData(std::unique_ptr<::UnrankedMemRefType<T>, Fun> &arg) {
    return arg->descriptor;
  }
  // Direct invocation based on MemRefType which automatically packs the data.
  template <typename... Args>
  // TODO(suderman): Re-enable clang-format when new version migrates.
  // clang-format off
  llvm::Error invoke(StringRef funcName, Args &...args) {
    // clang-format on
    const std::string adapterName =
        std::string("_mlir_ciface_") + funcName.str();
    void *argsArray[] = {getData(args)...};
    std::array<void *, sizeof...(Args)> argsArray2;
    for (unsigned i = 0; i < sizeof...(Args); ++i)
      argsArray2[i] = &argsArray[i];
    return engine->invokePacked(adapterName,
                                llvm::MutableArrayRef<void *>{argsArray2});
  }

 protected:
  std::function<void(mlir::PassManager &)> getDefaultMLIRPassBuilder();
  void runLoweringPass(std::function<void(mlir::PassManager &)> passBuilder);

  Target target;
  // An execution engine and an associated target machine. The latter must
  // outlive the former since it may be used by the transformation layers.
  std::unique_ptr<mlir::ExecutionEngine> engine;
  std::unique_ptr<llvm::TargetMachine> targetMachine;
};

}  // namespace mlir

#endif  // IREE_LLVM_SANDBOX_MODELBUILDER_MODELRUNNER_H_
