//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Passes used by model builder tests to be able to auto-generate a dispatch
// wrapper for GPU module. This allows re-using linalg to Spirv conversion
// without having to deal with host code.
//===----------------------------------------------------------------------===//
#include "ModelBuilder/VulkanWrapperPass.h"

#include <cstdint>

#include "mlir/Dialect/SPIRV/IR/SPIRVOps.h"
#include "mlir/Dialect/SPIRV/IR/TargetAndABI.h"
#include "mlir/Dialect/StandardOps/IR/Ops.h"
#include "mlir/IR/Attributes.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Support/LLVM.h"
#include "mlir/Target/SPIRV/Serialization.h"

using namespace mlir;  // NOLINT

static constexpr const char *kSPIRVBlobAttrName = "spirv_blob";
static constexpr const char *kSPIRVEntryPointAttrName =
    "spirv_entry_point_name";
static constexpr const char *kVulkanLaunch = "vulkanLaunch";

namespace {

/// A pass that serialize a spirv::ModuloOp and create a dispatch call with
/// matching signature.
class AddVulkanLaunchWrapper
    : public PassWrapper<AddVulkanLaunchWrapper, OperationPass<ModuleOp>> {
 public:
  AddVulkanLaunchWrapper(ArrayRef<int64_t> workloadSize, ArrayRef<Type> args)
      : workloadSize(workloadSize.begin(), workloadSize.end()),
        args(args.begin(), args.end()) {}
  void runOnOperation() override;

 private:
  /// Creates a SPIR-V binary shader from the given `module` using
  /// `spirv::serialize` function.
  LogicalResult createBinaryShader(ModuleOp module,
                                   std::vector<char> &binaryShader);

  /// Adds an entry point with the matching function signature
  void convertGpuLaunchFunc(spirv::EntryPointOp entryPoint);

  /// Declares the vulkan launch function. Returns an error if the any type of
  /// operand is unsupported by Vulkan runtime.
  LogicalResult declareVulkanLaunchFunc(Location loc);

 private:
  SmallVector<int64_t, 3> workloadSize;
  SmallVector<Type, 4> args;
};

}  // anonymous namespace

void AddVulkanLaunchWrapper::runOnOperation() {
  bool done = false;
  getOperation().walk([this, &done](spirv::EntryPointOp op) {
    if (done) {
      op.emitError("should only contain one 'spv::EntryPointOp' op");
      return signalPassFailure();
    }
    done = true;
    convertGpuLaunchFunc(op);
  });

  // Erase `spirv::Module` operations.
  for (auto spirvModule :
       llvm::make_early_inc_range(getOperation().getOps<spirv::ModuleOp>()))
    spirvModule.erase();
}

LogicalResult AddVulkanLaunchWrapper::declareVulkanLaunchFunc(Location loc) {
  auto builder = OpBuilder::atBlockEnd(getOperation().getBody());

  SmallVector<Type, 8> vulkanLaunchTypes(3, builder.getIndexType());
  vulkanLaunchTypes.insert(vulkanLaunchTypes.end(), args.begin(), args.end());

  // Declare vulkan launch function.
  auto type = FunctionType::get(loc->getContext(), vulkanLaunchTypes, {});
  FuncOp vkLaunch = builder.create<FuncOp>(loc, kVulkanLaunch, type);
  vkLaunch.setPrivate();
  return success();
}

LogicalResult AddVulkanLaunchWrapper::createBinaryShader(
    ModuleOp module, std::vector<char> &binaryShader) {
  bool done = false;
  SmallVector<uint32_t, 0> binary;
  for (auto spirvModule : module.getOps<spirv::ModuleOp>()) {
    if (done)
      return spirvModule.emitError("should only contain one 'spv.module' op");
    done = true;

    if (failed(spirv::serialize(spirvModule, binary))) return failure();
  }
  binaryShader.resize(binary.size() * sizeof(uint32_t));
  std::memcpy(binaryShader.data(), reinterpret_cast<char *>(binary.data()),
              binaryShader.size());
  return success();
}

