// RUN: export M=32 && export N=64 && export K=128 && export ITERS=10 && \
// RUN: cat %p/matmul_f32_base.mlir | sed 's@${M}@'"$M"'@g'| sed 's@${K}@'"$K"'@g' | sed 's@${N}@'"$N"'@g'| sed 's@${ITERS}@'"$ITERS"'@g' |\

// RUN: mlir-proto-opt -linalg-tensor-codegen-strategy="anchor-func=init_and_matmul anchor-op=linalg.matmul tile-sizes=4,8,16 vectorize vector-contract-lowering=false" |\
// RUN: mlir-opt -linalg-comprehensive-module-bufferize |\
// RUN: tee | FileCheck %s

// CHECK-LABEL: func @init_and_matmul(
//  CHECK-SAME:       %[[A:[0-9a-zA-Z]+]]: memref<
//  CHECK-SAME:       %[[B:[0-9a-zA-Z]+]]: memref<
//  CHECK-SAME:       %[[C:[0-9a-zA-Z]+]]: memref<
//       CHECK:   constant 0.0
//   CHECK-NOT:   memref.alloc
// At function boundary we are still pessimistic, so spurious memref.cast to most dynamic strided memrefs are introduced.
// These will go away in the future.
//       CHECK:   linalg.fill(%{{.*}}, %[[C]]) : f32, memref<32x64xf32{{.*}}>
//   CHECK-NOT:   copy
//       CHECK:   scf.for %[[I:.*]] =
//       CHECK:     scf.for %[[J:.*]] =
//       CHECK:       %[[SVC:.*]] = memref.subview %[[C]]{{.*}} : memref<32x64xf32> to memref<4x8xf32
//       CHECK:       %[[VC:.*]] = vector.transfer_read %[[SVC]]{{.*}}{in_bounds = [true, true]} : memref<4x8xf32{{.*}}>, vector<4x8xf32>
//       CHECK:       scf.for %[[K:.*]] = {{.*}} iter_args(%{{.*}} = %[[VC]]) -> (vector<4x8xf32>)
//       CHECK:         %[[SVA:.*]] = memref.subview %[[A]][%[[I]], %[[K]]] [4, 16] [1, 1] : memref<32x128xf32> to memref<4x16xf32
//       CHECK:         %[[SVB:.*]] = memref.subview %[[B]][%[[K]], %[[J]]] [16, 8] [1, 1] : memref<128x64xf32> to memref<16x8xf32
//       CHECK:         vector.transfer_read %[[SVA]]{{.*}}, %{{.*}} {in_bounds = [true, true]} : memref<4x16xf32{{.*}}>, vector<4x16xf32>
//       CHECK:         vector.transfer_read %[[SVB]]{{.*}}, %{{.*}} {in_bounds = [true, true], permutation_map = {{.*}}} : memref<16x8xf32{{.*}}>, vector<8x16xf32>
//       CHECK:         vector.contract
//       CHECK:         scf.yield %{{.*}} : vector<4x8xf32>
//       CHECK:       }
//       CHECK:       vector.transfer_write %{{.*}}, %[[SVC]]{{.*}}{in_bounds = [true, true]} : vector<4x8xf32>, memref<4x8xf32
//   CHECK-NOT:       copy
//       CHECK:     }
//       CHECK:   }
//   CHECK-NOT:   copy

// CHECK-LABEL: func @exec(
