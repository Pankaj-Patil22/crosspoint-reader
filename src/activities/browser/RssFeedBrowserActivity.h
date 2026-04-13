#pragma once
#include <RssParser.h>

#include <string>
#include <vector>

#include "../Activity.h"
#include "RssFeedStore.h"
#include "util/ButtonNavigator.h"

/**
 * Two-level RSS feed browser.
 *
 * Level 1 — Feed list (top to bottom):
 *   [0] ↺ Refresh All  → connect WiFi, fetch every feed, update unread counts, stay on feed list.
 *   [1] +  Add Feed     → keyboard entry for URL then name.
 *   [2+]   Feed entries → name + unread count badge.
 *          Confirm      → open (load cached articles, no network).
 *          Long-press   → confirm delete.
 *
 * Level 2 — Article list: cached items for the selected feed.
 *          Unread articles show a filled dot on the left.
 *          Confirm      → open in reader (marks as read).
 *          Back         → feed list (unread count refreshed from memory).
 */
class RssFeedBrowserActivity final : public Activity {
 public:
  enum class State {
    FEED_LIST,       // Browsing feed list
    ARTICLE_LIST,    // Browsing cached articles
    CHECK_WIFI,      // Checking WiFi before refresh
    WIFI_SELECTION,  // WiFi selection sub-activity running
    LOADING,         // Fetching / parsing one or more feeds
    ERROR,           // Network or parse error
  };

  explicit RssFeedBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("RssFeedBrowser", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  struct ArticleMeta {
    std::string title;
    std::string guid;
    std::string pubDate;
    std::string txtPath;  // Path to stripped plain-text file on SD
    bool read = false;
  };

  ButtonNavigator buttonNavigator;
  State state = State::FEED_LIST;
  State errorReturnState = State::FEED_LIST;

  // Feed list level
  // List indices: 0 = Refresh All, 1 = Add, 2+ = feeds (feedIndex = selector - 2)
  int feedSelector = 0;
  std::vector<int> feedUnreadCounts;

  // Article list level
  std::vector<ArticleMeta> articles;
  int articleSelector = 0;
  std::string currentFeedName;
  std::string currentFeedUrl;

  // Loading / error
  std::string statusMessage;
  std::string errorMessage;

  // Long-press delete tracking
  bool confirmHeld = false;

  // ---- feed list actions ----
  void openFeed(size_t feedIndex);     // Instant: load cache → ARTICLE_LIST
  void startRefreshAll();              // WiFi check → doRefreshAll()
  void doRefreshAll();                 // Fetch every feed sequentially (called post-WiFi)
  void startAddFeed();
  void deleteFeedWithConfirm(size_t feedIndex);

  // ---- single-feed silent fetch (used by doRefreshAll) ----
  // Fetches feedUrl, preserves existing read flags, writes updated cache.
  // Does not modify instance articles / currentFeedUrl.
  void fetchAndSaveCacheForFeed(const std::string& feedUrl);

  // ---- article actions ----
  void openArticle(size_t index);

  // ---- unread counts ----
  void loadAllUnreadCounts();
  void updateUnreadCountForCurrentFeed();
  static int countUnreadFromCache(const std::string& feedUrl);

  // ---- per-feed cache ----
  void loadCachedArticles();
  void saveFeedCache() const;

  // ---- SD helpers ----
  static std::string articleTxtPath(const std::string& guid);
  static bool writeArticleToSd(const std::string& title, const std::string& htmlContent,
                                const std::string& path);
  static std::string htmlToPlainText(const std::string& html);

  bool preventAutoSleep() override { return true; }

  static constexpr int PAGE_ITEMS = 23;
  // Number of fixed items before actual feeds in the list
  static constexpr int FEED_LIST_HEADER_ITEMS = 2;  // Refresh All + Add
};
