#pragma once
#include <Print.h>
#include <expat.h>

#include <functional>
#include <string>
#include <vector>

/**
 * Represents a single item from an RSS 2.0 feed.
 */
struct RssItem {
  std::string title;
  std::string link;
  std::string description;  // HTML content (XML entities decoded by Expat)
  std::string pubDate;
  std::string guid;
};

/**
 * Parser for RSS 2.0 feeds.
 * Uses the Expat XML parser to extract channel metadata and items.
 *
 * The description field in each item contains HTML content with XML entities
 * already decoded by Expat (e.g. &lt; becomes <).
 *
 * An optional item callback can be set to process items as they are parsed,
 * which allows clearing the description from memory immediately to reduce
 * peak RAM usage when parsing feeds with large chapter content.
 *
 * Usage:
 *   RssParser parser;
 *   parser.setItemCallback([](RssItem& item) {
 *     // Process item.description here, then clear it:
 *     item.description.clear();
 *   });
 *   // Feed data via write() calls (e.g. from HttpDownloader::fetchUrl)
 *   parser.flush();
 *   for (const auto& item : parser.getItems()) { ... }
 */
class RssParser final : public Print {
 public:
  using ItemCallback = std::function<void(RssItem&)>;

  RssParser();
  ~RssParser();

  // Disable copy
  RssParser(const RssParser&) = delete;
  RssParser& operator=(const RssParser&) = delete;

  size_t write(uint8_t) override;
  size_t write(const uint8_t*, size_t) override;
  void flush() override;

  bool error() const;
  operator bool() { return !error(); }

  /**
   * Set a callback to be invoked when each <item> is fully parsed.
   * The callback may modify item (e.g. clear description to free RAM).
   * Items are stored after the callback returns.
   */
  void setItemCallback(ItemCallback cb) { itemCallback = std::move(cb); }

  const std::string& getChannelTitle() const { return channelTitle; }
  const std::vector<RssItem>& getItems() const& { return items; }
  std::vector<RssItem> getItems() && { return std::move(items); }

 private:
  static void XMLCALL startElement(void* userData, const XML_Char* name, const XML_Char** atts);
  static void XMLCALL endElement(void* userData, const XML_Char* name);
  static void XMLCALL characterData(void* userData, const XML_Char* s, int len);

  XML_Parser parser = nullptr;
  std::vector<RssItem> items;
  RssItem currentItem;
  std::string currentText;
  std::string channelTitle;
  ItemCallback itemCallback;

  // Maximum description size per item to limit RAM usage
  static constexpr size_t MAX_DESCRIPTION_SIZE = 64 * 1024;

  bool inItem = false;
  bool inTitle = false;
  bool inLink = false;
  bool inDescription = false;
  bool inPubDate = false;
  bool inGuid = false;
  bool inChannelTitle = false;

  bool errorOccured = false;
};
