#pragma once

#include <Epub.h>
#include <Epub/FootnoteEntry.h>
#include <Epub/PageLink.h>
#include <Epub/Section.h>

#include <atomic>
#include <memory>
#include <optional>
#include <vector>

#include "BookmarkEntry.h"
#include "ChapterPosition.h"
#include "EpubReaderMenuActivity.h"
#include "ProgressMapper.h"
#include "ReaderActivity.h"
#include "ReaderToolbarUi.h"
#include "components/OptionPopup.h"

class EpubReaderActivity final : public ReaderActivity {
  std::shared_ptr<Epub> epub;
  std::unique_ptr<Section> section = nullptr;
  int currentSpineIndex = 0;
  int nextPageNumber = 0;
  std::optional<uint16_t> pendingPageJump;
  std::string pendingAnchor;
  int cachedSpineIndex = 0;
  int cachedChapterTotalPageCount = 0;
  std::optional<uint32_t> cachedVisibleTextOffset;
  std::optional<uint32_t> currentPageVisibleOffset;
  std::optional<uint32_t> pendingOffsetJump;
  unsigned long lastPageTurnTime = 0UL;
  unsigned long pageTurnDuration = 0UL;
  int8_t pendingManualTurn = 0;
  bool pendingPercentJump = false;
  float pendingSpineProgress = 0.0f;
  bool pendingScreenshot = false;
  bool pendingSyncSaveError = false;
  uint8_t pageLoadRetryCount = 0;
  static constexpr uint8_t MAX_PAGE_LOAD_RETRIES = 3;
  bool skipNextButtonCheck = false;
  bool automaticPageTurnActive = false;
  // Auto turn stays selected but its timer is held (toggled by a short power press).
  bool automaticPageTurnPaused = false;
  uint8_t activePageTurnOption = 0;  // auto page-turn option index currently running (0 = off)

  // Smart auto page turn: reading speed is learned from manual turns.
  static constexpr unsigned long MIN_ADAPT_ELAPSED_MS = 2000UL;   // faster forward turns count as accidental taps
  static constexpr unsigned long MIN_SMART_DURATION_MS = 2000UL;  // floor for near-empty pages
  static constexpr unsigned long BACK_SLOWDOWN_WINDOW_MS = 3000UL;
  static constexpr uint16_t WPM_ADAPT_MIN = 30;
  static constexpr uint16_t DEFAULT_WPM = 200;
  static constexpr uint16_t FALLBACK_PAGE_WORDS = 250;  // used for image-only pages
  // Pages the reader has stepped back from the furthest auto-turned page; the timer waits while > 0.
  uint8_t skipForwardAdaptCount = 0;
  // Only the first back press after an auto turn may slow the learned speed down.
  bool backwardSlowdownApplied = false;
  // The last forward turn was too fast to be real; the next back press undoes it without penalty.
  bool lastForwardWasAccidental = false;
  uint16_t currentPageWordCount = 0;
  // Running average of words per text page, used to estimate reading time left.
  uint16_t averagePageWords = 0;
  // Auto turns since the last input; auto turn pauses at the limit so an unattended reader stops.
  static constexpr uint8_t AUTO_TURN_UNATTENDED_PAGE_LIMIT = 10;
  uint8_t unattendedAutoTurns = 0;
  // Running auto turn reports activity this often, under the shortest (1 min) sleep timeout.
  static constexpr unsigned long AUTO_TURN_AWAKE_PULSE_MS = 30000UL;
  unsigned long lastAwakePulseMs = 0UL;
  // Each nudge scales the learned speed by NUDGE_PERCENT.
  static constexpr uint16_t NUDGE_PERCENT = 10;
  // readingSpeedWpm changed in RAM and still needs saving.
  bool dirtyReadingSpeedWpm = false;
  bool showBookmarkMessage = false;
  bool showDictionaryMessage = false;
  unsigned long dictionaryMessageTime = 0UL;
  bool currentPageBookmarked = false;
  int idlePrewarmSpine = -1;
  int idlePrewarmPage = -1;
  unsigned long lastRenderCompleteMs = 0;
  bool bookmarkRemoved = false;
  std::vector<BookmarkEntry> cachedBookmarks;
  bool recentsEntryRemoved = false;
  unsigned long bookmarkMessageTime = 0UL;
  bool pendingReadFolderMove = false;

