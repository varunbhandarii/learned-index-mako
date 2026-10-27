#include "alex_index.h"

#include <gtest/gtest.h>

#include <cstdlib>

namespace {

// alex_enabled_for() reads the environment on its first call, so this binary sets it first
// and checks everything in one test.
TEST(AlexConfig, SelectsListedLocalTables) {
  setenv("MAKO_INDEX", "alex", 1);
  setenv("MAKO_ALEX_TABLES", "item,stock", 1);

  EXPECT_TRUE(lim::alex_enabled_for("item"));
  EXPECT_TRUE(lim::alex_enabled_for("stock_0"));
  EXPECT_TRUE(lim::alex_enabled_for("stock_12"));
  EXPECT_FALSE(lim::alex_enabled_for("stock_remote_3"));  // proxies for other shards
  EXPECT_FALSE(lim::alex_enabled_for("stock_data_0"));
  EXPECT_FALSE(lim::alex_enabled_for("customer_0"));
  EXPECT_FALSE(lim::alex_enabled_for("17"));  // Mako names tables by id before opening them
  EXPECT_FALSE(lim::alex_enabled_for("stock_"));
  EXPECT_FALSE(lim::alex_enabled_for(""));
}

}  // namespace
