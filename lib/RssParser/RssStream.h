#pragma once
#include <Stream.h>

#include "RssParser.h"

/**
 * Stream wrapper that feeds data into an RssParser.
 * Designed for use with HttpDownloader::fetchUrl(url, stream).
 * The destructor calls parser.flush() to finalize parsing.
 */
class RssParserStream : public Stream {
 public:
  explicit RssParserStream(RssParser& parser);
  ~RssParserStream() override;

  // Output only — read operations are not supported
  int available() override;
  int peek() override;
  int read() override;

  size_t write(uint8_t c) override;
  size_t write(const uint8_t* buffer, size_t size) override;

 private:
  RssParser& parser;
};