  // Toolbar reader menu (SETTINGS.readerMenuStyle == READER_MENU_TOOLBAR): drawn
  // over the page instead of pushing the full-screen list menu. Select opens the
  // Toolbar; its tools open the Contents/Text/More bottom-sheet panels.
  enum class Overlay { None, Toolbar, Contents, Text, More };
  Overlay overlay = Overlay::None;
  int focusedTool = 0;  // toolbar tool focus: 0=Contents, 1=Text, 2=More
  int panelIndex = 0;   // selected row within the active panel
  // Panel list navigation: a tap steps one row, a hold jumps PANEL_HOLD_STEP rows in one go
  // (a contents list runs to hundreds of chapters). One jump per hold, not a repeat -- every
  // step repaints the panel, so repeating is bounded by the e-ink refresh anyway and reads as
  // sluggish. True once a hold has jumped, so the release that ends it is swallowed.
  static constexpr unsigned long PANEL_HOLD_MS = 1500;
  static constexpr int PANEL_HOLD_STEP = 10;
  bool panelHoldJumped = false;
  // Whether the panel draws its cursor row. Button boards always do; touch
  // boards only once a button has moved it, so a tapped row is not left inverted.
  bool panelCursorShown = false;
  // FreeInkUI chrome + tap targets for the overlay; created when it opens,
  // released when it closes.
  std::unique_ptr<ReaderToolbarUi> toolbarUi;
  // Modal option picker over the panel (same component the Settings screens
  // use), for enum rows: font size / line spacing / alignment / orientation /
  // auto page turn. Toggle rows stay one-tap toggles, as in Settings.
  OptionPopup overlayPopup;
  // True while a clean-page snapshot (renderer.storeBwBuffer) backs the open
  // overlay, letting panel->toolbar steps restore the page without a full
  // re-render. Discarded on close / whenever the page under the overlay changes.
  bool overlayPageStored = false;
  // True while a deferred overlay chrome refresh (pushOverlayRefresh) may still
  // be running on the panel. settleOverlayRefresh() must run before the
  // framebuffer is touched or another differential refresh is pushed.
  bool overlayRefreshPending = false;
  void pushOverlayRefresh();
  void settleOverlayRefresh();
  std::vector<EpubReaderMenuActivity::MenuItem> moreItems;

  // Footnote support
  std::vector<FootnoteEntry> currentPageFootnotes;
  std::vector<PageLink> currentPageLinks;
  int currentPageLinkMarginLeft = 0;
  int currentPageLinkMarginTop = 0;
  struct SavedPosition {
    int spineIndex;
    int pageNumber;
  };
  static constexpr int MAX_FOOTNOTE_DEPTH = 3;
  SavedPosition savedPositions[MAX_FOOTNOTE_DEPTH] = {};
  int footnoteDepth = 0;

  uint16_t buildViewportWidth = 0;
  uint16_t buildViewportHeight = 0;
  bool partialRebuildStartFailed = false;

  int lastSavedSpineIndex = -1;
  int lastSavedPage = -1;
  int lastSavedPageCount = -1;

