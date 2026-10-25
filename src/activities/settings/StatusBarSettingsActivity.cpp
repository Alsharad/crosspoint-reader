#include "StatusBarSettingsActivity.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <I18n.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
// Root view rows.
enum RootRow { ROOT_TOP = 0, ROOT_BOTTOM, ROOT_XTC, ROOT_COUNT };

// Bar view rows: the seven slots, then the bar's own options.
constexpr int ROW_PERCENTAGE_FORMAT = ReaderStatusBarConfig::SLOT_COUNT;
constexpr int ROW_PROGRESS_BAR = ROW_PERCENTAGE_FORMAT + 1;
constexpr int ROW_PROGRESS_BAR_THICKNESS = ROW_PROGRESS_BAR + 1;
constexpr int BAR_ROW_COUNT = ROW_PROGRESS_BAR_THICKNESS + 1;
static_assert(BAR_ROW_COUNT == StatusBarSettingsActivity::MAX_ROWS, "keep MAX_ROWS in sync with the bar rows");

constexpr int PROGRESS_BAR_ITEMS = 3;
const StrId progressBarNames[PROGRESS_BAR_ITEMS] = {StrId::STR_BOOK, StrId::STR_CHAPTER, StrId::STR_HIDE};

constexpr int PROGRESS_BAR_THICKNESS_ITEMS = 3;
const StrId progressBarThicknessNames[PROGRESS_BAR_THICKNESS_ITEMS] = {
    StrId::STR_PROGRESS_BAR_THIN, StrId::STR_PROGRESS_BAR_MEDIUM, StrId::STR_PROGRESS_BAR_THICK};

constexpr int PERCENTAGE_FORMAT_ITEMS = CrossPointSettings::READER_STATUS_BAR_PERCENT_FORMAT_COUNT;
const StrId percentageFormatNames[PERCENTAGE_FORMAT_ITEMS] = {StrId::STR_PERCENTAGE_FORMAT_WHOLE,
                                                              StrId::STR_PERCENTAGE_FORMAT_ONE_DECIMAL,
                                                              StrId::STR_PERCENTAGE_FORMAT_TWO_DECIMALS};

constexpr int XTC_STATUS_BAR_ITEMS = 3;
const StrId xtcStatusBarNames[XTC_STATUS_BAR_ITEMS] = {StrId::STR_HIDE, StrId::STR_BOTTOM, StrId::STR_TOP};

constexpr int previewLabelGap = 6;

std::string itemName(const ReaderStatusBarItem item) {
  switch (item) {
    case ReaderStatusBarItem::Date:
      return tr(STR_DATE);
    case ReaderStatusBarItem::Clock:
      return tr(STR_STATUS_BAR_CLOCK);
    case ReaderStatusBarItem::Battery:
      return tr(STR_BATTERY);
    case ReaderStatusBarItem::ChapterPageCount:
      return tr(STR_CHAPTER_PAGE_COUNT);
    case ReaderStatusBarItem::BookProgressPercentage:
      return tr(STR_BOOK_PROGRESS_PERCENTAGE);
    case ReaderStatusBarItem::TitleBook:
      return std::string(tr(STR_TITLE)) + " (" + tr(STR_BOOK) + ")";
    case ReaderStatusBarItem::TitleChapter:
      return std::string(tr(STR_TITLE)) + " (" + tr(STR_CHAPTER) + ")";
    default:
      return tr(STR_STATUS_BAR_EMPTY);
  }
}

std::string slotName(const unsigned slot) {
  if (slot == ReaderStatusBarConfig::CENTER) return tr(STR_CENTER);
  if (slot < ReaderStatusBarConfig::CENTER) {
    return std::string(tr(STR_STATUS_BAR_LEFT)) + " " + std::to_string(slot - ReaderStatusBarConfig::LEFT_FIRST + 1);
  }
  return std::string(tr(STR_STATUS_BAR_RIGHT)) + " " + std::to_string(slot - ReaderStatusBarConfig::RIGHT_FIRST + 1);
}
}  // namespace

StatusBarSettingsActivity::StatusBarSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("StatusBarSettings", renderer, mappedInput) {}

