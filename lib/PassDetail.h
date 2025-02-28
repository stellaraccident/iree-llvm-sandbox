//===- PassDetail.h - Pass class details ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef RUNNERS_PASSDETAIL_H_
#define RUNNERS_PASSDETAIL_H_

#include "mlir/Dialect/Affine/IR/AffineOps.h"
#include "mlir/IR/Dialect.h"
#include "mlir/Pass/Pass.h"

namespace mlir {
// Forward declaration from Dialect.h
template <typename ConcreteDialect>
void registerDialect(DialectRegistry &registry);

namespace linalg {
class LinalgDialect;
}  // end namespace linalg

namespace scf {
class SCFDialect;
}  // end namespace scf

namespace memref {
class MemRefDialect;
}  // end namespace memref

namespace tensor {
class TensorDialect;
}  // end namespace tensor

namespace vector {
class VectorDialect;
}  // end namespace vector

#define GEN_PASS_CLASSES
#include "Passes.h.inc"

}  // end namespace mlir

#endif  // RUNNERS_PASSDETAIL_H_
