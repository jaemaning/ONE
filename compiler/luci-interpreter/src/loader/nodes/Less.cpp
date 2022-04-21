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

#include "kernels/Less.h"

namespace luci_interpreter
{

std::unique_ptr<Kernel> build_kernel_CircleLess(const luci::CircleNode *circle_node,
                                                KernelBuilderHelper &helper)
{
  const auto *node = loco::must_cast<const luci::CircleLess *>(circle_node);
  assert(node->arity() == 2);

  const Tensor *x = helper.getInputTensor(node->x());
  const Tensor *y = helper.getInputTensor(node->y());
  Tensor *output = helper.getOutputTensor(node);

  return std::make_unique<kernels::Less>(x, y, output);
}

} // namespace luci_interpreter
