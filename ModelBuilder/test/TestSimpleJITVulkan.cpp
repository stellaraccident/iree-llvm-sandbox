//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// clang-format off

// NOLINTNEXTLINE
// TODO(thomasraoux): Set the right path to vulkan wrapper shared library. The
// test won't run until this is done.
// RUN: test-simple-jit-vulkan -vulkan-wrapper=$(dirname %s)/../../../../llvm/llvm-project/mlir/tools/libvulkan-runtime-wrappers.so 2>&1 | IreeFileCheck %s

// clang-format on

#include <string>

#include "ModelBuilder/ModelBuilder.h"
#include "ModelBuilder/ModelRunner.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/InitLLVM.h"
#include "mlir/Dialect/SPIRV/IR/TargetAndABI.h"
#include "mlir/ExecutionEngine/RunnerUtils.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/OperationSupport.h"
#include "mlir/Parser.h"

static llvm::cl::opt<std::string> vulkanWrapper(
    "vulkan-wrapper", llvm::cl::desc("Vulkan wrapper library"),
    llvm::cl::value_desc("filename"), llvm::cl::init("-"));

using namespace mlir;  // NOLINT

template <unsigned vecSize>
void testVectorAdd1d() {
  MLIRContext context;
  ModelBuilder modelBuilder;
  constexpr int workgroupSize = 32;
  auto typeA = modelBuilder.getMemRefType(vecSize, modelBuilder.f32);
  auto typeB = modelBuilder.getMemRefType(vecSize, modelBuilder.f32);
  auto typeC = modelBuilder.getMemRefType(vecSize, modelBuilder.f32);
  gpu::GPUFuncOp kernelFunc;
  {
    // create the GPU module.
    auto kernelModule = modelBuilder.makeGPUModule("kernels");
    // create kernel
    kernelFunc = modelBuilder.makeGPUKernel("kernel_add", kernelModule,
                                            {workgroupSize, 1, 1},
                                            {typeA, typeB, typeC});
    OpBuilder b(&kernelFunc.body());
    edsc::ScopedContext scope(b, kernelFunc.getLoc());

    MemRefIndexedValue A(kernelFunc.getArgument(0)),
        B(kernelFunc.getArgument(1)), C(kernelFunc.getArgument(2));
    auto ThreadIndex = b.create<gpu::ThreadIdOp>(
        modelBuilder.loc, b.getIndexType(), b.getStringAttr("x"));
    auto BlockIndex = b.create<gpu::BlockIdOp>(
        modelBuilder.loc, b.getIndexType(), b.getStringAttr("x"));
    auto GroupSize = b.create<gpu::BlockDimOp>(
        modelBuilder.loc, b.getIndexType(), b.getStringAttr("x"));
    Value Index = b.create<arith::AddIOp>(
        modelBuilder.loc, ThreadIndex,
        b.create<arith::MulIOp>(modelBuilder.loc, BlockIndex, GroupSize));
    C(Index) = A(Index) + B(Index);
    b.create<gpu::ReturnOp>(kernelFunc.getLoc());
  }
  const std::string funcName("add_dispatch");
  {
    // Add host side code, simple dispatch:
    auto f =
        modelBuilder.makeFunction(funcName, {}, {typeA, typeB, typeC},
                                  MLIRFuncOpConfig().setEmitCInterface(true));
    OpBuilder b(&f.getBody());
    edsc::ScopedContext scope(b, f.getLoc());
    auto wgx = std_constant_index(workgroupSize);
    auto one = std_constant_index(1);
    auto dispatchSizeX = std_constant_index(vecSize / workgroupSize);
    assert(vecSize % workgroupSize == 0);
    b.create<gpu::LaunchFuncOp>(
        f.getLoc(), kernelFunc, gpu::KernelDim3{dispatchSizeX, one, one},
        gpu::KernelDim3{wgx, one, one},
        b.create<arith::ConstantIndexOp>(f.getLoc(), 0),
        ValueRange({f.getArgument(0), f.getArgument(1), f.getArgument(2)}));
    std_ret();
  }

  // 2. Compile the function, pass in runtime support library
  //    to the execution engine for vector.print.
  ModelRunner runner(modelBuilder.getModuleRef(),
                     ModelRunner::Target::GPUTarget);
  runner.compile(CompilationOptions(), {vulkanWrapper});

  // 3. Allocate data within data structures that interoperate with the MLIR ABI
  // conventions used by codegen.
  auto oneInit = [](unsigned idx, float *ptr) { ptr[idx] = 1.0f; };
  auto incInit = [](unsigned idx, float *ptr) { ptr[idx] = 1.0f + idx; };
  auto zeroInit = [](unsigned idx, float *ptr) { ptr[idx] = 0.0f; };
  auto A = makeInitializedStridedMemRefDescriptor<float, 1>({vecSize}, oneInit);
  auto B = makeInitializedStridedMemRefDescriptor<float, 1>({vecSize}, incInit);
  auto C =
      makeInitializedStridedMemRefDescriptor<float, 1>({vecSize}, zeroInit);

  // 4. Call the funcOp named `funcName`.
  auto err = runner.invoke(funcName, A, B, C);
  if (err) llvm_unreachable("Error running function.");

  // 5. Print out the output buffer.
  ::impl::printMemRef(*C);
}

int main(int argc, char **argv) {
  // Allow LLVM setup through command line and parse the
  // test specific option for a runtime support library.
  llvm::InitLLVM y(argc, argv);
  llvm::cl::ParseCommandLineOptions(argc, argv, "TestSimpleJITVulkan\n");
  // clang-format off
  // CHECK: [2,  3,  4,  5,  6,  7,  8,  9,  10,  11,  12,  13,  14,  15,  16,
  // CHECK: 17,  18,  19,  20,  21,  22,  23,  24,  25,  26,  27,  28,  29,  30,
  // CHECK: 31,  32,  33]
  testVectorAdd1d<32>();
  // CHECK: [2,  3,  4,  5,  6,  7,  8,  9,  10,  11,  12,  13,  14,  15,  16,
  // CHECK: 17,  18,  19,  20,  21,  22,  23,  24,  25,  26,  27,  28,  29,  30,
  // CHECK: 31,  32,  33,  34,  35,  36,  37,  38,  39,  40,  41,  42,  43,  44,
  // CHECK: 45,  46,  47,  48,  49,  50,  51,  52,  53,  54,  55,  56,  57,  58,
  // CHECK: 59,  60,  61,  62,  63,  64,  65]
  testVectorAdd1d<64>();
}
