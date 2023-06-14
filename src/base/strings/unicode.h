// Copyright 2010-2021, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#ifndef MOZC_BASE_STRINGS_UNICODE_H_
#define MOZC_BASE_STRINGS_UNICODE_H_

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "base/strings/internal/utf8_internal.h"
#include "absl/base/attributes.h"
#include "absl/base/optimization.h"
#include "absl/strings/string_view.h"

namespace mozc {
namespace strings {

// The Unicode replacement character (U+FFFD) for ill-formed sequences.
using ::mozc::utf8_internal::kReplacementCharacter;

// Returns the byte length of a single UTF-8 character based on the leading
// byte.
//
// REQUIRES: The UTF-8 character is valid.
using ::mozc::utf8_internal::OneCharLen;

// Returns the byte length of a single UTF-8 character at the iterator. This
// overload participates in resolution only if InputIterator is not convertible
// to char.
//
// REQUIRES: The iterator is valid, points to the leading byte of the UTF-8
// character, and the value type is char.
template <typename InputIterator,
          typename std::enable_if_t<!std::is_convertible_v<InputIterator, char>,
                                    int> = 0>
constexpr uint8_t OneCharLen(const InputIterator it) {
  static_assert(
      std::is_same_v<typename std::iterator_traits<InputIterator>::value_type,
                     char>,
      "The iterator value_type must be char.");
  return OneCharLen(*it);
}

// Checks if the string is a valid UTF-8 string.
bool IsValidUtf8(absl::string_view sv);

// Returns the codepoint count of the given UTF-8 string indicated as [first,
// last) or a string_view.
//
// REQUIRES: The UTF-8 string must be valid. This implementation only sees the
// leading byte of each character and doesn't check if it's well-formed.
// Complexity: linear
template <typename InputIterator>
size_t CharsLen(InputIterator first, InputIterator last);
inline size_t CharsLen(const absl::string_view sv) {
  return CharsLen(sv.begin(), sv.end());
}

// Returns the number of Unicode characters between [0, n]. It stops counting at
// n. This is faster than CharsLen if you just want to check the length against
// certain thresholds.
//
// REQUIRES: The UTF-8 string must be valid. Same restrictions as CharsLen
// apply.
// Complexity: linear to min(n, CharsLen())
//
// Example:
//    const size_t len = AtLeastCharsLen(sv, 9);
//    if (len < 5) {
//      // len is shorter than 5
//    } else if (len < 9) {
//      // len is shorter than 9
//    }
template <typename InputIterator>
size_t AtLeastCharsLen(InputIterator first, InputIterator last, size_t n);
inline size_t AtLeastCharsLen(absl::string_view sv, size_t n) {
  return AtLeastCharsLen(sv.begin(), sv.end(), n);
}

// Returns <first char, rest> of the string.
// The result is clipped if the input string isn't long enough.
constexpr std::pair<absl::string_view, absl::string_view> FrontChar(
    absl::string_view s);

// Converts the UTF-8 string to UTF-32. ToUtf32 works correctly with
// ill-formed UTF-8 sequences. Unrecognized encodings are replaced with U+FFFD.
std::u32string Utf8ToUtf32(absl::string_view sv);

// Converts the UTF-32 string to UTF-8. If a code point is outside of the
// valid Unicode range [U+0000, U+10FFFF], it'll be replaced with U+FFFD.
std::string Utf32ToUtf8(std::u32string_view sv);

// Appends a single Unicode character represented by a char32_t code point to
// dest.
inline void StrAppendChar32(std::string* dest, const char32_t cp) {
  const utf8_internal::EncodeResult ec = utf8_internal::Encode(cp);
  // basic_string::append() is faster than absl::StrAppend() here.
  dest->append(ec.data(), ec.size());
}

}  // namespace strings

// Utf8CharIterator is an iterator adapter for a string-like iterator to iterate
// over each UTF-8 character.
// Note that the simple dereference returns the underlying StringIterator value.
// Use one of the member functions to access each character.
template <bool AsChar32>
class Utf8CharIterator {
 public:
  using difference_type =
      typename std::iterator_traits<const char*>::difference_type;
  using value_type = std::conditional_t<AsChar32, char32_t, absl::string_view>;
  using pointer = const value_type*;
  // The reference type can be non-reference for input iterators.
  using reference = value_type;
  using iterator_category = std::input_iterator_tag;

  // Constructs Utf8CharIterator at it for range [first, last). You also need to
  // pass the valid range of the underlying array to prevent any buffer overruns
  // because both operator++ and operator-- can move the StringIterator multiple
  // times in one call.
  Utf8CharIterator(const char* const first, const char* const last)
      : ptr_(first), last_(last) {
    Decode();
  }

