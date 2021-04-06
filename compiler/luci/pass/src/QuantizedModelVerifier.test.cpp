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

#include "QuantizedModelVerifier.h"

#include "luci/Pass/QuantizeWithMinMaxPass.h"

#include "test/TestIOGraph.h"

#include <gtest/gtest.h>

using Type = loco::DataType;
using Granularity = luci::QuantizationGranularity;

namespace
{

void insert_scale_zp(luci::CircleNode *node, float scale, int64_t zp)
{
  auto qparam = node->quantparam();
  assert(qparam != nullptr); // FIX_CALLER_UNLESS
  qparam->scale.push_back(scale);
  qparam->zerop.push_back(zp);
}

void quantize_and_verify(loco::Graph *g, Type quantized_dtype, Granularity granularity)
{
  luci::QuantizeWithMinMaxPass pass(Type::FLOAT32, quantized_dtype, granularity);
  pass.run(g);

  luci::QuantizedModelVerifier verifier(quantized_dtype, granularity);
  verifier.verify(g);
}

// Helper function to reduce duplicate test codes
// Assumption: g->output()->from() is the target node
void quantize_and_verify_with_wrong_type(luci::test::TestIOGraph *g, Type quantized_dtype,
                                         Granularity granularity, Type wrong_dtype)
{
  luci::QuantizeWithMinMaxPass pass(Type::FLOAT32, quantized_dtype, granularity);
  pass.run(g->g());

  auto node = loco::must_cast<luci::CircleNode *>(g->output()->from());
  node->dtype(wrong_dtype);

  luci::QuantizedModelVerifier verifier(quantized_dtype, granularity);
  verifier.verify(g->g());
}

// Helper function to reduce duplicate test codes
// Assumption: g->output()->from() is the target node
void quantize_and_verify_with_wrong_granularity(luci::test::TestIOGraph *g, Type quantized_dtype,
                                                Granularity granularity)
{
  luci::QuantizeWithMinMaxPass pass(Type::FLOAT32, quantized_dtype, granularity);
  pass.run(g->g());

  auto node = loco::must_cast<luci::CircleNode *>(g->output()->from());
  insert_scale_zp(node, 1.0, 1);

  luci::QuantizedModelVerifier verifier(quantized_dtype, granularity);
  verifier.verify(g->g());
}

// Set min/max for all non-const nodes in the graph
void set_minmax_to_non_const(loco::Graph *g, float min, float max)
{
  for (auto node : loco::all_nodes(g))
  {
    auto const_node = dynamic_cast<luci::CircleConst *>(node);
    if (const_node != nullptr)
      continue;

    // Min/Max is not recorded for ArgMax
    // See MinMaxObserver.cpp in record_minmax module
    auto argmax_node = dynamic_cast<luci::CircleArgMax *>(node);
    if (argmax_node != nullptr)
      continue;

    auto circle_node = loco::must_cast<luci::CircleNode *>(node);
    auto qparam = std::make_unique<luci::CircleQuantParam>();
    {
      qparam->min.emplace_back(min);
      qparam->max.emplace_back(max);
    }
    circle_node->quantparam(std::move(qparam));
  }
}

class LogisticTestGraph final : public luci::test::TestIOGraph
{
public:
  void init(void)
  {
    TestIOGraph::init({32}, {32});
    _logistic = g()->nodes()->create<luci::CircleLogistic>();
    {
      _logistic->x(input());
    }
    output()->from(_logistic);

    set_minmax_to_non_const(g(), -1, 1);
  }

public:
  luci::CircleLogistic *_logistic = nullptr;
};

class SoftmaxTestGraph final : public luci::test::TestIOGraph
{
public:
  void init(void)
  {
    TestIOGraph::init({32}, {32});
    _softmax = g()->nodes()->create<luci::CircleSoftmax>();
    {
      _softmax->logits(input());
      _softmax->beta(0.1);
    }
    output()->from(_softmax);

    set_minmax_to_non_const(g(), -1, 1);
  }

public:
  luci::CircleSoftmax *_softmax = nullptr;
};

template <Type indexT> class SliceTestGraph final : public luci::test::TestIOGraph
{
public:
  void init(void)
  {
    TestIOGraph::init({32}, {32});
    _begin = g()->nodes()->create<luci::CircleConst>();
    {
      _begin->dtype(indexT);
    }
    _size = g()->nodes()->create<luci::CircleConst>();
    {
      _size->dtype(indexT);
    }
    _slice = g()->nodes()->create<luci::CircleSlice>();
    {
      _slice->input(input());
      _slice->begin(_begin);
      _slice->size(_size);
    }
    output()->from(_slice);

    set_minmax_to_non_const(g(), -1, 1);
  }

public:
  luci::CircleSlice *_slice = nullptr;
  luci::CircleConst *_begin = nullptr;
  luci::CircleConst *_size = nullptr;
};

class ReshapeTestGraph final : public luci::test::TestIOGraph
{
public:
  void init(void)
  {
    TestIOGraph::init({32}, {32});
    _shape = g()->nodes()->create<luci::CircleConst>();
    {
      _shape->dtype(Type::S32);
    }
    _reshape = g()->nodes()->create<luci::CircleReshape>();
    {
      _reshape->tensor(input());
      _reshape->shape(_shape);
    }
    output()->from(_reshape);

    set_minmax_to_non_const(g(), -1, 1);
  }

public:
  luci::CircleReshape *_reshape = nullptr;
  luci::CircleConst *_shape = nullptr;
};

class TanhTestGraph final : public luci::test::TestIOGraph
{
public:
  void init(void)
  {
    TestIOGraph::init({32}, {32});
    _tanh = g()->nodes()->create<luci::CircleTanh>();
    {
      _tanh->x(input());
    }
    output()->from(_tanh);

    set_minmax_to_non_const(g(), -1, 1);
  }

public:
  luci::CircleTanh *_tanh = nullptr;
};

template <Type indexT> class ArgMaxTestGraph final : public luci::test::TestIOGraph
{
public:
  void init(void)
  {
    TestIOGraph::init({32}, {1});
    // output dtype is float by default, but ArgMax should have indexType (s32/s64)
    output()->dtype(indexT);
    _dimension = g()->nodes()->create<luci::CircleConst>();
    {
      _dimension->dtype(indexT);
    }
    _argmax = g()->nodes()->create<luci::CircleArgMax>();
    {
      _argmax->input(input());
      _argmax->dimension(_dimension);
      _argmax->output_type(indexT);
      _argmax->dtype(indexT);
    }
    output()->from(_argmax);

    set_minmax_to_non_const(g(), -1, 1);
  }

public:
  luci::CircleArgMax *_argmax = nullptr;
  luci::CircleConst *_dimension = nullptr;
};

class TransposeTestGraph final : public luci::test::TestIOGraph
{
public:
  void init(void)
  {
    TestIOGraph::init({32}, {32});
    _perm = g()->nodes()->create<luci::CircleConst>();
    {
      _perm->dtype(Type::S32);
    }
    _transpose = g()->nodes()->create<luci::CircleTranspose>();
    {
      _transpose->a(input());
      _transpose->perm(_perm);
    }
    output()->from(_transpose);

    set_minmax_to_non_const(g(), -1, 1);
  }

public:
  luci::CircleTranspose *_transpose = nullptr;
  luci::CircleConst *_perm = nullptr;
};

} // namespace