  static constexpr int BUILD_PAGES_PER_CHUNK = 8;
  static constexpr int BACKGROUND_BUILD_PAGES_PER_TICK = 2;
  static constexpr size_t BACKGROUND_BUILD_MIN_FREE_HEAP = 32 * 1024;
  static constexpr size_t BACKGROUND_BUILD_MIN_MAX_ALLOC = 16 * 1024;
  // Requires the render lock; heap admission is checked separately by the build tick.
  bool backgroundBuildWanted() const;
  bool buildTickHeapGate();
  bool buildHeapPaused = false;
  static constexpr size_t RENDER_MIN_FREE_HEAP = 24 * 1024;
  static constexpr int BUILD_WINDOW_AHEAD = 5;
  static constexpr int PARTIAL_REBUILD_START_MARGIN = 15;
  static constexpr int BUILD_POPUP_PAGE_THRESHOLD = 20;
  static constexpr size_t BUILD_POPUP_BYTE_THRESHOLD = 96 * 1024;
  static constexpr unsigned long BUILD_POPUP_DEADLINE_MS = 1000;
  bool buildPopupPending = false;
  void showBuildPopup(GfxRenderer& renderer, int& pagesUntilFullRefresh);
  bool applyDeferredReposition();
  void clearDeferredReposition();
  void rememberCurrentContentOffset();
  bool saveProgress(int spineIndex, int currentPage, int pageCount);
  void jumpToPercent(int percent);
  void onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction action);
  // Live section position, or the values cached before a child screen
  // released the section.
  ChapterPosition chapterPosition() const;
  int bookPercentFor(const ChapterPosition& position) const;
  void openReaderMenu();
  // Toolbar reader menu (see Overlay above).
  bool usesToolbarMenu() const;
  void openOverlay(Overlay target);
  void closeOverlayToPage();
  void discardOverlayPage();
  void handleOverlayInput();
  void renderOverlay();
  std::string currentChapterTitle() const;
  // Text panel rows (font, size, line spacing, alignment, focus reading).
  std::string textRowName(int row) const;
  std::string textRowValue(int row) const;
  void showTextRowPopup(int row);
  // Persist + re-paginate + re-render under the open panel (live preview).
  void applyTextSettingLive();
  void paintOverlayPopup();
  // Persist the reader text settings, (re)load the selected SD font, and
  // re-paginate the current chapter so changes apply without re-opening the book.
  void applyReaderTextSettings();
  // More panel rows.
  void buildMoreActions();
  std::string moreRowName(int row) const;
  std::string moreRowValue(int row) const;
  void activateMoreRow(int row);
  void openFootnoteSelect(bool reopenMenuOnCancel);
  void openDictionaryWordSelect();
  bool launchKOReaderSync();
  unsigned long confirmLongPressThreshold() const;
  void toggleAutoPageTurn(uint8_t selectedPageTurnOption);
  bool smartPageTurnActive() const;
  // Turn duration for a page of wordCount words at wpm, never below MIN_SMART_DURATION_MS.
  static unsigned long smartPageDurationMs(uint16_t wordCount, uint16_t wpm);
  // Feeds a manual page turn into the learned reading speed; elapsedMs is the time spent on the page left.
  void adaptReadingSpeed(bool isForwardTurn, unsigned long elapsedMs);
  void manualPageTurn(bool isForwardTurn);
  void flushReadingSpeed();
  void nudgeReadingSpeed(bool faster);
  // Estimated minutes left in the chapter at the learned speed, -1 when there is no estimate.
  int chapterMinutesLeft() const;
  // Option the auto page-turn picker opens on: the running one, else the last one used.
  int autoTurnPickerIndex() const;
  void loadCachedBookmarks();
  void addBookmark();
  void updateBookmarkFlag();

  void navigateToHref(const std::string& href, bool savePosition = false);
  void restoreSavedPosition();

  void renderContents(std::unique_ptr<Page> page, int orientedMarginTop, int orientedMarginRight,
                      int orientedMarginBottom, int orientedMarginLeft);
  void renderStatusBar() const;
  void applyOrientation(uint8_t orientation);
  void applyInitialOrientation() override;
  // The orientation the current layout was built for. The control center's
  // orientation tile can move SETTINGS.orientation while this reader sits on
  // the activity stack, and Pop restores it without onEnter(), so the drift has
  // to be noticed here rather than assumed away.
  uint8_t appliedOrientation = 0;

  bool loadBook() override;
  std::string getBookTitle() const override { return epub ? epub->getTitle() : ""; }
  std::string getBookAuthor() const override { return epub ? epub->getAuthor() : ""; }
  std::string getBookThumbBmpPath() const override { return epub ? epub->getThumbBmpPath() : ""; }
  void renderBook() override;
  void onEndOfBookRendered() override;

 public:
  explicit EpubReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath,
                              bool allowFastInitialRefresh)
      : ReaderActivity("EpubReader", renderer, mappedInput, std::move(bookPath), allowFastInitialRefresh) {}
  ~EpubReaderActivity() override;

  void onExit() override;
  bool preventAutoSleep() override;
  void loop() override;

  bool pageTurn(bool isForward) override;
  bool skipPages(int amount) override;
  bool isAtEndOfBook() const override;
  void onReturnFromEndOfBook() override;

  bool skipLoopDelay() override;

  ScreenshotInfo getScreenshotInfo() const override;
  CrossPointPosition getCurrentPosition() const;
};