  Utf8CharIterator(const Utf8CharIterator&) = default;
  Utf8CharIterator& operator=(const Utf8CharIterator&) = default;

  reference operator*() const {
    if constexpr (AsChar32) {
      return dr_.code_point();
    } else {
      return reference(ptr_, dr_.bytes_seen());
    }
  }

  // Moves the iterator to the next Unicode character.
  Utf8CharIterator& operator++() {
    ptr_ += dr_.bytes_seen();
    Decode();
    return *this;
  }
  Utf8CharIterator operator++(int) {
    Utf8CharIterator tmp(*this);
    ++*this;
    return tmp;
  }

  // Comparison operators.
  friend bool operator==(const Utf8CharIterator& lhs,
                         const Utf8CharIterator& rhs) {
    return lhs.ptr_ == rhs.ptr_;
  }
  friend bool operator!=(const Utf8CharIterator& lhs,
                         const Utf8CharIterator& rhs) {
    return !(lhs == rhs);
  }

 private:
  void Decode() {
    if (ptr_ != last_) {
      dr_ = utf8_internal::Decode(ptr_, last_);
    }
  }

  // TODO(yuryu): Use a contiguous iterator instead in C++20.
  const char* ptr_;
  const char* last_;
  utf8_internal::DecodeResult dr_;
};

// Utf8AsCharsBase is a wrapper to iterate over a UTF-8 string as a char32_t
// code point or an absl::string_view substring of each character. Use the
// aliases Utf8AsChars32 and Utf8AsChars.
//
// Note: Utf8AsCharsBase doesn't satisfy all items of the C++ Container
// requirement, but you can still use it with STL algorithms and ranged for
// loops. Specifically, it requires `size() == std::distance(begin(), end())`
// and that size() has constant time complexity. Since UTF-8 is a
// variable-length code, the constructor would need to precompute and store the
// size, however, this class will mostly be used to just iterate over each
// character once, so it'd be inefficient to iterate over the same string twice.
// Therefore, it doesn't have the size() member function.
template <bool AsChar32>
class Utf8AsCharsBase {
 public:
  using StringViewT = absl::string_view;
  using CharT = StringViewT::value_type;

  using value_type = std::conditional_t<AsChar32, char32_t, absl::string_view>;
  using reference = value_type&;
  using const_reference = const value_type&;
  using iterator = Utf8CharIterator<AsChar32>;
  using const_iterator = iterator;
  using difference_type = StringViewT::difference_type;
  using size_type = StringViewT::size_type;

  // Constructs an empty Utf8AsCharBase.
  Utf8AsCharsBase() = default;

  // Constructs Utf8AsCharBase with a string pointed by string_view.
  // Complexity: constant
  explicit Utf8AsCharsBase(const StringViewT sv) : sv_(sv) {}

  // Constructs Utf8AsCharBase of the first count characters in the array s.
  // Complexity: constant
  Utf8AsCharsBase(const CharT* s ABSL_ATTRIBUTE_LIFETIME_BOUND,
                  const size_type count)
      : sv_(s, count) {}

  // Constructs Utf8AsCharBase of the null-terminated string at the pointer.
  // Complexity: linear
  explicit Utf8AsCharsBase(const CharT* s ABSL_ATTRIBUTE_LIFETIME_BOUND)
      : sv_(s) {}

  // Construction from a null pointer is disallowed.
  Utf8AsCharsBase(std::nullptr_t) = delete;

  // Copyable.
  Utf8AsCharsBase(const Utf8AsCharsBase&) = default;
  Utf8AsCharsBase& operator=(const Utf8AsCharsBase&) = default;

  // Iterators.
  const_iterator begin() const { return const_iterator(sv_.data(), EndPtr()); }
  const_iterator end() const { return const_iterator(EndPtr(), EndPtr()); }
  const_iterator cbegin() const { return begin(); }
  const_iterator cend() const { return end(); }

  // Returns true if the string is empty.
  // Complexity: constant
  constexpr bool empty() const { return sv_.empty(); }

  // Returns the largest possible size in bytes.
  // Complexity: constant
  constexpr size_type max_size() const { return sv_.max_size(); }

  // Returns the first character.
  //
  // REQUIRES: !empty().
  // Complexity: constant
  value_type front() const { return *begin(); }

  // Returns the last character.
  //
  // REQUIRES: !empty().
  // Complexity: constant
  value_type back() const;