// Quantize and verify with given configurations
#define TEST_WITH_GRAPH(graph, type, granularity)                   \
  do                                                                \
  {                                                                 \
    graph g;                                                        \
    g.init();                                                       \
    EXPECT_NO_THROW(quantize_and_verify(g.g(), type, granularity)); \
  } while (0)

// Quantize and verify with wrong type
#define TEST_WITH_WRONG_TYPE(graph, type, granularity, wrong_dtype)                            \
  do                                                                                           \
  {                                                                                            \
    graph g;                                                                                   \
    g.init();                                                                                  \
    EXPECT_ANY_THROW(quantize_and_verify_with_wrong_type(&g, type, granularity, wrong_dtype)); \
  } while (0)

// Quantize and verify with wrong granularity
#define TEST_WITH_WRONG_GRANULARITY(graph, type, granularity)                            \
  do                                                                                     \
  {                                                                                      \
    graph g;                                                                             \
    g.init();                                                                            \
    EXPECT_ANY_THROW(quantize_and_verify_with_wrong_granularity(&g, type, granularity)); \
  } while (0)

TEST(QuantizedModelVerifierTest, Logistic)
{
  TEST_WITH_GRAPH(LogisticTestGraph, Type::U8, Granularity::LayerWise);
  TEST_WITH_GRAPH(LogisticTestGraph, Type::U8, Granularity::ChannelWise);
  TEST_WITH_GRAPH(LogisticTestGraph, Type::S16, Granularity::ChannelWise);
}

