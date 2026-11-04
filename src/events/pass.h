#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

#include "events/event_store.h"

namespace br {

inline bool sameValue(const AngleCut& a, const AngleCut& b) { return a.inputId == b.inputId; }
inline bool sameValue(const SpeedPoint& a, const SpeedPoint& b) { return a.speed == b.speed; }
inline bool sameValue(const ZoomPoint& a, const ZoomPoint& b) {
  return a.rect.cx == b.rect.cx && a.rect.cy == b.rect.cy && a.rect.scale == b.rect.scale;
}

// Base points outside the stretch the pass played over, the pass in the middle.
template <typename P>
std::vector<P> splicePass(const std::vector<P>& base, const std::vector<P>& pass, int64_t fromUs,
                          int64_t toUs) {
  std::vector<P> out;
  for (const auto& p : base) {
    if (p.tUs < fromUs) out.push_back(p);
  }
  out.insert(out.end(), pass.begin(), pass.end());
  for (const auto& p : base) {
    if (p.tUs > toUs) out.push_back(p);
  }
  std::sort(out.begin(), out.end(), [](const P& a, const P& b) { return a.tUs < b.tUs; });
  return out;
}

// Splice, then restore base's value where the pass stopped so everything after plays as before.
template <typename P>
std::vector<P> finishPass(const std::vector<P>& base, const std::vector<P>& pass, int64_t fromUs,
                          int64_t toUs, int64_t outUs) {
  auto out = splicePass(base, pass, fromUs, toUs);
  if (pass.empty() || toUs >= outUs || out.empty()) return out;
  const P* at = nullptr;
  for (const auto& b : base) {
    if (b.tUs <= toUs) at = &b;
  }
  if (at && !sameValue(out.back(), *at)) {
    P p = *at;
    p.tUs = toUs;
    out.push_back(p);
    std::sort(out.begin(), out.end(), [](const P& a, const P& b) { return a.tUs < b.tUs; });
  }
  return out;
}

}  // namespace br