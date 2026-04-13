#pragma once
#include <string>
#include <unordered_map>
#include <vector>

/**
 * Represents a user-configured RSS feed with a display name and URL.
 */
struct RssFeed {
  std::string name;
  std::string url;
};

/**
 * Stores the list of user-configured RSS feeds.
 * Persisted to /.crosspoint/rss_feeds.json on the SD card.
 *
 * Usage: RSS_FEEDS.addFeed(name, url); RSS_FEEDS.saveToFile();
 */
class RssFeedStore {
  static RssFeedStore instance;
  std::vector<RssFeed> feeds;
  std::unordered_map<std::string, int> unreadCounts;

 public:
  static constexpr int MAX_FEEDS = 20;

  RssFeedStore() = default;
  RssFeedStore(const RssFeedStore&) = delete;
  RssFeedStore& operator=(const RssFeedStore&) = delete;

  static RssFeedStore& getInstance() { return instance; }

  /**
   * Add a new feed. Returns false if MAX_FEEDS is reached, name/url are empty,
   * or a feed with the same URL already exists.
   */
  bool addFeed(const std::string& name, const std::string& url);

  /**
   * Remove a feed by index. No-op if index is out of range.
   */
  void removeFeed(size_t index);

  /**
   * Remove the first feed whose URL matches. Returns true if found and removed.
   */
  bool removeFeedByUrl(const std::string& url);

  const std::vector<RssFeed>& getFeeds() const { return feeds; }
  size_t getCount() const { return feeds.size(); }

  /**
   * Cached unread counts – avoids re-reading SD on every activity entry.
   * Returns -1 when no cached value exists for the given URL.
   */
  int getUnreadCount(const std::string& url) const;
  void setUnreadCount(const std::string& url, int count);
  void clearUnreadCount(const std::string& url);

  /**
   * Compute the path of the per-feed article-metadata cache file.
   * Shared with the browser activity and the web server.
   */
  static std::string feedCachePath(const std::string& feedUrl);

  /**
   * Delete the feed's cache JSON and every article .txt file it references.
   * Call this before removeFeed/removeFeedByUrl when the user deletes a feed.
   */
  bool cleanupFiles(const std::string& feedUrl) const;

  bool loadFromFile();
  bool saveToFile() const;
};

#define RSS_FEEDS RssFeedStore::getInstance()
