#include "RssStream.h"

RssParserStream::RssParserStream(RssParser& parser) : parser(parser) {}

RssParserStream::~RssParserStream() { parser.flush(); }

int RssParserStream::available() { return 0; }
int RssParserStream::peek() { abort(); }
int RssParserStream::read() { abort(); }

size_t RssParserStream::write(uint8_t c) { return parser.write(c); }

size_t RssParserStream::write(const uint8_t* buffer, size_t size) { return parser.write(buffer, size); }
