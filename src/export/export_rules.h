#pragma once
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <optional>
#include <string>

#include "core/rate.h"

namespace br {

enum class ExportCodec { H264, Hevc };
enum class ExportQuality { High, Medium, Small, Custom };

// Bits per second. Presets scale with pixels and frame rate; Custom needs mbps above zero.
int64_t exportBitrate(ExportCodec codec, ExportQuality quality, std::optional<double> mbps,
                      uint32_t w, uint32_t h, bc::Rate rate);

// Replaces characters Windows forbids and control bytes, trims trailing dots and spaces,
// and names an empty result "Export".
std::string sanitizeFileName(std::string name);

// <sanitized name> - YYYY-MM-DD HHmm.mp4, adding " (2)", " (3)" while that or its .part exists.
std::filesystem::path exportPath(const std::filesystem::path& folder, const std::string& name,
                                 const std::tm& local);

ExportCodec parseCodec(const std::string& s);
ExportQuality parseQuality(const std::string& s);

}  // namespace br