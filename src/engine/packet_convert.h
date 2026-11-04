#pragma once
#include <cstdint>
#include <span>
#include <vector>

#include "store/segment.h"

extern "C" {
#include <libavutil/rational.h>
}

struct AVCodecParameters;

namespace br {
// A packet's own pts in microseconds, or the arrival time when the source has none.
int64_t packetSourcePtsUs(int64_t pts, AVRational timeBase, int64_t arrivalUs);
// A demuxed H.264/HEVC packet as the store takes it: Annex-B, and with parameter sets on a
// keyframe that did not carry them in band.
std::vector<uint8_t> packetToAnnexB(std::vector<uint8_t> au, StoredCodec codec);
std::vector<uint8_t> extradataAnnexB(const AVCodecParameters& p, StoredCodec codec);
bool hasParameterSets(std::span<const uint8_t> au, StoredCodec codec);
void prependParameterSets(std::vector<uint8_t>& au, const AVCodecParameters& p, StoredCodec codec);
}  // namespace br