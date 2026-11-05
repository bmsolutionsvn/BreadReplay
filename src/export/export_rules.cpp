#include "export/export_rules.h"

#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace br {
namespace {
constexpr double kReferencePixels = 1920.0 * 1080.0;
constexpr double kReferenceFps = 30.0;

double presetMbps(ExportCodec codec, ExportQuality quality) {
  if (codec == ExportCodec::H264) {
    switch (quality) {
      case ExportQuality::High: return 20.0;
      case ExportQuality::Medium: return 10.0;
      case ExportQuality::Small: return 5.0;
      case ExportQuality::Custom: break;
    }
  } else {
    switch (quality) {
      case ExportQuality::High: return 12.0;
      case ExportQuality::Medium: return 6.0;
      case ExportQuality::Small: return 3.0;
      case ExportQuality::Custom: break;
    }
  }
  throw std::invalid_argument("custom quality needs a bitrate");
}

std::string stamp(const std::tm& t) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%04d-%02d-%02d %02d%02d", t.tm_year + 1900, t.tm_mon + 1,
                t.tm_mday, t.tm_hour, t.tm_min);
  return buf;
}

bool taken(const std::filesystem::path& p) {
  std::error_code ec;
  auto part = p;
  part += ".part";
  return std::filesystem::exists(p, ec) || std::filesystem::exists(part, ec);
}

// Names arrive as UTF-8 but path(narrow) goes through the ANSI code page on Windows.
std::filesystem::path utf8Path(const std::string& s) {
  return std::filesystem::path(std::u8string(s.begin(), s.end()));
}
}  // namespace

int64_t exportBitrate(ExportCodec codec, ExportQuality quality, std::optional<double> mbps,
                      uint32_t w, uint32_t h, bc::Rate rate) {
  if (quality == ExportQuality::Custom) {
    if (!mbps || !(*mbps > 0.0)) throw std::invalid_argument("custom bitrate must be above zero");
    return std::llround(*mbps * 1'000'000.0);
  }
  const double scale = static_cast<double>(w) * h * rate.fps() / (kReferencePixels * kReferenceFps);
  return std::llround(presetMbps(codec, quality) * 1'000'000.0 * scale);
}

std::string sanitizeFileName(std::string name) {
  static const std::string bad = "<>:\"/\\|?*";
  for (char& c : name) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (u < 0x20 || u == 0x7f || bad.find(c) != std::string::npos) c = '_';
  }
  while (!name.empty() && (name.back() == '.' || name.back() == ' ')) name.pop_back();
  if (name.empty()) name = "Export";
  return name;
}

std::filesystem::path exportPath(const std::filesystem::path& folder, const std::string& name,
                                 const std::tm& local) {
  const std::string base = sanitizeFileName(name) + " - " + stamp(local);
  std::filesystem::path p = folder / utf8Path(base + ".mp4");
  for (int n = 2; taken(p); ++n) p = folder / utf8Path(base + " (" + std::to_string(n) + ").mp4");
  return p;
}

ExportCodec parseCodec(const std::string& s) {
  if (s == "h264") return ExportCodec::H264;
  if (s == "hevc") return ExportCodec::Hevc;
  throw std::invalid_argument("unknown export codec '" + s + "'");
}

ExportQuality parseQuality(const std::string& s) {
  if (s == "high") return ExportQuality::High;
  if (s == "medium") return ExportQuality::Medium;
  if (s == "small") return ExportQuality::Small;
  if (s == "custom") return ExportQuality::Custom;
  throw std::invalid_argument("unknown export quality '" + s + "'");
}

}  // namespace br