// TODO(thomaraoux): unify the logic with ConvertGpuLaunchFuncToVulkanLaunchFunc
// by moving it to a common helper function.
void AddVulkanLaunchWrapper::convertGpuLaunchFunc(
    spirv::EntryPointOp entryPoint) {
  ModuleOp module = getOperation();
  MLIRContext *ctx = module.getContext();
  Location loc = entryPoint.getLoc();

  // Get the workgroup size from spv.ExecutionMode.
  std::array<int64_t, 3> workgroupSize;
  bool done = false;
  getOperation().walk([this, &done, &workgroupSize](spirv::ExecutionModeOp op) {
    if (done) {
      op.emitError("should only contain one 'spv::ExecutionModeOp' op");
      return signalPassFailure();
    }
    done = true;
    for (int i = 0; i < op.values().size(); ++i) {
      workgroupSize[i] =
          op.values()[i].cast<IntegerAttr>().getValue().getZExtValue();
    }
  });

  // Serialize `spirv::Module` into binary form.
  std::vector<char> binary;
  if (failed(createBinaryShader(module, binary))) return signalPassFailure();

  FunctionType ft = FunctionType::get(ctx, args, {});
  std::string name = std::string(entryPoint.fn()) + "_wrapper";
  auto function = FuncOp::create(loc, name, ft);
  module.push_back(function);
  function.addEntryBlock();
  function->setAttr("llvm.emit_c_interface", mlir::UnitAttr::get(ctx));

  // Declare vulkan launch function.
  if (failed(declareVulkanLaunchFunc(loc))) return signalPassFailure();

  OpBuilder builder(function.getBody());
  std::vector<Value> arguments;
  // Calculate the number of groups to dispatch based on the workload size
  // and the workgroup size picked by the tiling pass.
  for (int i = 0; i < 3; i++) {
    auto dispatchSize = std::max(int64_t(1), workloadSize[i]);
    Value numGroups = builder.create<arith::ConstantIndexOp>(loc, dispatchSize);
    arguments.push_back(numGroups);
  }
  arguments.insert(arguments.end(), function.args_begin(), function.args_end());

  // Create vulkan launch call op.
  auto vulkanLaunchCallOp = builder.create<CallOp>(
      loc, ArrayRef<Type>{},
      FlatSymbolRefAttr::get(builder.getStringAttr(kVulkanLaunch)), arguments);

  // Set SPIR-V binary shader data as an attribute.
  vulkanLaunchCallOp->setAttr(
      kSPIRVBlobAttrName,
      StringAttr::get(loc->getContext(),
                      StringRef{binary.data(), binary.size()}));

  // Set entry point name as an attribute.
  vulkanLaunchCallOp->setAttr(
      kSPIRVEntryPointAttrName,
      StringAttr::get(loc->getContext(), entryPoint.fn()));

  builder.create<ReturnOp>(loc);
}

namespace {
/// A pass that serialize a spirv::ModuloOp and create a dispatch call with
/// matching signature.
class SetSpirvABI
    : public PassWrapper<SetSpirvABI, OperationPass<spirv::FuncOp>> {
 public:
  void runOnOperation() override {
    spirv::FuncOp f = this->getOperation();
    MLIRContext *context = &getContext();
    for (auto &argType : llvm::enumerate(f.getType().getInputs())) {
      Optional<spirv::StorageClass> sc;
      auto abiInfo =
          spirv::getInterfaceVarABIAttr(0, argType.index(), sc, context);
      f.setArgAttr(argType.index(), spirv::getInterfaceVarABIAttrName(),
                   abiInfo);
    }
  }
};

}  // anonymous namespace

std::unique_ptr<mlir::OperationPass<mlir::ModuleOp>>
mlir::createAddVulkanLaunchWrapperPass(llvm::ArrayRef<int64_t> workloadSize,
                                       ArrayRef<Type> args) {
  return std::make_unique<AddVulkanLaunchWrapper>(workloadSize, args);
}

std::unique_ptr<mlir::OperationPass<mlir::spirv::FuncOp>>
mlir::createSetSpirvABIPass() {
  return std::make_unique<SetSpirvABI>();
}