void StatusBarSettingsActivity::onEnter() {
  UiListActivity::onEnter();

  if (SETTINGS.xtcStatusBarMode >= XTC_STATUS_BAR_ITEMS) {
    SETTINGS.xtcStatusBarMode = CrossPointSettings::XTC_STATUS_BAR_MODE::XTC_STATUS_BAR_HIDE;
  }

  const bool clockAvailable = halClock.isAvailable();
  pickerItems.clear();
  pickerItems.reserve(8);
  pickerItems.push_back(ReaderStatusBarItem::Empty);
  if (clockAvailable) {
    pickerItems.push_back(ReaderStatusBarItem::Clock);
    pickerItems.push_back(ReaderStatusBarItem::Date);
  }
  pickerItems.push_back(ReaderStatusBarItem::Battery);
  pickerItems.push_back(ReaderStatusBarItem::ChapterPageCount);
  pickerItems.push_back(ReaderStatusBarItem::BookProgressPercentage);
  pickerItems.push_back(ReaderStatusBarItem::TitleBook);
  pickerItems.push_back(ReaderStatusBarItem::TitleChapter);

  openView(View::Root);
}

int StatusBarSettingsActivity::listCount() const { return view == View::Root ? ROOT_COUNT : BAR_ROW_COUNT; }

ReaderStatusBarPosition StatusBarSettingsActivity::position() const {
  return view == View::Top ? ReaderStatusBarPosition::Top : ReaderStatusBarPosition::Bottom;
}

void StatusBarSettingsActivity::openView(const View next) {
  view = next;
  // Labels only change with the view, so they are set here rather than every buildScreen().
  for (int i = 0; i < listCount(); i++) {
    rowLabels_[i] = rowLabel(i);
    rowItems_[i] = fui::ListItem{};
    rowItems_[i].label = rowLabels_[i].c_str();
    rowItems_[i].actionValue = static_cast<int16_t>(i);
  }
  nav.selected = 0;
  requestUpdate();
}

std::string StatusBarSettingsActivity::rowLabel(const int index) const {
  if (view == View::Root) {
    switch (index) {
      case ROOT_TOP:
        return tr(STR_TOP_STATUS_BAR);
      case ROOT_BOTTOM:
        return tr(STR_BOTTOM_STATUS_BAR);
      default:
        return tr(STR_XTC_STATUS_BAR);
    }
  }
  if (index < static_cast<int>(ReaderStatusBarConfig::SLOT_COUNT)) return slotName(index);
  if (index == ROW_PERCENTAGE_FORMAT) return tr(STR_PERCENTAGE_FORMAT);
  if (index == ROW_PROGRESS_BAR) return tr(STR_PROGRESS_BAR);
  return tr(STR_PROGRESS_BAR_THICKNESS);
}

std::string StatusBarSettingsActivity::rowValue(const int index) const {
  if (view == View::Root) {
    return index == ROOT_XTC ? I18N.get(xtcStatusBarNames[SETTINGS.xtcStatusBarMode]) : "";
  }
  const auto config = SETTINGS.readerStatusBar(position());
  if (index < static_cast<int>(ReaderStatusBarConfig::SLOT_COUNT)) return itemName(config.slots[index]);
  if (index == ROW_PERCENTAGE_FORMAT) {
    return I18N.get(percentageFormatNames[std::min<int>(config.percentageFormat, PERCENTAGE_FORMAT_ITEMS - 1)]);
  }
  if (index == ROW_PROGRESS_BAR) {
    return I18N.get(progressBarNames[std::min<int>(config.progressBar, PROGRESS_BAR_ITEMS - 1)]);
  }
  return I18N.get(
      progressBarThicknessNames[std::min<int>(config.progressBarThickness, PROGRESS_BAR_THICKNESS_ITEMS - 1)]);
}

bool StatusBarSettingsActivity::handleCustomInput() {
  return optionPopup.handleInput(mappedInput, [this] { requestUpdate(); });
}

void StatusBarSettingsActivity::onBackButton() {
  if (view != View::Root) {
    openView(View::Root);
    return;
  }
  finish();
}

void StatusBarSettingsActivity::activateIndex(const int index) {
  if (optionPopup.isActive()) return;
  nav.selected = index;
  // Activation opens a popup or another view; a lingering flash would gray an unrelated row.
  app.clearTapFlash();
  editRow(index);
  requestUpdate();
}

void StatusBarSettingsActivity::saveBar(const ReaderStatusBarConfig& config) {
  SETTINGS.setReaderStatusBar(position(), config);
  SETTINGS.saveToFile();
}

