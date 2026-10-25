#include "BookFontSystem.h"

#if CROSSPOINT_VECTOR_FONTS

#include <Arduino.h>
#include <Epub.h>
#include <FontPsram.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <Print.h>
#include <TtfEpdFont.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cstring>

#include "CrossPointSettings.h"
#include "SdCardFontSystem.h"

BookFontSystem bookFontSystem;

namespace {

// Fonts past this are skipped: CJK faces belong on the SD font path, which streams.
constexpr size_t MAX_FONT_BYTES = 4 * 1024 * 1024;
// PSRAM left free after a font file is read in.
constexpr size_t PSRAM_HEADROOM = 512 * 1024;

// Print sink that fills a preallocated buffer, failing once it is full.
class BufferPrint final : public Print {
 public:
  BufferPrint(uint8_t* data, const size_t capacity) : data_(data), capacity_(capacity) {}
  size_t write(const uint8_t c) override { return write(&c, 1); }
  size_t write(const uint8_t* buffer, const size_t size) override {
    if (size > capacity_ - used_) return 0;
    memcpy(data_ + used_, buffer, size);
    used_ += size;
    return size;
  }
  size_t used() const { return used_; }

 private:
  uint8_t* data_;
  size_t capacity_;
  size_t used_ = 0;
};

}  // namespace

BookFontSystem::BookFontSystem() = default;
BookFontSystem::~BookFontSystem() = default;

void BookFontSystem::open(const std::shared_ptr<Epub>& epub, GfxRenderer& renderer) {
  if (epub_ == epub) {
    refreshGenericFamilies();
    return;
  }
  close();
  epub_ = epub;
  renderer_ = &renderer;
  loaded_.reserve(MAX_LOADED);

  std::vector<CssFontFace> faces;
  CssParser::loadFontFaces(epub->getCachePath(), faces);  // none: the book embeds no fonts
  families_.reserve(faces.size() + CrossPointSettings::GENERIC_FONT_COUNT);
  for (CssFontFace& face : faces) {
    auto family = std::find_if(families_.begin(), families_.end(),
                               [&face](const Family& candidate) { return candidate.hash == face.family; });
    if (family == families_.end()) {
      families_.emplace_back();
      family = families_.end() - 1;
      family->hash = face.family;
    }
    const uint8_t role = (face.bold ? TtfEpdFont::Bold : 0) | (face.italic ? TtfEpdFont::Italic : 0);
    if (family->hrefs[role].empty()) {
      family->hrefs[role] = FsHelpers::normalisePath(FsHelpers::decodeUriEscapes(face.href));
    }
  }
  // TtfEpdFont needs a regular source; a family declared only in other styles uses its first.
  for (Family& family : families_) {
    for (auto& href : family.hrefs) {
      if (family.hrefs[TtfEpdFont::Regular].empty()) std::swap(family.hrefs[TtfEpdFont::Regular], href);
    }
  }
  LOG_DBG("BKF", "%u embedded font families", static_cast<unsigned>(families_.size()));
  refreshGenericFamilies();
}

void BookFontSystem::dropFamily(const size_t index) {
  for (auto it = loaded_.begin(); it != loaded_.end();) {
    bool ofFamily = false;
    for (int pointSize = 6; pointSize <= 72 && !ofFamily; ++pointSize) {
      ofFamily = it->fontId == fontIdFor(families_[index].hash, static_cast<uint8_t>(pointSize));
    }
    if (ofFamily) {
      renderer_->unregisterTtfFont(it->fontId);
      renderer_->removeFont(it->fontId);
      it = loaded_.erase(it);
    } else {
      ++it;
    }
  }
  families_.erase(families_.begin() + static_cast<std::ptrdiff_t>(index));
}

void BookFontSystem::refreshGenericFamilies() {
  const SdCardFontRegistry& registry = sdFontSystem.registry();
  for (int i = 0; i < CrossPointSettings::GENERIC_FONT_COUNT; ++i) {
    std::string name = SETTINGS.genericFontNames[i];
    // The reader font is already loaded; mapping a family to it changes nothing.
    if (name == SETTINGS.sdFontFamilyName) name.clear();
    if (name == genericNames_[i]) continue;
    genericNames_[i] = name;
    const uint32_t hash = genericFamilyId(static_cast<CssGenericFamily>(i + 1));
    for (size_t f = 0; f < families_.size(); ++f) {
      if (families_[f].hash == hash) {
        dropFamily(f);
        break;
      }
    }
    const SdCardFontFamilyInfo* info = name.empty() ? nullptr : registry.findFamily(name);
    if (!info || !info->vector) continue;
    Family family;
    family.hash = hash;
    family.fromSd = true;
    char path[SdCardFontFamilyInfo::PATH_BUFFER_SIZE];
    for (const auto& file : info->files) {
      if (file.style < 4 && family.hrefs[file.style].empty() && info->filePath(file, path, sizeof(path))) {
        family.hrefs[file.style] = path;
      }
    }
    for (auto& href : family.hrefs) {
      if (family.hrefs[TtfEpdFont::Regular].empty()) std::swap(family.hrefs[TtfEpdFont::Regular], href);
    }
    if (!family.hrefs[TtfEpdFont::Regular].empty()) families_.push_back(std::move(family));
  }
}

void BookFontSystem::close() {
  for (auto& name : genericNames_) name.clear();
  for (const auto& loaded : loaded_) {
    renderer_->unregisterTtfFont(loaded.fontId);
    renderer_->removeFont(loaded.fontId);
  }
  loaded_.clear();
  families_.clear();
  epub_.reset();
}

