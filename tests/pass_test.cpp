#include <doctest/doctest.h>

#include "events/pass.h"

namespace {
constexpr int64_t kS = 1'000'000;
}

TEST_CASE("Pass: splice keeps base outside the played stretch") {
  std::vector<br::AngleCut> base{{0, "cam1"}, {4 * kS, "cam3"}, {8 * kS, "cam4"}};
  const auto out = br::splicePass(base, {{3 * kS, "cam2"}}, 3 * kS, 6 * kS);
  REQUIRE(out.size() == 3);
  CHECK(out[1].inputId == "cam2");
  CHECK(out[2].tUs == 8 * kS);
}

TEST_CASE("Pass: finish restores the base value where the pass stopped") {
  std::vector<br::AngleCut> base{{0, "cam1"}, {4 * kS, "cam3"}, {8 * kS, "cam4"}};
  const auto out = br::finishPass(base, {{3 * kS, "cam2"}}, 3 * kS, 6 * kS, 10 * kS);
  REQUIRE(out.size() == 4);
  CHECK(out[2].tUs == 6 * kS);
  CHECK(out[2].inputId == "cam3");
  CHECK(out[3].inputId == "cam4");
}