#include "RssParser.h"

#include <Logging.h>

#include <cstring>

RssParser::RssParser() {
  parser = XML_ParserCreate(nullptr);
  if (!parser) {
    errorOccured = true;
    LOG_DBG("RSS", "Couldn't allocate memory for parser");
    return;
  }
  XML_SetUserData(parser, this);
  XML_SetElementHandler(parser, startElement, endElement);
  XML_SetCharacterDataHandler(parser, characterData);
}

RssParser::~RssParser() {
  if (parser) {
    XML_StopParser(parser, XML_FALSE);
    XML_SetElementHandler(parser, nullptr, nullptr);
    XML_SetCharacterDataHandler(parser, nullptr);
    XML_ParserFree(parser);
    parser = nullptr;
  }
}

size_t RssParser::write(uint8_t c) { return write(&c, 1); }

size_t RssParser::write(const uint8_t* xmlData, const size_t length) {
  if (errorOccured) {
    return length;
  }

  const char* currentPos = reinterpret_cast<const char*>(xmlData);
  size_t remaining = length;
  constexpr size_t chunkSize = 1024;

  while (remaining > 0) {
    void* const buf = XML_GetBuffer(parser, chunkSize);
    if (!buf) {
      errorOccured = true;
      LOG_DBG("RSS", "Couldn't allocate memory for buffer");
      XML_ParserFree(parser);
      parser = nullptr;
      return length;
    }

    const size_t toRead = remaining < chunkSize ? remaining : chunkSize;
    memcpy(buf, currentPos, toRead);

    if (XML_ParseBuffer(parser, static_cast<int>(toRead), 0) == XML_STATUS_ERROR) {
      errorOccured = true;
      LOG_DBG("RSS", "Parse error at line %lu: %s", XML_GetCurrentLineNumber(parser),
              XML_ErrorString(XML_GetErrorCode(parser)));
      XML_ParserFree(parser);
      parser = nullptr;
      return length;
    }

    currentPos += toRead;
    remaining -= toRead;
  }
  return length;
}

void RssParser::flush() {
  if (!parser) return;
  if (XML_Parse(parser, nullptr, 0, XML_TRUE) != XML_STATUS_OK) {
    errorOccured = true;
    XML_ParserFree(parser);
    parser = nullptr;
  }
}

bool RssParser::error() const { return errorOccured; }

void XMLCALL RssParser::startElement(void* userData, const XML_Char* name, const XML_Char** /*atts*/) {
  auto* self = static_cast<RssParser*>(userData);

  if (strcmp(name, "item") == 0) {
    self->inItem = true;
    self->currentItem = RssItem{};
    return;
  }

  if (strcmp(name, "title") == 0) {
    self->inTitle = true;
    self->currentText.clear();
    // Track whether we're in channel title (outside item) or item title
    self->inChannelTitle = !self->inItem;
    return;
  }

  if (!self->inItem) return;

  if (strcmp(name, "link") == 0) {
    self->inLink = true;
    self->currentText.clear();
    return;
  }

  if (strcmp(name, "description") == 0) {
    self->inDescription = true;
    self->currentText.clear();
    return;
  }

  if (strcmp(name, "pubDate") == 0) {
    self->inPubDate = true;
    self->currentText.clear();
    return;
  }

  if (strcmp(name, "guid") == 0) {
    self->inGuid = true;
    self->currentText.clear();
    return;
  }
}

void XMLCALL RssParser::endElement(void* userData, const XML_Char* name) {
  auto* self = static_cast<RssParser*>(userData);

  if (strcmp(name, "item") == 0) {
    // Finalize the item
    if (!self->currentItem.title.empty()) {
      // Invoke callback so caller can process/clear description before storage
      if (self->itemCallback) {
        self->itemCallback(self->currentItem);
      }
      self->items.push_back(std::move(self->currentItem));
      self->currentItem = RssItem{};
    }
    self->inItem = false;
    return;
  }

  if (strcmp(name, "title") == 0) {
    if (self->inChannelTitle && !self->inItem) {
      self->channelTitle = self->currentText;
    } else if (self->inTitle && self->inItem) {
      self->currentItem.title = self->currentText;
    }
    self->inTitle = false;
    self->inChannelTitle = false;
    return;
  }

  if (!self->inItem) return;

  if (strcmp(name, "link") == 0) {
    if (self->inLink) {
      self->currentItem.link = self->currentText;
    }
    self->inLink = false;
    return;
  }

  if (strcmp(name, "description") == 0) {
    if (self->inDescription) {
      self->currentItem.description = self->currentText;
    }
    self->inDescription = false;
    return;
  }

  if (strcmp(name, "pubDate") == 0) {
    if (self->inPubDate) {
      self->currentItem.pubDate = self->currentText;
    }
    self->inPubDate = false;
    return;
  }

  if (strcmp(name, "guid") == 0) {
    if (self->inGuid) {
      self->currentItem.guid = self->currentText;
    }
    self->inGuid = false;
    return;
  }
}

void XMLCALL RssParser::characterData(void* userData, const XML_Char* s, const int len) {
  auto* self = static_cast<RssParser*>(userData);

  if (self->inTitle || self->inLink || self->inPubDate || self->inGuid) {
    self->currentText.append(s, len);
    return;
  }

  if (self->inDescription) {
    // Cap description size to avoid excessive RAM usage
    if (self->currentText.size() < MAX_DESCRIPTION_SIZE) {
      const size_t available = MAX_DESCRIPTION_SIZE - self->currentText.size();
      const size_t toAppend = static_cast<size_t>(len) < available ? static_cast<size_t>(len) : available;
      self->currentText.append(s, toAppend);
    }
  }
}
