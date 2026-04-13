#include "RssFeedBrowserActivity.h"

#include <ArduinoJson.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <RssStream.h>
#include <WiFi.h>

#include <cctype>
#include <cstring>
#include <unordered_map>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/HttpDownloader.h"
#include "util/StringUtils.h"

// ---------------------------------------------------------------------------
// HTML → plain text
// ---------------------------------------------------------------------------

std::string RssFeedBrowserActivity::htmlToPlainText(const std::string& html) {
  std::string result;
  result.reserve(html.size());

  bool inTag = false;

  for (size_t i = 0; i < html.size(); i++) {
    const char c = html[i];

    if (c == '<') {
      inTag = true;
      // Detect block-level tags and emit a newline
      size_t j = i + 1;
      if (j < html.size() && html[j] == '/') j++;  // closing tag
      char tag[8] = {};
      int ti = 0;
      while (j < html.size() && html[j] != '>' && html[j] != ' ' && html[j] != '/' && ti < 7) {
        tag[ti++] = static_cast<char>(tolower(static_cast<unsigned char>(html[j++])));
      }
      if (strcmp(tag, "p") == 0 || strcmp(tag, "br") == 0 || strcmp(tag, "div") == 0 ||
          strcmp(tag, "h1") == 0 || strcmp(tag, "h2") == 0 || strcmp(tag, "h3") == 0 ||
          strcmp(tag, "h4") == 0 || strcmp(tag, "li") == 0) {
        if (!result.empty() && result.back() != '\n') result += '\n';
      }
    } else if (c == '>') {
      inTag = false;
    } else if (!inTag) {
      if (c == '&') {
        // Decode HTML entity
        const size_t start = i + 1;
        size_t end = start;
        while (end < html.size() && html[end] != ';' && html[end] != '<' && end - start < 10) end++;

        if (end < html.size() && html[end] == ';') {
          const char* ent = html.c_str() + start;
          const int entLen = static_cast<int>(end - start);

          if (strncmp(ent, "nbsp", 4) == 0 && entLen == 4) {
            result += ' ';
          } else if (strncmp(ent, "amp", 3) == 0 && entLen == 3) {
            result += '&';
          } else if (strncmp(ent, "lt", 2) == 0 && entLen == 2) {
            result += '<';
          } else if (strncmp(ent, "gt", 2) == 0 && entLen == 2) {
            result += '>';
          } else if (strncmp(ent, "quot", 4) == 0 && entLen == 4) {
            result += '"';
          } else if (strncmp(ent, "apos", 4) == 0 && entLen == 4) {
            result += '\'';
          } else if (strncmp(ent, "ndash", 5) == 0 && entLen == 5) {
            result += '-';
          } else if (strncmp(ent, "mdash", 5) == 0 && entLen == 5) {
            result += '-';
          } else if (entLen > 0 && ent[0] == '#') {
            // Numeric entity: &#NNN; or &#xHH;
            char numStr[9] = {};
            const int copyLen = entLen - 1 < 8 ? entLen - 1 : 8;
            memcpy(numStr, ent + 1, copyLen);
            int code = 0;
            if (numStr[0] == 'x' || numStr[0] == 'X') {
              code = static_cast<int>(strtol(numStr + 1, nullptr, 16));
            } else {
              code = atoi(numStr);
            }
            if (code == 160) {
              result += ' ';  // non-breaking space
            } else if (code > 0 && code < 128) {
              result += static_cast<char>(code);
            } else if (code >= 0x80 && code < 0x800) {
              result += static_cast<char>(0xC0 | (code >> 6));
              result += static_cast<char>(0x80 | (code & 0x3F));
            } else if (code >= 0x800 && code < 0x10000) {
              result += static_cast<char>(0xE0 | (code >> 12));
              result += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
              result += static_cast<char>(0x80 | (code & 0x3F));
            }
          }
          i = end;  // for loop will increment past ';'
        } else {
          result += c;
        }
      } else {
        result += c;
      }
    }
  }

  // Normalise: collapse runs of blank lines to at most one blank line,
  // and trim trailing whitespace from each line.
  std::string normalized;
  normalized.reserve(result.size());
  int consecutiveNewlines = 0;

  for (size_t i = 0; i < result.size(); i++) {
    const char c = result[i];
    if (c == '\n') {
      consecutiveNewlines++;
      if (consecutiveNewlines <= 2) normalized += '\n';
    } else if (c == '\r') {
      // Skip carriage returns
    } else {
      consecutiveNewlines = 0;
      normalized += c;
    }
  }

  // Free result before returning to avoid holding two large strings simultaneously.
  result.clear();
  result.shrink_to_fit();

  return normalized;
}

