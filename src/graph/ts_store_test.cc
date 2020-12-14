// Copyright 2020 the deepx authors.
// Author: Yafei Zhang (kimmyzhang@tencent.com)
//

#include <deepx_core/graph/graph.h>
#include <deepx_core/graph/graph_node.h>
#include <deepx_core/graph/tensor_map.h>
#include <deepx_core/graph/ts_store.h>
#include <deepx_core/tensor/data_type.h>
#include <gtest/gtest.h>

namespace deepx_core {

class TSStoreTest : public testing::Test, public DataType {
 protected:
  void TestExpire(ts_t now, ts_t expire, const id_set_t& expected_expired) {
    Graph graph;
    auto* X = new InstanceNode("X", Shape(1, 0), TENSOR_TYPE_CSR);
    auto* W = new VariableNode("W", Shape(0, 2), TENSOR_TYPE_SRM);
    auto* Z = EmbeddingLookup("Z", X, W);
    ASSERT_TRUE(graph.Compile({Z}, 1));

    TSStore ts_store;

    {
      ts_store.set_now(0);
      ts_store.set_expire_threshold(expire);
      ts_store.Init(&graph);

      TensorMap grad;
      grad.insert<srm_t>("W") = srm_t{{1, 2}, {{1, 1}, {2, 2}}};
      ts_store.Update(&grad);
    }

    {
      ts_store.set_now(now);
      ts_store.set_expire_threshold(expire);
      ts_store.Init(&graph);

      TensorMap grad;
      grad.insert<srm_t>("W") = srm_t{{3, 4}, {{3, 3}, {4, 4}}};
      ts_store.Update(&grad);

      // 'ts_store.id_ts_map_' is:
      // 1, 0
      // 2, 0
      // 3, now
      // 4, now
    }

    id_set_t expired = ts_store.Expire();
    EXPECT_EQ(expired, expected_expired);
  }
};

TEST_F(TSStoreTest, Expire_now0_expire0) { TestExpire(0, 0, id_set_t({})); }

TEST_F(TSStoreTest, Expire_now0_expire2) { TestExpire(0, 2, id_set_t({})); }

TEST_F(TSStoreTest, Expire_now2_expire0) { TestExpire(2, 0, id_set_t({})); }

TEST_F(TSStoreTest, Expire_now5_expire2) { TestExpire(5, 2, id_set_t({1, 2})); }

TEST_F(TSStoreTest, Expire_now5_expire8) { TestExpire(5, 8, id_set_t({})); }

}  // namespace deepx_core