TEST(QuantizedModelVerifierTest, Logistic_wrong_type_NEG)
{
  {
    LogisticTestGraph g;
    g.init();

    luci::QuantizeWithMinMaxPass pass(Type::FLOAT32, Type::U8, Granularity::LayerWise);
    pass.run(g.g());

    g._logistic->dtype(Type::S16);

    luci::QuantizedModelVerifier verifier(Type::U8, Granularity::LayerWise);
    EXPECT_ANY_THROW(verifier.verify(g.g()));
  }
}

TEST(QuantizedModelVerifierTest, Logistic_wrong_granularity_NEG)
{
  {
    LogisticTestGraph g;
    g.init();

    luci::QuantizeWithMinMaxPass pass(Type::FLOAT32, Type::U8, Granularity::LayerWise);
    pass.run(g.g());

    insert_scale_zp(g._logistic, 1.0, 1);

    luci::QuantizedModelVerifier verifier(Type::U8, Granularity::LayerWise);
    EXPECT_ANY_THROW(verifier.verify(g.g()));
  }
}

TEST(QuantizedModelVerifierTest, Softmax)
{
  TEST_WITH_GRAPH(SoftmaxTestGraph, Type::U8, Granularity::LayerWise);
  TEST_WITH_GRAPH(SoftmaxTestGraph, Type::U8, Granularity::ChannelWise);
  TEST_WITH_GRAPH(SoftmaxTestGraph, Type::S16, Granularity::ChannelWise);
}

TEST(QuantizedModelVerifierTest, Softmax_wrong_type_U8_LWQ_NEG)
{
  SoftmaxTestGraph g;
  g.init();

  luci::QuantizeWithMinMaxPass pass(Type::FLOAT32, Type::U8, Granularity::LayerWise);
  pass.run(g.g());

  g._softmax->dtype(Type::S16);

  luci::QuantizedModelVerifier verifier(Type::U8, Granularity::LayerWise);
  EXPECT_ANY_THROW(verifier.verify(g.g()));
}

TEST(QuantizedModelVerifierTest, Softmax_wrong_granularity_U8_LWQ_NEG)
{
  SoftmaxTestGraph g;
  g.init();

  luci::QuantizeWithMinMaxPass pass(Type::FLOAT32, Type::U8, Granularity::LayerWise);
  pass.run(g.g());

  insert_scale_zp(g._softmax, 1.0, 1);

  luci::QuantizedModelVerifier verifier(Type::U8, Granularity::LayerWise);
  EXPECT_ANY_THROW(verifier.verify(g.g()));
}

TEST(QuantizedModelVerifierTest, Slice)
{
  TEST_WITH_GRAPH(SliceTestGraph<Type::S32>, Type::U8, Granularity::LayerWise);
  TEST_WITH_GRAPH(SliceTestGraph<Type::S32>, Type::U8, Granularity::ChannelWise);
  TEST_WITH_GRAPH(SliceTestGraph<Type::S32>, Type::S16, Granularity::ChannelWise);

  TEST_WITH_GRAPH(SliceTestGraph<Type::S64>, Type::U8, Granularity::LayerWise);
  TEST_WITH_GRAPH(SliceTestGraph<Type::S64>, Type::U8, Granularity::ChannelWise);
  TEST_WITH_GRAPH(SliceTestGraph<Type::S64>, Type::S16, Granularity::ChannelWise);
}

