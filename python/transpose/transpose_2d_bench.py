# RUN: %PYTHON %s 2>&1 | FileCheck %s

# This file contains small benchmarks with reasonably-sized problem/tiling sizes
# and codegen options.

from typing import Any, List, Optional, Sequence

from ..core.experts import *
from ..core.harness import *
from ..core.transforms import *

from .definitions import *
from .ops import *

fun_name = 'transpose_2d_on_tensors'
op_name = 'linalg.generic'

################################################################################
### Compilation strategies.
################################################################################


def maxCandidateThatDivides(candidates: List[int], value_to_divide: int):
  res = 0
  for c in candidates:
    if c > res and value_to_divide % c == 0:
      res = c
  return res


def maxCandidateSmallerThan(candidates: List[int], ub: int):
  res = 0
  for c in candidates:
    if c > res and c <= ub:
      res = c
  return res


def maxMultipleOfSmallerThan(n: int, ub: List[int]):
  return min(ub) - min(ub) % n


def all_experts(problem_sizes: List[int], transpose_avx2_lowering):
  candidateL1TileSizes1 = [
      24, 30, 32, 36, 40, 42, 48, 54, 60, 64, 80, 96, 120, 128
  ]
  candidateL1TileSizes2 = [
      24, 30, 32, 36, 40, 42, 48, 54, 60, 64, 80, 96, 120, 128
  ]
  sizes1 = [ \
    maxCandidateThatDivides(candidateL1TileSizes1, problem_sizes[0]), \
    maxCandidateThatDivides(candidateL1TileSizes2, problem_sizes[1])  \
  ]
  sizes_for_register_tiling = [ \
    ts if ts > 0 else s for (s, ts
                             ) in zip(problem_sizes, sizes1) \
  ]
  # candidateRegisterTileSizes1 = [1, 2]
  # candidateRegisterTileSizes1 = [1, 2, 4]
  # candidateRegisterTileSizes1 = [1, 2, 4, 6]
  candidateRegisterTileSizes1 = [1, 2, 4, 6, 8]
  candidateRegisterTileSizes2 = [1, 2, 4, 6, 8, 16]
  sizes2 = [ \
    maxCandidateThatDivides(candidateRegisterTileSizes1, sizes_for_register_tiling[0]), \
    maxCandidateThatDivides(candidateRegisterTileSizes2, sizes_for_register_tiling[1])  \
  ]
  return [
      SingleTilingExpert(
          fun_name=fun_name,
          op_name=op_name,
          sizes=sizes2,
          interchange=[],
          peel=[],
          pad=False,
          pack_paddings=[],
          hoist_paddings=[],
          # kwargs passed down to LowerVectors.
          # TODO: better composition of experts.
          transpose_lowering='shuffle',
          transpose_avx2_lowering=transpose_avx2_lowering,
          # Set to True to see the IR.
          print_ir_after_all=False),
      DoubleTilingExpert(
          fun_name=fun_name,
          op_name=op_name,
          sizes1=sizes1,
          interchange1=[],
          peel1=[],
          pad1=False,
          pack_paddings1=[],
          hoist_paddings1=[],
          sizes2=sizes2,
          interchange2=[],
          peel2=[],
          pad2=False,
          pack_paddings2=[0, 1],
          hoist_paddings2=[2, 2],
          # kwargs passed down to LowerVectors.
          # TODO: better composition of experts.
          transpose_lowering='shuffle',
          transpose_avx2_lowering=transpose_avx2_lowering,
          # Set to True to see the IR.
          print_ir_after_all=False)
  ]


################################################################################
### Problem instantiations.
################################################################################

keys = ['M', 'N']