// ---------------------------------------------------------------------------
// SD write helpers
// ---------------------------------------------------------------------------

std::string RssFeedBrowserActivity::articleTxtPath(const std::string& guid) {
  const std::string safe = StringUtils::sanitizeFilename(guid, 32);
  return "/.crosspoint/rss_" + safe + ".txt";
}

bool RssFeedBrowserActivity::writeArticleToSd(const std::string& title, const std::string& htmlContent,
                                               const std::string& path) {
  const std::string plain = htmlToPlainText(htmlContent);

  // Build file: title heading, underline, then body.
  // Construct the Arduino String directly to avoid an extra std::string copy.
  String content;
  content.reserve(title.size() + plain.size() + 64);
  content += title.c_str();
  content += '\n';
  for (size_t i = 0; i < title.size() && i < 60; i++) content += '-';
  content += "\n\n";
  content += plain.c_str();

  return Storage.writeFile(path.c_str(), content);
}

// ---------------------------------------------------------------------------
// Activity lifecycle
// ---------------------------------------------------------------------------

void RssFeedBrowserActivity::onEnter() {
  Activity::onEnter();
  state = State::FEED_LIST;
  feedSelector = 0;
  articles.clear();
  articleSelector = 0;
  errorMessage.clear();
  statusMessage.clear();
  currentFeedName.clear();
  currentFeedUrl.clear();
  loadAllUnreadCounts();
  requestUpdate();
}

void RssFeedBrowserActivity::onExit() {
  Activity::onExit();
  WiFi.mode(WIFI_OFF);
  articles.clear();
}

// ---------------------------------------------------------------------------
// loop()
// ---------------------------------------------------------------------------

void RssFeedBrowserActivity::loop() {
  if (state == State::WIFI_SELECTION || state == State::LOADING || state == State::CHECK_WIFI) {
    if (state == State::CHECK_WIFI || state == State::LOADING) {
      if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
        state = State::FEED_LIST;
        requestUpdate();
      }
    }
    return;
  }

  if (state == State::ERROR) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      state = errorReturnState;
      requestUpdate();
    }
    return;
  }

  // ---- FEED_LIST ----
  if (state == State::FEED_LIST) {
    const int feedCount = static_cast<int>(RSS_FEEDS.getCount());
    const int totalItems = feedCount + FEED_LIST_HEADER_ITEMS;

    buttonNavigator.onNextRelease([this, totalItems] {
      feedSelector = ButtonNavigator::nextIndex(feedSelector, totalItems);
      requestUpdate();
    });
    buttonNavigator.onPreviousRelease([this, totalItems] {
      feedSelector = ButtonNavigator::previousIndex(feedSelector, totalItems);
      requestUpdate();
    });
    buttonNavigator.onNextContinuous([this, totalItems] {
      feedSelector = ButtonNavigator::nextPageIndex(feedSelector, totalItems, PAGE_ITEMS);
      requestUpdate();
    });
    buttonNavigator.onPreviousContinuous([this, totalItems] {
      feedSelector = ButtonNavigator::previousPageIndex(feedSelector, totalItems, PAGE_ITEMS);
      requestUpdate();
    });

    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      onGoHome();
      return;
    }

    // Long-press delete: only on actual feed entries (index >= FEED_LIST_HEADER_ITEMS)
    if (mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
        feedSelector >= FEED_LIST_HEADER_ITEMS && mappedInput.getHeldTime() > 600) {
      confirmHeld = true;
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      const bool wasLongPress = confirmHeld;
      confirmHeld = false;
      if (feedSelector == 0) {
        startRefreshAll();
      } else if (feedSelector == 1) {
        startAddFeed();
      } else if (wasLongPress) {
        deleteFeedWithConfirm(feedSelector - FEED_LIST_HEADER_ITEMS);
      } else {
        openFeed(feedSelector - FEED_LIST_HEADER_ITEMS);
      }
      return;
    }
    return;
  }

  // ---- ARTICLE_LIST ----
  if (state == State::ARTICLE_LIST) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      updateUnreadCountForCurrentFeed();
      state = State::FEED_LIST;
      articles.clear();
      articleSelector = 0;
      requestUpdate();
      return;
    }

    if (articles.empty()) return;

    buttonNavigator.onPreviousRelease([this] {
      articleSelector = ButtonNavigator::previousIndex(articleSelector, articles.size());
      requestUpdate();
    });
    buttonNavigator.onPreviousContinuous([this] {
      articleSelector = ButtonNavigator::previousPageIndex(articleSelector, articles.size(), PAGE_ITEMS);
      requestUpdate();
    });
    buttonNavigator.onNextRelease([this] {
      articleSelector = ButtonNavigator::nextIndex(articleSelector, articles.size());
      requestUpdate();
    });
    buttonNavigator.onNextContinuous([this] {
      articleSelector = ButtonNavigator::nextPageIndex(articleSelector, articles.size(), PAGE_ITEMS);
      requestUpdate();
    });

    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      openArticle(articleSelector);
    }
  }
}

