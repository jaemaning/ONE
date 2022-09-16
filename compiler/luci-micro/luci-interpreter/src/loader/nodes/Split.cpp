/*
 * Copyright (c) 2021 Samsung Electronics Co., Ltd. All Rights Reserved
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

#include "Builders.h"

#include "kernels/Split.h"

namespace luci_interpreter
{

std::unique_ptr<Kernel>
build_kernel_CircleSplit(std::vector<std::pair<const Tensor *, int32_t>> &inputs,
                         std::vector<std::pair<Tensor *, int32_t>> &outputs,
                         const uint32_t op_index, KernelBuilder &builder)
{
  assert(inputs.size() == 2);

  const Tensor *axis = inputs.at(0).first;
  const Tensor *input = inputs.at(1).first;
  std::vector<Tensor *> output_tensors(outputs.size());

  for (uint32_t i = 0; i < outputs.size(); ++i)
  {
    output_tensors[i] = outputs.at(i).first;
  }

  // NOTE 'num_splits' attribute is ignored.
  return std::make_unique<kernels::Split>(axis, input, std::move(output_tensors));
}

} // namespace luci_interpreter