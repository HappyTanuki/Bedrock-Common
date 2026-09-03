#include <gtest/gtest.h>

#include <cstddef>

TEST(Platform, PointerSizeIsSupported) {
  EXPECT_TRUE(sizeof(void*) == 4U || sizeof(void*) == 8U);
}
