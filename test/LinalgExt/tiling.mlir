// RUN: mlir-proto-opt -linalg-ext-tiling="tile-sizes=2,4" %s | FileCheck %s

func @reverse_1d_tensor(%arg0 : tensor<?x?xf32>) -> tensor<?x?xf32> {
  %c0 = arith.constant 0 : index
  %c1 = arith.constant 1 : index
  %d0 = tensor.dim %arg0, %c0 : tensor<?x?xf32>
  %d1 = tensor.dim %arg0, %c1 : tensor<?x?xf32>
  %init = linalg.init_tensor [%d0, %d1] : tensor<?x?xf32>
  %reverse = linalg_ext.reverse
      dimensions(dense<0> : tensor<1xi64>)
      ins(%arg0 : tensor<?x?xf32>)
      outs(%init : tensor<?x?xf32>) : tensor<?x?xf32>
  return %reverse : tensor<?x?xf32>
}
