#include "core/build_info.hpp"

#include <gtest/gtest.h>

#include <cstdint>

TEST(BuildInfo, ReportsMilestoneVersion) { EXPECT_STREQ(edge::version(), "v0.1-oculus"); }

TEST(Build, Targets64Bit) { EXPECT_EQ(sizeof(void*), 8u); }