// ---------------------------------------------------------------------------
// render()
// ---------------------------------------------------------------------------

void RssFeedBrowserActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  // Header
  renderer.drawCenteredText(UI_12_FONT_ID, 15, tr(STR_RSS_FEEDS), true, EpdFontFamily::BOLD);

  // ---- Loading / status overlays ----
  if (state == State::CHECK_WIFI || state == State::LOADING) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, statusMessage.c_str());
    // No button hints: the fetch is blocking so Back cannot be processed mid-load.
    renderer.displayBuffer();
    return;
  }

  if (state == State::WIFI_SELECTION) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_CHECKING_WIFI));
    renderer.displayBuffer();
    return;
  }

  if (state == State::ERROR) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 20, tr(STR_ERROR_MSG));
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 10, errorMessage.c_str());
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  // ---- FEED_LIST ----
  if (state == State::FEED_LIST) {
    const int feedCount = static_cast<int>(RSS_FEEDS.getCount());
    const int totalItems = feedCount + FEED_LIST_HEADER_ITEMS;

    if (feedCount == 0) {
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 15, tr(STR_NO_RSS_FEEDS));
    }

    const int pageStart = feedSelector / PAGE_ITEMS * PAGE_ITEMS;
    renderer.fillRect(0, 60 + (feedSelector % PAGE_ITEMS) * 30 - 2, pageWidth - 1, 30);

    for (int i = pageStart; i < totalItems && i < pageStart + PAGE_ITEMS; i++) {
      std::string label;
      if (i == 0) {
        label = std::string("↺ ") + tr(STR_REFRESH);
      } else if (i == 1) {
        label = std::string("+ ") + tr(STR_ADD_RSS_FEED);
      } else {
        const int fi = i - FEED_LIST_HEADER_ITEMS;
        label = RSS_FEEDS.getFeeds()[fi].name;
        const int unread = fi < static_cast<int>(feedUnreadCounts.size()) ? feedUnreadCounts[fi] : 0;
        if (unread > 0) label += " (" + std::to_string(unread) + ")";
      }
      auto text = renderer.truncatedText(UI_10_FONT_ID, label.c_str(), pageWidth - 40);
      renderer.drawText(UI_10_FONT_ID, 20, 60 + (i % PAGE_ITEMS) * 30, text.c_str(),
                        i != feedSelector);
    }

    const char* confirmLabel = (feedSelector >= FEED_LIST_HEADER_ITEMS) ? tr(STR_OPEN) : tr(STR_SELECT);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  // ---- ARTICLE_LIST ----
  if (state == State::ARTICLE_LIST) {
    if (articles.empty()) {
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_NO_ARTICLES));
      const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      renderer.displayBuffer();
      return;
    }

    const int pageStart = articleSelector / PAGE_ITEMS * PAGE_ITEMS;
    renderer.fillRect(0, 60 + (articleSelector % PAGE_ITEMS) * 30 - 2, pageWidth - 1, 30);

    for (size_t i = static_cast<size_t>(pageStart);
         i < articles.size() && i < static_cast<size_t>(pageStart + PAGE_ITEMS); i++) {
      const bool selected = static_cast<int>(i) == articleSelector;
      const int rowY = 60 + (i % PAGE_ITEMS) * 30;

      // Unread dot: 5×5 filled square, centred vertically in the row
      if (!articles[i].read) {
        renderer.fillRect(6, rowY + 12, 5, 5, !selected);
      }

      const std::string numbered = std::to_string(i + 1) + ". " + articles[i].title;
      auto text = renderer.truncatedText(UI_10_FONT_ID, numbered.c_str(), pageWidth - 40);
      renderer.drawText(UI_10_FONT_ID, 20, rowY, text.c_str(), !selected);
    }

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
  }
}