int BookFontSystem::fontIdFor(const uint32_t family, const uint8_t pointSize) {
  uint32_t hash = 2166136261u;
  for (int shift = 0; shift < 32; shift += 8) {
    hash ^= static_cast<uint8_t>(family >> shift);
    hash *= 16777619u;
  }
  hash ^= pointSize;
  hash *= 16777619u;
  hash ^= 0x424B4600u;  // "BKF\0" salt keeps book fonts apart from reader and variant ids
  const int id = static_cast<int>(hash);
  return id != 0 ? id : 1;
}

int BookFontSystem::resolve(const uint32_t family, const float scale) {
  auto it = std::find_if(families_.begin(), families_.end(),
                         [family](const Family& candidate) { return candidate.hash == family; });
  if (it == families_.end()) return 0;
  const auto pointSize =
      static_cast<uint8_t>(std::clamp(static_cast<int>(SETTINGS.fontPointSize * scale + 0.5f), 6, 72));
  const int id = fontIdFor(family, pointSize);
  return loadFont(*it, pointSize, id) ? id : 0;
}

bool BookFontSystem::load(const int fontId) {
  for (Family& family : families_) {
    for (int pointSize = 6; pointSize <= 72; ++pointSize) {
      if (fontIdFor(family.hash, static_cast<uint8_t>(pointSize)) == fontId) {
        return loadFont(family, static_cast<uint8_t>(pointSize), fontId);
      }
    }
  }
  return false;
}

bool BookFontSystem::readFamilyBytes(Family& family) {
  if (family.bytesLoaded) return !family.bytes[TtfEpdFont::Regular].empty();
  family.bytesLoaded = true;
  for (int role = 0; role < 4; ++role) {
    const std::string& href = family.hrefs[role];
    if (href.empty()) continue;
    size_t size = 0;
    HalFile sdFile;
    if (family.fromSd) {
      if (Storage.openFileForRead("BKF", href, sdFile)) size = sdFile.size();
    } else if (!epub_->getItemSize(href, &size)) {
      size = 0;
    }
    if (size == 0 || size > MAX_FONT_BYTES) {
      LOG_ERR("BKF", "Skipping font %s (%u bytes)", href.c_str(), static_cast<unsigned>(size));
      continue;
    }
    // PsramAlloc aborts on OOM, so only allocate what PSRAM can hold with room to spare.
    if (heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) < size + PSRAM_HEADROOM) {
      LOG_ERR("BKF", "No PSRAM for font %s (%u bytes)", href.c_str(), static_cast<unsigned>(size));
      continue;
    }
    auto& bytes = family.bytes[role];
    bytes.resize(size);
    bool read = false;
    if (family.fromSd) {
      size_t done = 0;
      while (done < size) {
        const int n = sdFile.read(bytes.data() + done, std::min<size_t>(size - done, 4096));
        if (n <= 0) break;
        done += static_cast<size_t>(n);
      }
      read = done == size;
    } else {
      BufferPrint sink(bytes.data(), size);
      read = epub_->readItemContentsToStream(href, sink, 4096) && sink.used() == size;
    }
    if (!read) {
      LOG_ERR("BKF", "Failed to read font %s", href.c_str());
      freeink::font::PsramVector<uint8_t>().swap(bytes);
    }
  }
  return !family.bytes[TtfEpdFont::Regular].empty();
}

bool BookFontSystem::loadFont(Family& family, const uint8_t pointSize, const int fontId) {
  for (auto& loaded : loaded_) {
    if (loaded.fontId == fontId) {
      loaded.lastUse = ++clock_;
      return true;
    }
  }
  if (family.failed || !readFamilyBytes(family)) return false;

  auto font = makeUniqueNoThrow<TtfEpdFont>();
  if (!font) {
    LOG_ERR("BKF", "OOM: TtfEpdFont @%upt", pointSize);
    return false;
  }
  for (int role = 0; role < 4; ++role) {
    const auto& bytes = family.bytes[role];
    if (!bytes.empty()) font->addResidentSource(static_cast<uint8_t>(role), bytes.data(), bytes.size());
  }
  if (!font->load(pointSize, /*twoBit=*/true, /*glyphCacheBytes=*/64 * 1024, /*maxGlyphs=*/512)) {
    // Bad bytes stay bad: give the family up for this book instead of retrying every paragraph.
    LOG_ERR("BKF", "Failed to load embedded font %08x; using the reader font", static_cast<unsigned>(family.hash));
    family.failed = true;
    font.reset();  // it points into the family's bytes
    for (auto& bytes : family.bytes) freeink::font::PsramVector<uint8_t>().swap(bytes);
    return false;
  }
  // Evict only once the new font is usable, so a failed load never costs a working one.
  if (loaded_.size() >= MAX_LOADED) {
    const auto oldest = std::min_element(loaded_.begin(), loaded_.end(),
                                         [](const Loaded& a, const Loaded& b) { return a.lastUse < b.lastUse; });
    renderer_->unregisterTtfFont(oldest->fontId);
    renderer_->removeFont(oldest->fontId);
    loaded_.erase(oldest);
  }
  font->build(" ");
  renderer_->insertFont(fontId, font->family());
  renderer_->registerTtfFont(fontId, font.get());
  loaded_.push_back({fontId, ++clock_, std::move(font)});
  LOG_DBG("BKF", "Loaded embedded font %08x @%upt (heap free %u)", static_cast<unsigned>(family.hash), pointSize,
          static_cast<unsigned>(ESP.getFreeHeap()));
  return true;
}

#endif  // CROSSPOINT_VECTOR_FONTS