void StatusBarSettingsActivity::editRow(const int index) {
  if (view == View::Root) {
    if (index == ROOT_TOP) {
      openView(View::Top);
    } else if (index == ROOT_BOTTOM) {
      openView(View::Bottom);
    } else {
      optionPopup.show(StrId::STR_XTC_STATUS_BAR, xtcStatusBarNames, XTC_STATUS_BAR_ITEMS, SETTINGS.xtcStatusBarMode,
                       [](int idx) {
                         SETTINGS.xtcStatusBarMode = idx;
                         SETTINGS.saveToFile();
                       });
    }
    return;
  }

  if (index < static_cast<int>(ReaderStatusBarConfig::SLOT_COUNT)) {
    editSlot(static_cast<unsigned>(index));
    return;
  }
  auto config = SETTINGS.readerStatusBar(position());
  if (index == ROW_PERCENTAGE_FORMAT) {
    optionPopup.show(StrId::STR_PERCENTAGE_FORMAT, percentageFormatNames, PERCENTAGE_FORMAT_ITEMS,
                     config.percentageFormat, [this, config](int idx) mutable {
                       config.percentageFormat = idx;
                       saveBar(config);
                     });
  } else if (index == ROW_PROGRESS_BAR) {
    optionPopup.show(StrId::STR_PROGRESS_BAR, progressBarNames, PROGRESS_BAR_ITEMS, config.progressBar,
                     [this, config](int idx) mutable {
                       config.progressBar = idx;
                       saveBar(config);
                     });
  } else {
    optionPopup.show(StrId::STR_PROGRESS_BAR_THICKNESS, progressBarThicknessNames, PROGRESS_BAR_THICKNESS_ITEMS,
                     config.progressBarThickness, [this, config](int idx) mutable {
                       config.progressBarThickness = idx;
                       saveBar(config);
                     });
  }
}

void StatusBarSettingsActivity::editSlot(const unsigned slot) {
  auto config = SETTINGS.readerStatusBar(position());
  std::vector<std::string> names;
  names.reserve(pickerItems.size());
  int current = 0;
  for (size_t i = 0; i < pickerItems.size(); i++) {
    names.push_back(itemName(pickerItems[i]));
    if (pickerItems[i] == config.slots[slot]) current = static_cast<int>(i);
  }
  optionPopup.show(StrId::STR_STATUS_BAR, names, current, [this, config, slot](int idx) mutable {
    config.slots[slot] = pickerItems[idx];
    saveBar(config);
  });
}

void StatusBarSettingsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // A bar view keeps a footer band for the live preview (label + bar) above the button hints.
  const int previewFooter = view == View::Root
                                ? 0
                                : UITheme::getReaderStatusBarHeight(position()) +
                                      renderer.getLineHeight(UI_10_FONT_ID) + previewLabelGap + metrics.verticalSpacing;
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight + previewFooter), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  // Only the value text tracks live settings; assign into the existing strings (no array growth).
  for (int i = 0; i < listCount(); i++) {
    rowValues_[i] = rowValue(i);
    rowItems_[i].value = rowValues_[i].empty() ? nullptr : rowValues_[i].c_str();
  }

  fui::ListProps props;
  props.items = rowItems_;
  props.count = static_cast<uint16_t>(listCount());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}

void StatusBarSettingsActivity::render(RenderLock&&) {
  if (optionPopup.processRender(renderer, mappedInput)) return;

  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const char* title = view == View::Root  ? tr(STR_CUSTOMISE_STATUS_BAR)
                      : view == View::Top ? tr(STR_TOP_STATUS_BAR)
                                          : tr(STR_BOTTOM_STATUS_BAR);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, title);

  renderUi();

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  if (view != View::Root) {
    // Preview the bar being edited, pinned directly above the button hints.
    const int barHeight = UITheme::getReaderStatusBarHeight(position());
    const int barY = renderer.getScreenHeight() - metrics.buttonHintsHeight - barHeight - metrics.verticalSpacing;
    renderer.drawCenteredText(UI_10_FONT_ID, barY - previewLabelGap - renderer.getLineHeight(UI_10_FONT_ID),
                              tr(STR_PREVIEW));
    ReaderStatusBarContent content;
    content.bookProgress = 75;
    content.chapterPage = 8;
    content.chapterPageCount = 32;
    content.bookTitle = tr(STR_EXAMPLE_BOOK);
    content.chapterTitle = tr(STR_EXAMPLE_CHAPTER);
    content.previewOriginY = barY;
    GUI.drawReaderStatusBar(renderer, position(), content);
  }

  renderer.displayBuffer();
}