TEST(QuantizedModelVerifierTest, Slice_wrong_type_NEG)
{
  {
    SliceTestGraph<Type::S32> g;
    g.init();

    luci::QuantizeWithMinMaxPass pass(Type::FLOAT32, Type::U8, Granularity::LayerWise);
    pass.run(g.g());

    g._slice->dtype(Type::S16);

    luci::QuantizedModelVerifier verifier(Type::U8, Granularity::LayerWise);
    EXPECT_ANY_THROW(verifier.verify(g.g()));
  }
}

TEST(QuantizedModelVerifierTest, Slice_wrong_granularity_NEG)
{
  {
    SliceTestGraph<Type::S32> g;
    g.init();

    luci::QuantizeWithMinMaxPass pass(Type::FLOAT32, Type::U8, Granularity::LayerWise);
    pass.run(g.g());

    insert_scale_zp(g._slice, 1.0, 1);

    luci::QuantizedModelVerifier verifier(Type::U8, Granularity::LayerWise);
    EXPECT_ANY_THROW(verifier.verify(g.g()));
  }
}

TEST(QuantizedModelVerifierTest, ArgMax)
{
  TEST_WITH_GRAPH(ArgMaxTestGraph<Type::S32>, Type::U8, Granularity::LayerWise);
  TEST_WITH_GRAPH(ArgMaxTestGraph<Type::S32>, Type::U8, Granularity::ChannelWise);
  TEST_WITH_GRAPH(ArgMaxTestGraph<Type::S32>, Type::S16, Granularity::ChannelWise);

  TEST_WITH_GRAPH(ArgMaxTestGraph<Type::S64>, Type::U8, Granularity::LayerWise);
  TEST_WITH_GRAPH(ArgMaxTestGraph<Type::S64>, Type::U8, Granularity::ChannelWise);
  TEST_WITH_GRAPH(ArgMaxTestGraph<Type::S64>, Type::S16, Granularity::ChannelWise);
}

TEST(QuantizedModelVerifierTest, ArgMax_wrong_type_NEG)
{
  {
    ArgMaxTestGraph<Type::S32> g;
    g.init();

    luci::QuantizeWithMinMaxPass pass(Type::FLOAT32, Type::U8, Granularity::LayerWise);
    pass.run(g.g());

    g._dimension->dtype(Type::U8);

    luci::QuantizedModelVerifier verifier(Type::U8, Granularity::LayerWise);
    EXPECT_ANY_THROW(verifier.verify(g.g()));
  }
}

TEST(QuantizedModelVerifierTest, ArgMax_wrong_granularity_NEG)
{
  {
    ArgMaxTestGraph<Type::S32> g;
    g.init();

    luci::QuantizeWithMinMaxPass pass(Type::FLOAT32, Type::U8, Granularity::LayerWise);
    pass.run(g.g());

    insert_scale_zp(loco::must_cast<luci::CircleNode *>(g._argmax->input()), 1.0, 1);

    luci::QuantizedModelVerifier verifier(Type::U8, Granularity::LayerWise);
    EXPECT_ANY_THROW(verifier.verify(g.g()));
  }
}

TEST(QuantizedModelVerifierTest, Reshape)
{
  TEST_WITH_GRAPH(ReshapeTestGraph, Type::U8, Granularity::LayerWise);
  TEST_WITH_GRAPH(ReshapeTestGraph, Type::U8, Granularity::ChannelWise);
  TEST_WITH_GRAPH(ReshapeTestGraph, Type::S16, Granularity::ChannelWise);
}

