/*
 * Copyright (c) 2020 Samsung Electronics Co., Ltd. All Rights Reserved
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __ONERT_BACKEND_CPU_COMMON_BACKEND_CONTEXT_HELPERS_H__
#define __ONERT_BACKEND_CPU_COMMON_BACKEND_CONTEXT_HELPERS_H__

#include <vector>

#include "ir/Index.h"
#include "ir/OpSequences.h"
#include "ir/LowerInfoMap.h"
#include "util/logging.h"

namespace onert
{
namespace backend
{
namespace cpu_common
{

// TODO Remove the template param BackendContext once unification of cpu backend context is done
template <typename T_BackendContext>
void planTensors(const T_BackendContext &ctx, const std::vector<onert::ir::OpSequenceIndex> &order,
                 const ir::OpSequences &op_seqs, const ir::LowerInfoMap &lower_info)
{
  auto graph = ctx.graph();
  auto tensor_builder = ctx.tensor_builder;

  ir::OperandIndexMap<uint32_t> uses_map;
  ir::OperandIndexMap<uint32_t> def_map;
  ir::OperandIndexSequence constants;

  auto model_io =
    (graph->getInputs() + graph->getOutputs()) | ir::Remove::UNDEFINED | ir::Remove::DUPLICATED;

  // Prepare scanning
  for (auto ind : ctx.operand_list())
  {
    if (model_io.contains(ind))
      continue;
    const auto &obj = graph->operands().at(ind);
    const auto &li = lower_info.operand.at(ind);
    if (li->def_factors().getOnlyElement().backend() != ctx.backend())
      continue;

    // Ignore unused tensor
    if (li->def_factors().size() == 0 && li->use_factors().size() == 0)
    {
      VERBOSE_F() << "Operand #" << ind.value() << " will not be used. no more process."
                  << std::endl;
      return;
    }

    uses_map[ind] = obj.getUses().size();
    def_map[ind] = obj.getDef().valid() ? 1 : 0;

    if (obj.isConstant())
      constants.append(ind);

    auto factor = li->def_factors().getOnlyElement();
    if (!tensor_builder->isRegistered(ind))
    {
      // These tensors do not exist in any op_seq (No use and def)
      const auto info = obj.info();
      const auto backend_layout = factor.layout();
      // TODO Change tensor info to have permuted shape
      tensor_builder->registerTensorInfo(ind, info, backend_layout);
    }
  }

  // Start scanning to do notify{First|Last}Use for each tensor

  // If a tensor is a constant, increase the use of the tensor and allocate it first.
  // Increasing use count here makes the tensor never be deallocated, i.e it they will be
  // deallocated last.
  for (const auto &ind : constants)
  {
    uses_map[ind]++;
    tensor_builder->notifyFirstUse(ind);
  }

  // At each operation,
  // 1. Scan DEF of outputs. If the DEF, allocate it
  // 2. Scan DEF of inputs. If variable tensor, allocate it
  // 3. Scan USE of inputs. Decrease the USE and deallocate if the USE is 0
  for (const auto op_seq_ind : order)
  {
    const auto &op_seq = op_seqs.at(op_seq_ind);
    for (const auto &op_idx : op_seq.operations())
    {
      auto op_inputs =
        graph->operations().at(op_idx).getInputs() | ir::Remove::DUPLICATED | ir::Remove::UNDEFINED;
      auto op_outputs = graph->operations().at(op_idx).getOutputs() | ir::Remove::DUPLICATED |
                        ir::Remove::UNDEFINED;

      // Define outputs
      for (const auto &ind : op_outputs)
      {
        if (model_io.contains(ind))
          continue;
        if (!tensor_builder->isRegistered(ind))
          continue;
        assert(def_map.find(ind) != def_map.end());
        if (def_map[ind])
        {
          def_map[ind] = 0;
          tensor_builder->notifyFirstUse(ind);
        }
      }

      // Scan variable tensors
      // This tensor has features like constant. But OperandInfo and LowerInfo treat them as
      // non-constant because of less memory usage by memory planning in here
      for (const auto &ind : op_inputs)
      {
        if (model_io.contains(ind))
          continue;
        if (!tensor_builder->isRegistered(ind))
          continue;
        const auto &operand = graph->operands().at(ind);
        if (operand.info().isVariable())
        {
          // The variable tensor with buffer is not supported yet
          assert(operand.data() == nullptr);
          assert(operand.getUses().size() == 1 && !operand.getDef().valid());
          assert(lower_info.operand.at(ind)->def_factors().size() == 1 &&
                 lower_info.operand.at(ind)->use_factors().size() == 1);
          assert(uses_map[ind] == 1 && def_map[ind] == 0);
          tensor_builder->notifyFirstUse(ind);
        }
      }

      for (const auto &ind : op_inputs)
      {
        if (model_io.contains(ind))
          continue;
        if (!tensor_builder->isRegistered(ind))
          continue;
        assert(uses_map.find(ind) != uses_map.end());
        assert(uses_map[ind] > 0);
        uses_map[ind]--;
        if (uses_map[ind] == 0)
        {
          // plan for deallocation of static tensornode
          tensor_builder->notifyLastUse(ind);

          // plan for deallocation of dynamic tensor
          auto dyn_tensor_manager = tensor_builder->dynamicTensorManager();
          auto *tensor = ctx.tensor_registry->getITensor(ind);
          assert(tensor);
          dyn_tensor_manager->planDealloc(op_idx, tensor);
        }
      }
    }
  }

  // Dispose and validate
  for (const auto &ind : constants)
  {
    --uses_map[ind];
    if (uses_map[ind] == 0) // To prevent notifyLastUse from being called twice
    {
      tensor_builder->notifyLastUse(ind);
    }
  }

  assert(
    std::all_of(uses_map.begin(), uses_map.end(),
                [](std::pair<const ir::OperandIndex, uint32_t> it) { return it.second == 0; }));

  assert(
    std::all_of(def_map.begin(), def_map.end(),
                [](std::pair<const ir::OperandIndex, uint32_t> it) { return it.second == 0; }));
}

} // namespace cpu_common
} // namespace backend
} // namespace onert

#endif // __ONERT_BACKEND_CPU_COMMON_BACKEND_CONTEXT_HELPERS_H__
