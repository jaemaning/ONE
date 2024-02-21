/*
 * Copyright (c) 2024 Samsung Electronics Co., Ltd. All Rights Reserved
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "execute/OMUtils.h"
#include "execute/OMKernelExecutionBuilder.h"
#include "OMStatus.h"
#include "execute/OMRuntimeKernel.h"
#include "core/OMUtils.h"

#include "core/OMRuntimeShape.h"
#include "PALAdd.h"

using namespace onert_micro;
using namespace onert_micro::execute;

namespace
{

constexpr uint32_t input1TensorIdx = 0;
constexpr uint32_t input2TensorIdx = 1;
constexpr uint32_t outputTensorIdx = 0;

} // namespace

// NOTE: doesnt currently support dynamic shapes
OMStatus onert_micro::execute::execute_kernel_CircleAdd(const OMExecuteArgs &execute_args)
{
  core::OMRuntimeContext &runtime_context = execute_args.runtime_context;
  core::OMRuntimeStorage &runtime_storage = execute_args.runtime_storage;
  uint16_t op_index = execute_args.kernel_index;

  const circle::Tensor *input1;
  const circle::Tensor *input2;
  const circle::Tensor *output;

  uint8_t *input1_data;
  uint8_t *input2_data;
  uint8_t *output_data;

  uint16_t input1_index = 0;
  uint16_t input2_index = 0;

  const circle::AddOptions *options;
  // Read kernel
  {
    execute::OMRuntimeKernel runtime_kernel;
    runtime_kernel.readKernel(op_index, runtime_context);

    input1 = runtime_kernel.inputs[input1TensorIdx];
    input2 = runtime_kernel.inputs[input2TensorIdx];
    output = runtime_kernel.outputs[outputTensorIdx];
    assert(input1 != nullptr);
    assert(input2 != nullptr);
    assert(output != nullptr);

    runtime_kernel.getDataFromStorage(op_index, runtime_storage, runtime_context);

    input1_data = runtime_kernel.inputs_data[input1TensorIdx];
    input2_data = runtime_kernel.inputs_data[input2TensorIdx];
    output_data = runtime_kernel.outputs_data[outputTensorIdx];
    assert(input1_data != nullptr);
    assert(input2_data != nullptr);
    assert(output_data != nullptr);

    options = runtime_kernel.first_operator->builtin_options_as_AddOptions();

    input1_index = runtime_kernel.inputs_index[input1TensorIdx];
    input2_index = runtime_kernel.inputs_index[input2TensorIdx];
  }

  OMStatus status;

  core::OMRuntimeShape input1_shape(input1);
  core::OMRuntimeShape input2_shape(input2);
  core::OMRuntimeShape output_shape(output);

#ifndef DIS_DYN_SHAPES
  // Check dynamic shapes
  if (runtime_storage.getDynamicTensorSize(input1_index) != -1)
    input1_shape = output_shape;

  if (runtime_storage.getDynamicTensorSize(input2_index) != -1)
    input2_shape = output_shape;
#endif // DIS_DYN_SHAPES

  // Check broadcast property
  core::BinaryArithmeticBroadcastParams params{};
  const bool need_broadcast = pal::processBroadcastShapes(input1_shape, input2_shape, &params);

  switch (input1->type())
  {
#ifndef DIS_FLOAT
    case circle::TensorType_FLOAT32:
    {
      execute::calculateActivationRange(options->fused_activation_function(),
                                        &params.float_activation_min, &params.float_activation_max);
      if (need_broadcast)
      {
        status = pal::BroadcastAdd4DSlow(
          params, input1_shape, core::utils::castInputData<float>(input1_data), input2_shape,
          core::utils::castInputData<float>(input2_data), output_shape,
          core::utils::castOutputData<float>(output_data));
      }
      else
      {
        status =
          pal::Add(params, output_shape.flatSize(), core::utils::castInputData<float>(input1_data),
                   core::utils::castInputData<float>(input2_data),
                   core::utils::castOutputData<float>(output_data));
      }
    }
    break;
    case circle::TensorType_INT64:
    {
      execute::calculateActivationRange(options->fused_activation_function(),
                                        &params.int64_activation_min, &params.int64_activation_max);

      if (need_broadcast)
      {
        status = pal::BroadcastAdd4DSlow(
          params, input1_shape, core::utils::castInputData<int64_t>(input1_data), input2_shape,
          core::utils::castInputData<int64_t>(input2_data), output_shape,
          core::utils::castOutputData<int64_t>(output_data));
      }
      else
      {
        status = pal::Add(params, input1_shape.flatSize(),
                          core::utils::castInputData<int64_t>(input1_data),
                          core::utils::castInputData<int64_t>(input2_data),
                          core::utils::castOutputData<int64_t>(output_data));
      }
    }
    break;
    case circle::TensorType_INT32:
    {
      execute::calculateActivationRange(options->fused_activation_function(),
                                        &params.int32_activation_min, &params.int32_activation_max);

      if (need_broadcast)
      {
        status = pal::BroadcastAdd4DSlow(
          params, input1_shape, core::utils::castInputData<int32_t>(input1_data), input2_shape,
          core::utils::castInputData<int32_t>(input2_data), output_shape,
          core::utils::castOutputData<int32_t>(output_data));
      }
      else
      {
        status = pal::Add(params, input1_shape.flatSize(),
                          core::utils::castInputData<int32_t>(input1_data),
                          core::utils::castInputData<int32_t>(input2_data),
                          core::utils::castOutputData<int32_t>(output_data));
      }
    }
    break;
#endif // DIS_FLOAT
    default:
    {
      status = UnsupportedType;
      assert(false && "Unsupported type.");
    }
  }

  return status;
}