# CHECK-NOT: FAILURE
def main():
  n_iters = 1000
  problem_size_list = [
      # The objective of these problem sizes is to run an experiment where we
      # control register tile sizes to stress test different implementations of
      # vector.transpose while isolating the cases with boundary conditions.
      # The setup is such that our volume of data is close from 256^2, 512^2 and
      # 1024^2.

      ### Second dimension is a multiple of 8 but not 16.
      # First dimension is a multiple of 4 but not 6, 8, 12, 16.
      [4 * 65, 8 * 35],  # ca. 256^2
      # First dimension is a multiple of 6 but not 8, 12, 16
      [6 * 45, 8 * 35],  # ca. 256^2
      # First dimension is a multiple of 8 but not 12 or 16
      [8 * 35, 8 * 35],  # ca. 256^2
      # First dimension is a multiple of 12 but not 16
      [12 * 25, 8 * 35],  # ca. 256^2
      # First dimension is a multiple of 16
      [16 * 16, 8 * 35],  # ca. 256^2
      ### Second dimension is a multiple of 16.
      [4 * 65, 256],  # ca. 256^2
      [6 * 45, 256],  # ca. 256^2
      [8 * 35, 256],  # ca. 256^2
      [12 * 25, 256],  # ca. 256^2
      [16 * 16, 256],  # ca. 256^2

      ### Second dimension is a multiple of 8 but not 16.
      [4 * 145, 8 * 65],  # ca. 512^2
      [6 * 95, 8 * 65],  # ca. 512^2
      [8 * 65, 8 * 65],  # ca. 512^2
      [12 * 55, 8 * 65],  # ca. 512^2
      [16 * 32, 8 * 65],  # ca. 512^2
      ### Second dimension is a multiple of 16.
      [4 * 145, 512],  # ca. 512^2
      [6 * 95, 512],  # ca. 512^2
      [8 * 65, 512],  # ca. 512^2
      [12 * 55, 512],  # ca. 512^2
      [16 * 32, 512],  # ca. 512^2

      # Same idea for 1024^2.
      ### Second dimension is a multiple of 8 but not 16.
      [4 * 275, 8 * 145],  # ca. 1024^2
      [6 * 175, 8 * 145],  # ca. 1024^2
      [8 * 145, 8 * 145],  # ca. 1024^2
      [12 * 85, 8 * 145],  # ca. 1024^2
      [16 * 65, 8 * 145],  # ca. 1024^2
      ### Second dimension is a multiple of 16.
      [4 * 275, 1024],  # ca. 1024^2
      [6 * 175, 1024],  # ca. 1024^2
      [8 * 145, 1024],  # ca. 1024^2
      [12 * 85, 1024],  # ca. 1024^2
      [16 * 65, 1024],  # ca. 1024^2

      # TODO: this is too slow atm.
      # [4096, 4096],
      # [6912, 4608],
  ]

  for np_types in [[np.float32, np.float32]]:
    for problem_sizes in problem_size_list:
      compile_time_problem_sizes_dict = {
          k: v for k, v in zip(keys, problem_sizes)
      }
      runtime_problem_sizes_dict = compile_time_problem_sizes_dict
      # Init printing.
      print(f'\n#############################################################\n'
            f'Compile-time problem sizes {compile_time_problem_sizes_dict}\n'
            f'Runtime problem sizes {runtime_problem_sizes_dict}\n'
            f'Problem types {np_types}')

      for expert in \
          all_experts(problem_sizes, transpose_avx2_lowering=False) + \
          all_experts(problem_sizes, transpose_avx2_lowering=True):
        print(f'\nCompilation expert {expert}')
        if 'sizes1' in expert.__dict__.keys():
          print(
              f'\t sizes1 = {expert.__dict__["sizes1"]} sizes2 = {expert.__dict__["sizes2"]} '
          )
        if 'sizes' in expert.__dict__.keys():
          print(f'\t sizes = {expert.__dict__["sizes"]}')

        problem = ProblemInstance(
            problem_definition=TransposeNDProblem(
                permutation=[1, 0], op_builder=transpose_2d),
            problem_sizes_keys=keys,
            np_types=np_types)

        problem.compile(
            entry_point_name='main',
            fun_to_benchmark_name=fun_name,
            compile_time_problem_sizes_dict=compile_time_problem_sizes_dict,
            transform=expert,
            # Used to pipe through llvm-mca
            dump_ir_to_file='/tmp/abc.mlir')

        problem.run(
            n_iters=n_iters,
            entry_point_name='main',
            runtime_problem_sizes_dict=runtime_problem_sizes_dict,
            # Used to pipe through llvm-mca with the **actual JIT'ed object**.
            dump_obj_to_file='/tmp/abc.o')


if __name__ == '__main__':
  main()
