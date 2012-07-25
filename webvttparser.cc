// Copyright (c) 2012 The WebM project authors. All Rights Reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file in the root of the source
// tree. An additional intellectual property rights grant can be found
// in the file PATENTS.  All contributing project authors may
// be found in the AUTHORS file in the root of the source tree.

#include "webvttparser.h"  // NOLINT
#include <climits>

using std::string;

namespace libwebvtt {

enum {
  kNUL = '\x00',
  kSPACE = ' ',
  kTAB = '\x09',
  kLF = '\x0A',
  kCR = '\x0D'
};

Reader::Reader() {
}

Reader::~Reader() {
}

Parser::Parser(Reader* r)
  : reader_(r), unget_(-1) {
}

int Parser::Init() {
  int e = ParseBOM();

  if (e < 0)  // error
    return e;

  if (e > 0)  // EOF
    return -1;

  // Parse "WEBVTT".  We read from the stream one character at-a-time, in
  // order to defend against non-WebVTT streams (e.g. binary files) that don't
  // happen to comprise lines of text demarcated with line terminators.

  const char idstr[] = "WEBVTT";
  const char* p = idstr;

  while (*p) {
    char c;
    e = GetChar(&c);

    if (e < 0)  // error
      return e;

    if (e > 0)  // EOF
      return -1;

    if (c != *p)
      return -1;

    ++p;
  }

  string line;

  e = ParseLine(&line);

  if (e < 0)  // error
    return e;

  if (e > 0)  // EOF
    return 0;  // weird but valid

  if (!line.empty()) {
    // Parse optional characters that follow "WEBVTT"

    const char c = line[0];

    if (c != kSPACE && c != kTAB)
      return -1;
  }

  // The WebVTT spec requires that the "WEBVTT" line
  // be followed by an empty line (to separate it from
  // first cue).

  e = ParseLine(&line);

  if (e < 0)  // error
    return e;

  if (e > 0)  // EOF
    return 0;  // weird but we allow it

  if (!line.empty())
    return -1;

  return 0;  // success
}

int Parser::Parse(Cue* cue) {
  if (cue == NULL)
    return -1;

  // Parse first non-blank line

  string line;
  int e;

  for (;;) {
    e = ParseLine(&line);

    if (e)
      return e;

    if (!line.empty())
      break;
  }

  // A WebVTT cue comprises an optional cue identifier line followed
  // by a (non-optional) timings line.  You determine whether you have
  // a timings line by scanning for the arrow token, the lexeme of which
  // may not appear in the cue identifier line.

  string::size_type off = line.find("-->");

  if (off != string::npos) {  // timings line
    cue->identifier.clear();
  } else {
    cue->identifier.swap(line);

    e = ParseLine(&line);

    if (e)
      return e;

    off = line.find("-->");

    if (off == string::npos)  // not a timings line
      return -1;
  }

  e = ParseTimingsLine(line,
                       off,
                       &cue->start_time,
                       &cue->stop_time,
                       &cue->settings);

  if (e)
    return e;

  // The cue payload comprises all the non-empty
  // lines that follow the timings line.

  Cue::payload_t& p = cue->payload;
  p.clear();

  for (;;) {
    e = ParseLine(&line);

    if (e < 0)  // error
      return e;

    if (line.empty())
      break;

    p.push_back(line);
  }

  if (p.empty())
    return -1;

  return 0;  // success
}

int Parser::GetChar(char* c) {
  if (unget_ >= 0) {
    *c = static_cast<char>(unget_);
    unget_ = -1;
    return 0;
  }

  return reader_->GetChar(c);
}

void Parser::UngetChar(char c) {
  unget_ = static_cast<unsigned char>(c);
}

int Parser::ParseBOM() {
  // Explanation of UTF-8 BOM:
  // http://en.wikipedia.org/wiki/Byte_order_mark

  static const char BOM[] = "\xEF\xBB\xBF";  // UTF-8 BOM

  for (int i = 0; i < 3; ++i) {
    char c;
    int e = GetChar(&c);

    if (e < 0)  // error
      return e;

    if (e > 0)  // EOF
      return 1;

    if (c != BOM[i]) {
      if (i == 0) {  // we don't have a BOM
        UngetChar(c);
        return 0;  // success
      }

      // We started a BOM, so we must finish the BOM.
      return -1;  // error
    }
  }

  return 0;  // success
}

int Parser::ParseLineTerminator(char c) {
  // The WebVTT spec states that lines may be
  // terminated in any of these three ways:
  //  LF
  //  CR
  //  CR LF

  if (c == kLF)
    return 0;  // success

  if (c != kCR)
    return -1;  // error

  // We detected a CR.  We must interrogate the next character
  // in the stream, to determine whether we have a LF.

  int e = GetChar(&c);

  if (e < 0)  // error
    return e;

  if (e > 0)  // EOF
    return 0;  // success

  if (c == kLF)
    return 0;  // success

  // The next character in the stream is not a LF, so
  // return it to the stream; this completes this line.

  UngetChar(c);
  return 0;  // success
}

int Parser::ParseLine(string* line) {
  line->clear();

  for (;;) {
    char c;
    int e = GetChar(&c);

    if (e < 0)  // error
      return e;

    if (e > 0)  // EOF
      return (line->empty()) ? 1 : 0;

    if (c == kLF || c == kCR) {
      e = ParseLineTerminator(c);

      if (e < 0)  // error
        return e;

      return 0;
    }

    line->push_back(c);
  }
}

int Parser::ParseTimingsLine(
  string& line,
  string::size_type arrow_pos,
  Time* start_time,
  Time* stop_time,
  Cue::settings_t* settings) {
  //
  // Place a NUL character at the start of the arrow token, in
  // order to demarcate the start time from remainder of line.

  if (arrow_pos == string::npos || arrow_pos >= line.length())
    return -1;

  line[arrow_pos] = kNUL;
  string::size_type idx = 0;

  int e = ParseTime(line, idx, start_time);

  if (e)
    return e;

  // Detect any junk that follows the start time,
  // but precedes the arrow symbol.

  while (char c = line[idx]) {
    if (c != kSPACE && c != kTAB)
      return -1;
    ++idx;
  }

  // Place a NUL character at the end of the line,
  // so the scanner has a place to stop, and begin
  // the scan just beyond the arrow token.

  line.push_back(kNUL);
  idx = arrow_pos + 3;

  e = ParseTime(line, idx, stop_time);

  if (e)
    return e;

  e = ParseSettings(line, idx, settings);

  if (e)
    return e;

  return 0;  // success
}

int Parser::ParseTime(
  const string& line,
  string::size_type& idx,
  Time* time) {
  //
  // WebVTT timestamp syntax comes in three flavors:
  //  SS[.sss]
  //  MM:SS[.sss]
  //  HH:MM:SS[.sss]

  if (idx == string::npos || idx >= line.length())
    return -1;

  // Consume any whitespace that precedes the timestamp.

  while (char c = line[idx]) {
    if (c != kSPACE && c != kTAB)
      break;
    ++idx;
  }

  Time& t = *time;

  // Parse a generic number value.  We don't know which component
  // of the time we have yet, until we do more parsing.

  int val = ParseNumber(line, idx);

  if (val < 0)  // error
    return val;

  // The presence of a colon character indicates that we have
  // an [HH:]MM:SS style syntax.

  if (line[idx] == ':') {
    // We have either HH:MM:SS or MM:SS

    // The value we just parsed is either the hours or minutes.
    // It must be followed by another number value (that is
    // either minutes or seconds).

    const int first_val = val;

    ++idx;  // consume colon

    // Parse second value

    val = ParseNumber(line, idx);

    if (val < 0)
      return val;

    if (val >= 60)  // either MM or SS
      return -1;

    if (line[idx] == ':') {
      // We have HH:MM:SS

      t.hours = first_val;
      t.minutes = val;  // vetted above

      ++idx;  // consume MM:SS colon

      // We have parsed the hours and minutes.
      // We must now parse the seconds.

      val = ParseNumber(line, idx);

      if (val < 0)
        return val;

      if (val >= 60)  // SS part of HH:MM:SS
        return -1;

      t.seconds = val;
    } else {
      // We have MM:SS

      // The implication here is that the hour value was omitted
      // from the timestamp (because it was 0).

      if (first_val >= 60)  // minutes
        return -1;

      t.hours = 0;
      t.minutes = first_val;
      t.seconds = val;  // vetted above
    }
  } else {
    // We have SS (only)

    // The time is expressed as total number of seconds,
    // so the seconds value has no upper bound.

    t.seconds = val;

    // Convert SS to HH:MM:SS

    t.minutes = t.seconds / 60;
    t.seconds -= t.minutes * 60;

    t.hours = t.minutes / 60;
    t.minutes -= t.hours * 60;
  }

  // We have parsed the hours, minutes, and seconds.
  // We must now parse the milliseconds.

  if (line[idx] != '.') {  // no milliseconds
    t.milliseconds = 0;
  } else {
    ++idx;  // consume FULL STOP

    val = ParseNumber(line, idx);

    if (val < 0)
      return val;

    if (val >= 1000)
      return -1;

    if (val < 10)
      t.milliseconds = val * 100;
    else if (val < 100)
      t.milliseconds = val * 10;
    else
      t.milliseconds = val;
  }

  // We have parsed the time proper.  We must check for any
  // junk that immediately follows the time specifier.

  const char c = line[idx];

  if (c != kNUL && c != kSPACE && c != kTAB)
    return -1;

  return 0;  // success
}

int Parser::ParseSettings(
  const string& line,
  string::size_type idx,
  Cue::settings_t* settings) {
  //
  // Scanning starts at position idx, and stops when
  // we consume a NUL character.

  settings->clear();

  if (idx == string::npos || idx >= line.length())
    return -1;

  for (;;) {
    // Parse the whitespace that precedes the NAME:VALUE pair.

    for (;;) {
      const char c = line[idx];

      if (c == kNUL)
        return 0;  // success

      if (c != kSPACE && c != kTAB)
        break;

      ++idx;  // consume whitespace
    }

    // There is something on the line for us to scan.

    settings->push_back(Setting());
    Setting& s = settings->back();

    // Parse the NAME part of the settings pair.

    for (;;) {
      const char c = line[idx];

      if (c == ':')  // we have reached end of NAME part
        break;

      if (c == kNUL || c == kSPACE || c == kTAB)
        return -1;

      s.name.push_back(c);

      ++idx;
    }

    if (s.name.empty())
      return -1;

    ++idx;  // consume colon

    // Parse the VALUE part of the settings pair.

    for (;;) {
      const char c = line[idx];

      if (c == kNUL || c == kSPACE || c == kTAB)
        break;

      if (c == ':')  // suspicious when part of VALUE
        return -1;   // TODO(matthewjheaney): verify this behavior

      s.value.push_back(c);

      ++idx;
    }

    if (s.value.empty())
      return -1;
  }
}

int Parser::ParseNumber(const std::string& line,
                        std::string::size_type& idx) {
  if (idx == string::npos || idx >= line.length())
    return -1;

  if (!isdigit(line[idx]))
    return -1;

  long long val = 0;  // NOLINT

  while (isdigit(line[idx])) {
    val *= 10;
    val += static_cast<int>(line[idx] - '0');

    if (val > INT_MAX)
      return -1;

    ++idx;
  }

  return static_cast<int>(val);
}

bool Time::operator==(const Time& rhs) const {
  if (hours != rhs.hours)
    return false;

  if (minutes != rhs.minutes)
    return false;

  if (seconds != rhs.seconds)
    return false;

  return (milliseconds == rhs.milliseconds);
}

bool Time::operator<(const Time& rhs) const {
  if (hours < rhs.hours)
    return true;

  if (hours > rhs.hours)
    return false;

  if (minutes < rhs.minutes)
    return true;

  if (minutes > rhs.minutes)
    return false;

  if (seconds < rhs.seconds)
    return true;

  if (seconds > rhs.seconds)
    return false;

  return (milliseconds < rhs.milliseconds);
}

bool Time::operator>(const Time& rhs) const {
  return rhs.operator<(*this);
}

bool Time::operator<=(const Time& rhs) const {
  return !this->operator>(rhs);
}

bool Time::operator>=(const Time& rhs) const {
  return !this->operator<(rhs);
}

presentation_t Time::presentation() const {
  const presentation_t h = 1000LL * 3600LL * presentation_t(hours);
  const presentation_t m = 1000LL * 60LL * presentation_t(minutes);
  const presentation_t s = 1000LL * presentation_t(seconds);
  const presentation_t result = h + m + s + milliseconds;
  return result;
}

Time& Time::presentation(presentation_t d) {
  if (d < 0) {  // error
    hours = 0;
    minutes = 0;
    seconds = 0;
    milliseconds = 0;

    return *this;
  }

  seconds = d / 1000;
  milliseconds = d - 1000 * seconds;

  minutes = seconds / 60;
  seconds -= 60 * minutes;

  hours = minutes / 60;
  minutes -= 60 * hours;

  return *this;
}

Time& Time::operator+=(presentation_t rhs) {
  const presentation_t d = this->presentation();
  const presentation_t dd = d + rhs;
  this->presentation(dd);
  return *this;
}

Time Time::operator+(presentation_t d) const {
  Time t(*this);
  t += d;
  return t;
}

Time& Time::operator-=(presentation_t d) {
  return this->operator+=(-d);
}

presentation_t Time::operator-(const Time& t) const {
  const presentation_t rhs = t.presentation();
  const presentation_t lhs = this->presentation();
  const presentation_t result = lhs - rhs;
  return result;
}

}  // namespace libwebvtt