// ---------------------------------------------------------------------------
// Feed actions
// ---------------------------------------------------------------------------

void RssFeedBrowserActivity::openFeed(const size_t feedIndex) {
  const auto& feeds = RSS_FEEDS.getFeeds();
  if (feedIndex >= feeds.size()) return;

  currentFeedName = feeds[feedIndex].name;
  currentFeedUrl = feeds[feedIndex].url;
  articleSelector = 0;
  loadCachedArticles();
  state = State::ARTICLE_LIST;
  requestUpdate();
}

void RssFeedBrowserActivity::startRefreshAll() {
  if (RSS_FEEDS.getCount() == 0) return;

  errorReturnState = State::FEED_LIST;
  state = State::CHECK_WIFI;
  statusMessage = tr(STR_CHECKING_WIFI);
  requestUpdate();

  if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
    doRefreshAll();
    return;
  }

  state = State::WIFI_SELECTION;
  requestUpdate();
  startActivityForResult(
      std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
      [this](const ActivityResult& result) {
        if (!result.isCancelled && WiFi.status() == WL_CONNECTED) {
          doRefreshAll();
        } else {
          WiFi.disconnect();
          WiFi.mode(WIFI_OFF);
          state = State::FEED_LIST;
          requestUpdate();
        }
      });
}

void RssFeedBrowserActivity::doRefreshAll() {
  const auto& feeds = RSS_FEEDS.getFeeds();
  state = State::LOADING;
  for (size_t i = 0; i < feeds.size(); i++) {
    statusMessage = feeds[i].name + " (" + std::to_string(i + 1) + "/" +
                    std::to_string(feeds.size()) + ")";
    requestUpdate(true);
    fetchAndSaveCacheForFeed(feeds[i].url);
  }
  loadAllUnreadCounts();
  state = State::FEED_LIST;
  requestUpdate();
}

void RssFeedBrowserActivity::fetchAndSaveCacheForFeed(const std::string& feedUrl) {
  // Preserve existing read flags so they survive the refresh
  std::unordered_map<std::string, bool> readFlags;
  const std::string cachePath = RssFeedStore::feedCachePath(feedUrl);
  if (Storage.exists(cachePath.c_str())) {
    const auto json = Storage.readFile(cachePath.c_str());
    if (!json.isEmpty()) {
      JsonDocument existing;
      if (!deserializeJson(existing, json)) {
        for (JsonObject obj : existing.as<JsonArray>()) {
          if (obj["r"] | false) readFlags[obj["g"] | std::string("")] = true;
        }
      }
    }
  }

  std::vector<ArticleMeta> fetched;
  RssParser parser;
  parser.setItemCallback([&fetched, &readFlags](RssItem& item) {
    const std::string guid = item.guid.empty() ? item.link : item.guid;
    const std::string path = articleTxtPath(guid);
    const bool cached = Storage.exists(path.c_str());
    const bool written = cached || writeArticleToSd(item.title, item.description, path);
    if (written) {
      ArticleMeta meta;
      meta.title = item.title;
      meta.guid = guid;
      meta.pubDate = item.pubDate;
      meta.txtPath = path;
      meta.read = readFlags.count(guid) > 0;
      fetched.push_back(std::move(meta));
      if (!cached) LOG_DBG("RSS", "Saved: %s", path.c_str());
    }
    item.description.clear();
    item.description.shrink_to_fit();
  });

  {
    RssParserStream stream{parser};
    if (!HttpDownloader::fetchUrl(feedUrl, stream)) {
      LOG_ERR("RSS", "Failed to fetch: %s", feedUrl.c_str());
      return;
    }
  }

  if (!parser || fetched.empty()) return;

  Storage.mkdir("/.crosspoint");
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (const auto& a : fetched) {
    JsonObject obj = arr.add<JsonObject>();
    obj["t"] = a.title;
    obj["g"] = a.guid;
    obj["d"] = a.pubDate;
    obj["f"] = a.txtPath;
    obj["r"] = a.read;
  }
  String json;
  serializeJson(doc, json);
  if (!Storage.writeFile(cachePath.c_str(), json)) {
    LOG_ERR("RSS", "Failed to write cache for %s", feedUrl.c_str());
    return;
  }
  RSS_FEEDS.clearUnreadCount(feedUrl);  // Force re-count from the new cache on next load
  LOG_DBG("RSS", "Cached %zu articles for %s", fetched.size(), feedUrl.c_str());
}


