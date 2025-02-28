//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// clang-format off

// NOLINTNEXTLINE
// RUN: test-dot-prod -runtime-support=$(dirname %s)/runtime-support.so 2>&1 | IreeFileCheck %s

// clang-format on

#include "ModelBuilder/MemRefUtils.h"
#include "ModelBuilder/ModelBuilder.h"
#include "ModelBuilder/ModelRunner.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/InitLLVM.h"

using namespace mlir;                    // NOLINT
using namespace mlir::edsc;              // NOLINT

static llvm::cl::opt<std::string> runtimeSupport(
    "runtime-support", llvm::cl::desc("Runtime support library filename"),
    llvm::cl::value_desc("filename"), llvm::cl::init("-"));

void DotProdOnVectors() {
  constexpr unsigned N = 4;

  ModelBuilder modelBuilder;
  // Build a func "dot_prod".
  constexpr StringLiteral funcName = "dot-prod";
  auto f32 = modelBuilder.f32;
  auto vectorType = modelBuilder.getVectorType(N, f32);
  auto refType = modelBuilder.getMemRefType(1, vectorType);

  auto func =
      modelBuilder.makeFunction(funcName, {}, {refType, refType},
                                MLIRFuncOpConfig().setEmitCInterface(true));

  SmallVector<AffineMap, 3> accesses;
  accesses.push_back(modelBuilder.getDimIdentityMap());
  accesses.push_back(accesses[0]);
  accesses.push_back(AffineMap::get(1, 0, modelBuilder.getContext()));

  SmallVector<Attribute, 1> iterator_types;
  iterator_types.push_back(modelBuilder.getStringAttr("reduction"));

  OpBuilder b(&func.getBody());
  edsc::ScopedContext scope(b, func.getLoc());
  Value A = func.getArgument(0), B = func.getArgument(1);
  Value idx_0 = std_constant_index(0);
  Value A_val = memref_load(A, idx_0);
  Value B_val = memref_load(B, idx_0);
  Value flt_0 = std_constant_float(APFloat(0.0f), f32);
  Value res_val = (vector_contract(A_val, B_val, flt_0,
                                   modelBuilder.getAffineMapArrayAttr(accesses),
                                   modelBuilder.getArrayAttr(iterator_types)));

  (vector_print(A_val));
  (vector_print(B_val));
  (vector_print(res_val));

  std_ret();

  // Compile the function, pass in runtime support library
  //    to the execution engine for vector.print.
  ModelRunner runner(modelBuilder.getModuleRef());
  runner.compile(CompilationOptions(), runtimeSupport);

  // initialize data by interoperating with the MLIR ABI by codegen.
  auto inputInit1 = [](unsigned idx, Vector1D<N, float> *ptr) {
    for (unsigned i = 0; i < N; ++i) ptr[idx][i] = 3.0 * i;
  };
  auto inputInit2 = [](unsigned idx, Vector1D<N, float> *ptr) {
    for (unsigned i = 0; i < N; ++i) ptr[idx][i] = 2.0 * i;
  };

  auto _A = makeInitializedStridedMemRefDescriptor<Vector1D<N, float>, 1>(
      {N}, inputInit1);
  auto _B = makeInitializedStridedMemRefDescriptor<Vector1D<N, float>, 1>(
      {N}, inputInit2);

  // Call the funcOp
  // CHECK: ( 0, 3, 6, 9 )
  // CHECK: ( 0, 2, 4, 6 )
  // CHECK: 84
  auto err = runner.invoke(funcName, _A, _B);

  if (err) llvm_unreachable("Error running function.");
}

int main(int argc, char **argv) {
  llvm::InitLLVM y(argc, argv);
  llvm::cl::ParseCommandLineOptions(argc, argv, "TestDotProd\n");
  DotProdOnVectors();
}