TEST(QuantizedModelVerifierTest, Reshape_wrong_type_NEG)
{
  TEST_WITH_WRONG_TYPE(ReshapeTestGraph, Type::U8, Granularity::LayerWise, Type::S16);
  TEST_WITH_WRONG_TYPE(ReshapeTestGraph, Type::U8, Granularity::ChannelWise, Type::S16);
  TEST_WITH_WRONG_TYPE(ReshapeTestGraph, Type::S16, Granularity::ChannelWise, Type::U8);
}

TEST(QuantizedModelVerifierTest, Reshape_wrong_granularity_NEG)
{
  TEST_WITH_WRONG_GRANULARITY(ReshapeTestGraph, Type::U8, Granularity::LayerWise);
  TEST_WITH_WRONG_GRANULARITY(ReshapeTestGraph, Type::U8, Granularity::ChannelWise);
  TEST_WITH_WRONG_GRANULARITY(ReshapeTestGraph, Type::S16, Granularity::ChannelWise);
}

TEST(QuantizedModelVerifierTest, Tanh)
{
  TEST_WITH_GRAPH(TanhTestGraph, Type::U8, Granularity::LayerWise);
  TEST_WITH_GRAPH(TanhTestGraph, Type::U8, Granularity::ChannelWise);
  TEST_WITH_GRAPH(TanhTestGraph, Type::S16, Granularity::ChannelWise);
}

TEST(QuantizedModelVerifierTest, Tanh_wrong_type_U8_CWQ_NEG)
{
  TanhTestGraph g;
  g.init();

  luci::QuantizeWithMinMaxPass pass(Type::FLOAT32, Type::U8, Granularity::ChannelWise);
  pass.run(g.g());

  g._tanh->dtype(Type::S16);

  luci::QuantizedModelVerifier verifier(Type::U8, Granularity::ChannelWise);
  EXPECT_ANY_THROW(verifier.verify(g.g()));
}

TEST(QuantizedModelVerifierTest, Tanh_wrong_granularity_U8_LWQ_NEG)
{
  TanhTestGraph g;
  g.init();

  luci::QuantizeWithMinMaxPass pass(Type::FLOAT32, Type::U8, Granularity::ChannelWise);
  pass.run(g.g());

  insert_scale_zp(g._tanh, 1.0, 1);

  luci::QuantizedModelVerifier verifier(Type::U8, Granularity::ChannelWise);
  EXPECT_ANY_THROW(verifier.verify(g.g()));
}

TEST(QuantizedModelVerifierTest, Transpose)
{
  TEST_WITH_GRAPH(TransposeTestGraph, Type::U8, Granularity::LayerWise);
  TEST_WITH_GRAPH(TransposeTestGraph, Type::U8, Granularity::ChannelWise);
  TEST_WITH_GRAPH(TransposeTestGraph, Type::S16, Granularity::ChannelWise);
}

TEST(QuantizedModelVerifierTest, Transpose_wrong_type_U8_CWQ_NEG)
{
  TransposeTestGraph g;
  g.init();

  luci::QuantizeWithMinMaxPass pass(Type::FLOAT32, Type::U8, Granularity::ChannelWise);
  pass.run(g.g());

  g._transpose->dtype(Type::S16);

  luci::QuantizedModelVerifier verifier(Type::U8, Granularity::ChannelWise);
  EXPECT_ANY_THROW(verifier.verify(g.g()));
}

TEST(QuantizedModelVerifierTest, Transpose_wrong_granularity_U8_CWQ_NEG)
{
  TransposeTestGraph g;
  g.init();

  luci::QuantizeWithMinMaxPass pass(Type::FLOAT32, Type::U8, Granularity::ChannelWise);
  pass.run(g.g());

  insert_scale_zp(g._transpose, 1.0, 1);

  luci::QuantizedModelVerifier verifier(Type::U8, Granularity::ChannelWise);
  EXPECT_ANY_THROW(verifier.verify(g.g()));
}

#undef TEST_WITH_GRAPH
#undef TEST_WITH_WRONG_TYPE
#undef TEST_WITH_WRONG_GRANULARITY
