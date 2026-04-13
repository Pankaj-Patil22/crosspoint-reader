#include "RssFeedStore.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>

#include "util/StringUtils.h"

RssFeedStore RssFeedStore::instance;

static constexpr const char* RSS_FEEDS_PATH = "/.crosspoint/rss_feeds.json";

bool RssFeedStore::addFeed(const std::string& name, const std::string& url) {
  if (name.empty() || url.empty()) return false;
  if (feeds.size() >= MAX_FEEDS) return false;
  for (const auto& f : feeds) {
    if (f.url == url) return false;
  }
  feeds.push_back({name, url});
  return true;
}

void RssFeedStore::removeFeed(const size_t index) {
  if (index < feeds.size()) {
    unreadCounts.erase(feeds[index].url);
    feeds.erase(feeds.begin() + static_cast<int>(index));
  }
}

bool RssFeedStore::removeFeedByUrl(const std::string& url) {
  for (auto it = feeds.begin(); it != feeds.end(); ++it) {
    if (it->url == url) {
      unreadCounts.erase(url);
      feeds.erase(it);
      return true;
    }
  }
  return false;
}

int RssFeedStore::getUnreadCount(const std::string& url) const {
  auto it = unreadCounts.find(url);
  return it != unreadCounts.end() ? it->second : -1;
}

void RssFeedStore::setUnreadCount(const std::string& url, int count) {
  unreadCounts[url] = count;
}

void RssFeedStore::clearUnreadCount(const std::string& url) {
  unreadCounts.erase(url);
}

std::string RssFeedStore::feedCachePath(const std::string& feedUrl) {
  return "/.crosspoint/rss_cache_" + StringUtils::sanitizeFilename(feedUrl, 40) + ".json";
}

bool RssFeedStore::cleanupFiles(const std::string& feedUrl) const {
  const std::string cachePath = feedCachePath(feedUrl);
  if (!Storage.exists(cachePath.c_str())) return true;

  // Delete each article .txt file listed in the cache
  const auto json = Storage.readFile(cachePath.c_str());
  if (!json.isEmpty()) {
    JsonDocument doc;
    if (!deserializeJson(doc, json)) {
      for (JsonObject obj : doc.as<JsonArray>()) {
        const std::string path = obj["f"] | std::string("");
        if (!path.empty() && Storage.exists(path.c_str())) {
          Storage.remove(path.c_str());
        }
      }
    }
  }
  return Storage.remove(cachePath.c_str());
}

bool RssFeedStore::loadFromFile() {
  feeds.clear();

  if (!Storage.exists(RSS_FEEDS_PATH)) {
    LOG_DBG("RSS", "No rss_feeds.json found, starting with empty list");
    return true;
  }

  const auto json = Storage.readFile(RSS_FEEDS_PATH);
  if (json.isEmpty()) return false;

  JsonDocument doc;
  const auto err = deserializeJson(doc, json);
  if (err) {
    LOG_ERR("RSS", "JSON parse error: %s", err.c_str());
    return false;
  }

  JsonArray arr = doc.as<JsonArray>();
  for (JsonObject obj : arr) {
    if (static_cast<int>(feeds.size()) >= MAX_FEEDS) break;
    RssFeed feed;
    feed.name = obj["name"] | std::string("");
    feed.url = obj["url"] | std::string("");
    if (!feed.name.empty() && !feed.url.empty()) {
      feeds.push_back(std::move(feed));
    }
  }

  LOG_DBG("RSS", "Loaded %zu RSS feeds", feeds.size());
  return true;
}

bool RssFeedStore::saveToFile() const {
  Storage.mkdir("/.crosspoint");
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (const auto& feed : feeds) {
    JsonObject obj = arr.add<JsonObject>();
    obj["name"] = feed.name;
    obj["url"] = feed.url;
  }

  String json;
  serializeJson(doc, json);
  return Storage.writeFile(RSS_FEEDS_PATH, json);
}
