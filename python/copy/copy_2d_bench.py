# RUN: %PYTHON %s 2>&1 | FileCheck %s

# This file contains small benchmarks with reasonably-sized problem/tiling sizes
# and codegen options.

from ..core.experts import *
from ..core.harness import *
from ..core.transforms import *

from .definitions import *

from typing import List

base_fun_name = 'copy_2d_on_tensors'
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


def all_experts(fun_name: str, problem_sizes: List[int]):
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
    ts if ts > 0 else s for (s, ts) in zip(problem_sizes, sizes1) \
  ]
  candidateRegisterTileSizes1 = [1, 2, 4, 8]
  candidateRegisterTileSizes2 = [1, 2, 4, 6, 8, 12, 16]
  sizes2 = [ \
    maxCandidateThatDivides(candidateRegisterTileSizes1, sizes_for_register_tiling[0]), \
    maxCandidateThatDivides(candidateRegisterTileSizes2, sizes_for_register_tiling[1])  \
  ]

  # Before bufferization, the IR only has a tensor.extract_slice /
  #   tensor.insert_slice pair.
  # Bufferization then properly introduces linalg.copy ops.
  # We want to make more these `linalg.copy` more efficient.
  # In the case of a single copy benchmark it is the one true thing to optimize.
  post_bufferization_transforms = [
      Vectorize(fun_name=fun_name, op_name='linalg.copy')
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
          # kwargs start here.
          post_bufferization_transforms=post_bufferization_transforms,
          # Set to True to see the IR.
          print_ir_after_all=False),
  ]


################################################################################
### Problem instantiations.
################################################################################

keys = ['M', 'N']

copy_2D_perf_search_list = [
    [32, 64],
]


# CHECK-NOT: FAILURE
def main():
  n_iters = 10000
  problem_size_list = copy_2D_perf_search_list
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

      fun_name = base_fun_name + '_offset_0' + \
          '_sizes' + ''.join('_' + str(sz) for sz in problem_sizes) + \
          '_strides_' + str(problem_sizes[1]) + '_1'
      for expert in all_experts(fun_name, problem_sizes):
        print(f'\nCompilation expert {expert}')
        if 'sizes1' in expert.__dict__.keys():
          print(f'\t sizes1 = {expert.__dict__["sizes1"]} '
                f'\t sizes2 = {expert.__dict__["sizes2"]} ')
        if 'sizes' in expert.__dict__.keys():
          print(f'\t sizes = {expert.__dict__["sizes"]}')

        problem = ProblemInstance(
            problem_definition=Copy2DProblem(),
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