  constexpr void swap(Utf8AsCharsBase& other) noexcept { sv_.swap(other.sv_); }

  // Bitwise comparison operators that compare two Utf8AsCharBase using the
  // underlying string_view comparators.
  friend bool operator==(const Utf8AsCharsBase lhs, const Utf8AsCharsBase rhs) {
    return lhs.sv_ == rhs.sv_;
  }
  friend bool operator!=(const Utf8AsCharsBase lhs, const Utf8AsCharsBase rhs) {
    return lhs.sv_ != rhs.sv_;
  }
  friend bool operator<(const Utf8AsCharsBase lhs, const Utf8AsCharsBase rhs) {
    return lhs.sv_ < rhs.sv_;
  }
  friend bool operator<=(const Utf8AsCharsBase lhs, const Utf8AsCharsBase rhs) {
    return lhs.sv_ <= rhs.sv_;
  }
  friend bool operator>=(const Utf8AsCharsBase lhs, const Utf8AsCharsBase rhs) {
    return lhs.sv_ >= rhs.sv_;
  }
  friend bool operator>(const Utf8AsCharsBase lhs, const Utf8AsCharsBase rhs) {
    return lhs.sv_ > rhs.sv_;
  }

 private:
  const CharT* EndPtr() const { return sv_.data() + sv_.size(); }

  StringViewT sv_;
};

// Utf8AsChars32 is a wrapper to iterator a UTF-8 string over each character as
// char32_t code points.
// Characters with invalid encodings are replaced with U+FFFD.
//
// Example:
//  bool Func(const absl::string_view sv) {
//    for (const char32_t c : Utf8AsChars32(sv)) {
//      ...
//    }
//    ...
//  }
//
// Example:
//  const absl::string_view sv = ...;
//  std::u32string s32 = ...;
//  std::vector<char32_t> v;
//  absl::c_copy(Utf8AsChars32(sv), std::back_inserter(s32));
//  absl::c_copy(Utf8AsChars32(sv), std::back_inserter(v));
using Utf8AsChars32 = Utf8AsCharsBase<true>;

// Utf8AsChars is a wrapper to iterator a UTF-8 string over each character as
// substrings. Characters with invalid encodings are returned as they are.
//
// Example:
//  bool Func(const absl::string_view sv) {
//    for (const absl::string_view c : Utf8AsChars(sv)) {
//      ...
//    }
//    ...
//  }
//
// Example:
// const absl::string_view sv = ...;
// std::vector<absl::string_view> v;
// absl::c_copy(Utf8AsChars(sv), std::back_inserter(v));
using Utf8AsChars = Utf8AsCharsBase<false>;

// Implementations.
namespace strings {

template <typename InputIterator>
size_t CharsLen(InputIterator first, const InputIterator last) {
  size_t result = 0;
  while (first != last) {
    ++result;
    std::advance(first, OneCharLen(first));
  }
  return result;
}

template <typename InputIterator>
size_t AtLeastCharsLen(InputIterator first, const InputIterator last,
                       const size_t n) {
  size_t i = 0;
  while (first != last && i < n) {
    ++i;
    std::advance(first, OneCharLen(first));
  }
  return i;
}

constexpr std::pair<absl::string_view, absl::string_view> FrontChar(
    absl::string_view s) {
  if (s.empty()) {
    return {};
  }
  const uint8_t len = OneCharLen(s.front());
  return {absl::ClippedSubstr(s, 0, len), absl::ClippedSubstr(s, len)};
}

}  // namespace strings

template <bool AsChar32>
typename Utf8AsCharsBase<AsChar32>::value_type Utf8AsCharsBase<AsChar32>::back()
    const {
  const char* const last = EndPtr();
  if (sv_.back() <= 0x7f) {
    // ASCII
    if constexpr (AsChar32) {
      return sv_.back();
    } else {
      return value_type(last - 1, 1);
    }
  }
  // Other patterns. UTF-8 characters are at most four bytes long.
  // Check three bytes first as it's the most common pattern.
  // We still need to check one byte as it handles invalid sequences.
  for (const int size : {3, 2, 4, 1}) {
    if (size <= sv_.size()) {
      const char* const ptr = last - size;
      const utf8_internal::DecodeResult dr = utf8_internal::Decode(ptr, last);
      if (dr.bytes_seen() == size) {
        if constexpr (AsChar32) {
          return dr.code_point();
        } else {
          return value_type(ptr, dr.bytes_seen());
        }
      }
    }
  }
  ABSL_UNREACHABLE();
}

}  // namespace mozc

#endif  // MOZC_BASE_STRINGS_UNICODE_H_
