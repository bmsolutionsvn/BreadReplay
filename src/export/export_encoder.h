#pragma once
#include <cstdint>
#include <vector>

#include "core/rate.h"
#include "export/export_rules.h"
#include "io/h264_encoder.h"

namespace br {

// GOP is two seconds of frames and the header is global, so the MP4 can be muxed.
bc::OpenedEncoder openExportEncoder(ExportCodec codec, uint32_t w, uint32_t h, bc::Rate rate,
                                    int64_t bitRate);

// Probed once by opening and freeing a 640 x 360 encoder of each codec.
const std::vector<ExportCodec>& availableExportCodecs();

}  // namespace br