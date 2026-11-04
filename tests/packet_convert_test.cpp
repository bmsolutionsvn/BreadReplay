#include <doctest/doctest.h>

extern "C" {
#include <libavcodec/avcodec.h>
}

#include <vector>

#include "engine/packet_convert.h"

namespace {
std::vector<uint8_t> avcC() {
  // version, profile, compat, level, lengthSizeMinusOne, one SPS, one PPS.
  return {1, 0x64, 0x00, 0x1F, 0xFF, 0xE1, 0x00, 0x04, 0x67, 0x42, 0x00, 0x1F,
          0x01, 0x00, 0x04, 0x68, 0xCE, 0x38, 0x80};
}

std::vector<uint8_t> hvcC() {
  std::vector<uint8_t> out(23, 0);
  out[0] = 1;
  out[22] = 3;  // three arrays: VPS, SPS, PPS
  for (uint8_t type : {uint8_t{32}, uint8_t{33}, uint8_t{34}}) {
    out.push_back(type);
    out.push_back(0);
    out.push_back(1);
    out.push_back(0);
    out.push_back(1);
    out.push_back(static_cast<uint8_t>(type << 1));  // the NAL header for that type
  }
  return out;
}

AVCodecParameters paramsFor(const std::vector<uint8_t>& extradata) {
  AVCodecParameters p{};
  p.extradata = const_cast<uint8_t*>(extradata.data());
  p.extradata_size = static_cast<int>(extradata.size());
  return p;
}
}  // namespace

TEST_CASE("PacketConvert: an AVCC H.264 packet becomes Annex-B") {
  const std::vector<uint8_t> avcc = {0, 0, 0, 3, 0x65, 0x88, 0x84};
  const auto annex = br::packetToAnnexB(avcc, br::StoredCodec::H264);
  CHECK(annex == std::vector<uint8_t>{0, 0, 0, 1, 0x65, 0x88, 0x84});
}

TEST_CASE("PacketConvert: a keyframe without parameter sets gets them from avcC") {
  const auto ex = avcC();
  auto params = paramsFor(ex);
  std::vector<uint8_t> au = {0, 0, 0, 1, 0x65, 0x88, 0x84};
  REQUIRE_FALSE(br::hasParameterSets(au, br::StoredCodec::H264));
  br::prependParameterSets(au, params, br::StoredCodec::H264);
  CHECK(br::hasParameterSets(au, br::StoredCodec::H264));
  CHECK(au.size() == 7 + 8 + 8);  // IDR, then SPS and PPS with 4-byte start codes
  CHECK(au[3] == 1);
  CHECK((au[4] & 0x1F) == 7);  // SPS first
}

TEST_CASE("PacketConvert: a keyframe that already has parameter sets is untouched") {
  auto params = paramsFor(avcC());
  std::vector<uint8_t> au = {0, 0, 0, 1, 0x67, 0x42, 0x00, 0x1F, 0x00, 0, 0, 1, 0x68, 0xCE, 0x38, 0x80,
                             0, 0, 0, 1, 0x65, 0x88};
  const auto before = au;
  br::prependParameterSets(au, params, br::StoredCodec::H264);
  CHECK(au == before);
}

TEST_CASE("PacketConvert: an HEVC keyframe gets VPS/SPS/PPS from hvcC") {
  const auto ex = hvcC();
  auto params = paramsFor(ex);
  std::vector<uint8_t> au = {0, 0, 0, 1, 0x26, 0x01, 0x02};  // an IDR (type 19)
  REQUIRE_FALSE(br::hasParameterSets(au, br::StoredCodec::Hevc));
  br::prependParameterSets(au, params, br::StoredCodec::Hevc);
  CHECK(br::hasParameterSets(au, br::StoredCodec::Hevc));
}