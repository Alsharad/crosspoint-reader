#pragma once

#include <string>
#include <vector>

#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"
#include "util/ReaderStatusBarConfig.h"

// Reader status bar configuration: a top and a bottom bar with seven placeable slots each.
// Editor layout ported from CrossInk (github.com/uxjulia/CrossInk), design by Julia Nguyen, MIT.
class StatusBarSettingsActivity final : public UiListActivity {
 public:
  explicit StatusBarSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  // Largest row count of any view (a bar's slots plus its three options).
  static constexpr int MAX_ROWS = ReaderStatusBarConfig::SLOT_COUNT + 3;

  void onEnter() override;
  void render(RenderLock&&) override;

 private:
  enum class View : uint8_t { Root, Top, Bottom };

  OptionPopup optionPopup;
  View view = View::Root;
  // Items the slot picker offers, decided in onEnter(): clock and date need an RTC.
  std::vector<ReaderStatusBarItem> pickerItems;

  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  bool handleCustomInput() override;
  void onBackButton() override;

  void openView(View next);
  ReaderStatusBarPosition position() const;
  std::string rowLabel(int index) const;
  std::string rowValue(int index) const;
  void editRow(int index);
  void editSlot(unsigned slot);
  void saveBar(const ReaderStatusBarConfig& config);

  std::string rowLabels_[MAX_ROWS];
  std::string rowValues_[MAX_ROWS];
  freeink::ui::ListItem rowItems_[MAX_ROWS]{};
};