void RssFeedBrowserActivity::openArticle(const size_t index) {
  if (index >= articles.size()) return;
  const ArticleMeta& article = articles[index];
  if (!Storage.exists(article.txtPath.c_str())) {
    LOG_ERR("RSS", "Article file missing: %s", article.txtPath.c_str());
    errorReturnState = State::ARTICLE_LIST;
    errorMessage = tr(STR_FETCH_RSS_FAILED);
    state = State::ERROR;
    requestUpdate();
    return;
  }
  if (!articles[index].read) {
    articles[index].read = true;
    saveFeedCache();
  }
  activityManager.goToReader(article.txtPath);
}

// ---------------------------------------------------------------------------
// Per-feed cache (article metadata + read flags)
// ---------------------------------------------------------------------------

void RssFeedBrowserActivity::loadAllUnreadCounts() {
  const auto& feeds = RSS_FEEDS.getFeeds();
  feedUnreadCounts.resize(feeds.size());
  for (size_t i = 0; i < feeds.size(); i++) {
    const int cached = RSS_FEEDS.getUnreadCount(feeds[i].url);
    if (cached >= 0) {
      feedUnreadCounts[i] = cached;  // Use in-memory value; no SD read needed
    } else {
      feedUnreadCounts[i] = countUnreadFromCache(feeds[i].url);
      RSS_FEEDS.setUnreadCount(feeds[i].url, feedUnreadCounts[i]);
    }
  }
}

int RssFeedBrowserActivity::countUnreadFromCache(const std::string& feedUrl) {
  const std::string path = RssFeedStore::feedCachePath(feedUrl);
  if (!Storage.exists(path.c_str())) return 0;
  const auto json = Storage.readFile(path.c_str());
  if (json.isEmpty()) return 0;
  JsonDocument doc;
  if (deserializeJson(doc, json)) return 0;
  int count = 0;
  for (JsonObject obj : doc.as<JsonArray>()) {
    if (!(obj["r"] | false)) count++;
  }
  return count;
}

void RssFeedBrowserActivity::updateUnreadCountForCurrentFeed() {
  const auto& feeds = RSS_FEEDS.getFeeds();
  for (size_t i = 0; i < feeds.size(); i++) {
    if (feeds[i].url == currentFeedUrl) {
      int count = 0;
      for (const auto& a : articles) {
        if (!a.read) count++;
      }
      if (i < feedUnreadCounts.size()) feedUnreadCounts[i] = count;
      RSS_FEEDS.setUnreadCount(currentFeedUrl, count);
      break;
    }
  }
}

void RssFeedBrowserActivity::loadCachedArticles() {
  articles.clear();
  const std::string path = RssFeedStore::feedCachePath(currentFeedUrl);
  if (!Storage.exists(path.c_str())) return;

  const auto json = Storage.readFile(path.c_str());
  if (json.isEmpty()) return;

  JsonDocument doc;
  if (deserializeJson(doc, json)) return;

  for (JsonObject obj : doc.as<JsonArray>()) {
    ArticleMeta meta;
    meta.title   = obj["t"] | std::string("");
    meta.guid    = obj["g"] | std::string("");
    meta.pubDate = obj["d"] | std::string("");
    meta.txtPath = obj["f"] | std::string("");
    meta.read    = obj["r"] | false;
    if (!meta.title.empty() && !meta.txtPath.empty()) {
      articles.push_back(std::move(meta));
    }
  }
  LOG_DBG("RSS", "Loaded %zu cached articles for feed", articles.size());
}

void RssFeedBrowserActivity::saveFeedCache() const {
  Storage.mkdir("/.crosspoint");
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (const ArticleMeta& a : articles) {
    JsonObject obj = arr.add<JsonObject>();
    obj["t"] = a.title;
    obj["g"] = a.guid;
    obj["d"] = a.pubDate;
    obj["f"] = a.txtPath;
    obj["r"] = a.read;
  }
  String json;
  serializeJson(doc, json);
  if (!Storage.writeFile(RssFeedStore::feedCachePath(currentFeedUrl).c_str(), json)) {
    LOG_ERR("RSS", "Failed to save feed cache for %s", currentFeedUrl.c_str());
  }
}

// ---------------------------------------------------------------------------
// Add / delete feeds
// ---------------------------------------------------------------------------

void RssFeedBrowserActivity::startAddFeed() {
  // Step 1: Enter URL
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_RSS_FEED_URL), "", 255, false),
      [this](const ActivityResult& urlResult) {
        if (urlResult.isCancelled) return;
        const std::string url = std::get<KeyboardResult>(urlResult.data).text;
        if (url.empty()) return;

        // Step 2: Enter name
        startActivityForResult(
            std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_RSS_FEED_NAME), "", 64, false),
            [this, url](const ActivityResult& nameResult) {
              if (nameResult.isCancelled) return;
              const std::string name = std::get<KeyboardResult>(nameResult.data).text;
              if (name.empty()) return;

              if (!RSS_FEEDS.addFeed(name, url)) {
                errorMessage = tr(STR_FETCH_RSS_FAILED);  // reuse generic error; feed full/duplicate
                errorReturnState = State::FEED_LIST;
                state = State::ERROR;
                requestUpdate();
                return;
              }
              if (!RSS_FEEDS.saveToFile()) {
                LOG_ERR("RSS", "Failed to persist feeds after add — change is in-memory only");
              }
              LOG_DBG("RSS", "Added feed: %s -> %s", name.c_str(), url.c_str());
              // Adjust selector so it stays valid
              feedUnreadCounts.resize(RSS_FEEDS.getCount(), 0);
              const int total = static_cast<int>(RSS_FEEDS.getCount()) + FEED_LIST_HEADER_ITEMS;
              if (feedSelector >= total) feedSelector = total - 1;
              requestUpdate();
            });
      });
}

void RssFeedBrowserActivity::deleteFeedWithConfirm(const size_t feedIndex) {
  const auto& feeds = RSS_FEEDS.getFeeds();
  if (feedIndex >= feeds.size()) return;

  const std::string heading = tr(STR_DELETE_RSS_FEED);
  const std::string body = feeds[feedIndex].name;
  // Capture URL now; feedIndex may shift if feeds change before the callback fires.
  const std::string urlToDelete = feeds[feedIndex].url;

  startActivityForResult(
      std::make_unique<ConfirmationActivity>(renderer, mappedInput, heading, body),
      [this, feedIndex, urlToDelete](const ActivityResult& result) {
        if (!result.isCancelled) {
          RSS_FEEDS.removeFeedByUrl(urlToDelete);
          if (!RSS_FEEDS.saveToFile()) {
            LOG_ERR("RSS", "Failed to persist feeds after delete — change is in-memory only");
          }
          RSS_FEEDS.cleanupFiles(urlToDelete);
          if (feedIndex < feedUnreadCounts.size()) {
            feedUnreadCounts.erase(feedUnreadCounts.begin() + feedIndex);
          }
          const int total = static_cast<int>(RSS_FEEDS.getCount()) + FEED_LIST_HEADER_ITEMS;
          if (feedSelector >= total) feedSelector = total - 1;
          if (feedSelector < 0) feedSelector = 0;
        }
        requestUpdate();
      });
}
