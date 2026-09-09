// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "iree/vm/bytecode/assembler_lexer.h"

#include <limits.h>
#include <string.h>

static bool iree_vm_bytecode_assembler_lexer_is_space(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static bool iree_vm_bytecode_assembler_lexer_is_name_start(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool iree_vm_bytecode_assembler_lexer_is_name_continue(char c) {
  return iree_vm_bytecode_assembler_lexer_is_name_start(c) ||
         (c >= '0' && c <= '9') || c == '.' || c == '$' || c == '-';
}

void iree_vm_bytecode_assembler_lexer_initialize(
    iree_string_view_t source, iree_vm_bytecode_assembler_lexer_t* out_lexer) {
  const char* data = source.data ? source.data : "";
  const iree_host_size_t size = source.data ? source.size : 0;
  out_lexer->source = iree_make_string_view(data, size);
  out_lexer->cursor = data;
  out_lexer->end = data + size;
}

iree_status_t iree_vm_bytecode_assembler_lexer_error(
    const iree_vm_bytecode_assembler_lexer_t* lexer, const char* message) {
  uint32_t line = 1;
  uint32_t column = 1;
  for (const char* p = lexer->source.data; p < lexer->cursor; ++p) {
    if (*p == '\n') {
      ++line;
      column = 1;
    } else {
      ++column;
    }
  }
  return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                          "VM assembly at %u:%u: %s", line, column, message);
}

void iree_vm_bytecode_assembler_lexer_skip_space(
    iree_vm_bytecode_assembler_lexer_t* lexer) {
  while (lexer->cursor < lexer->end &&
         iree_vm_bytecode_assembler_lexer_is_space(*lexer->cursor)) {
    ++lexer->cursor;
  }
}

bool iree_vm_bytecode_assembler_lexer_try_literal(
    iree_vm_bytecode_assembler_lexer_t* lexer, iree_string_view_t literal) {
  const char* cursor = lexer->cursor;
  if (literal.size == 0 ||
      !iree_vm_bytecode_assembler_lexer_is_space(literal.data[0])) {
    while (cursor < lexer->end &&
           iree_vm_bytecode_assembler_lexer_is_space(*cursor)) {
      ++cursor;
    }
  }
  iree_host_size_t i = 0;
  while (i < literal.size) {
    if (iree_vm_bytecode_assembler_lexer_is_space(literal.data[i])) {
      while (i < literal.size &&
             iree_vm_bytecode_assembler_lexer_is_space(literal.data[i])) {
        ++i;
      }
      if (cursor == lexer->end ||
          !iree_vm_bytecode_assembler_lexer_is_space(*cursor)) {
        return false;
      }
      while (cursor < lexer->end &&
             iree_vm_bytecode_assembler_lexer_is_space(*cursor)) {
        ++cursor;
      }
    } else if (cursor == lexer->end || *cursor++ != literal.data[i++]) {
      return false;
    }
  }
  lexer->cursor = cursor;
  return true;
}

bool iree_vm_bytecode_assembler_lexer_try_name(
    iree_vm_bytecode_assembler_lexer_t* lexer, iree_string_view_t* out_name) {
  const char* saved_cursor = lexer->cursor;
  iree_vm_bytecode_assembler_lexer_skip_space(lexer);
  const char* begin = lexer->cursor;
  if (begin == lexer->end ||
      !iree_vm_bytecode_assembler_lexer_is_name_start(*begin)) {
    lexer->cursor = saved_cursor;
    return false;
  }
  ++lexer->cursor;
  while (lexer->cursor < lexer->end &&
         iree_vm_bytecode_assembler_lexer_is_name_continue(*lexer->cursor)) {
    ++lexer->cursor;
  }
  *out_name = iree_make_string_view(begin, lexer->cursor - begin);
  return true;
}

iree_status_t iree_vm_bytecode_assembler_lexer_parse_name(
    iree_vm_bytecode_assembler_lexer_t* lexer, iree_string_view_t* out_name) {
  if (!iree_vm_bytecode_assembler_lexer_try_name(lexer, out_name)) {
    return iree_vm_bytecode_assembler_lexer_error(lexer,
                                                  "expected symbolic name");
  }
  return iree_ok_status();
}

iree_status_t iree_vm_bytecode_assembler_lexer_parse_unsigned(
    iree_vm_bytecode_assembler_lexer_t* lexer, uint8_t width,
    iree_vm_bytecode_assembler_radix_t radix, uint64_t* out_value) {
  iree_vm_bytecode_assembler_lexer_skip_space(lexer);
  const unsigned base =
      radix == IREE_VM_BYTECODE_ASSEMBLER_RADIX_HEXADECIMAL ? 16 : 10;
  if (base == 16) {
    if (lexer->end - lexer->cursor < 3 || lexer->cursor[0] != '0' ||
        (lexer->cursor[1] != 'x' && lexer->cursor[1] != 'X')) {
      return iree_vm_bytecode_assembler_lexer_error(
          lexer, "expected hexadecimal integer");
    }
    lexer->cursor += 2;
  }
  const char* begin = lexer->cursor;
  const uint64_t maximum =
      width == 8 ? UINT64_MAX : (UINT64_C(1) << (width * 8)) - 1;
  uint64_t value = 0;
  while (lexer->cursor < lexer->end) {
    const char c = *lexer->cursor;
    unsigned digit = UINT_MAX;
    if (c >= '0' && c <= '9') {
      digit = (unsigned)(c - '0');
    } else if (c >= 'a' && c <= 'f') {
      digit = (unsigned)(c - 'a') + 10;
    } else if (c >= 'A' && c <= 'F') {
      digit = (unsigned)(c - 'A') + 10;
    }
    if (digit >= base) break;
    if (value > (maximum - digit) / base) {
      return iree_vm_bytecode_assembler_lexer_error(
          lexer, "integer does not fit its field");
    }
    value = value * base + digit;
    ++lexer->cursor;
  }
  if (lexer->cursor == begin) {
    return iree_vm_bytecode_assembler_lexer_error(lexer,
                                                  "expected unsigned integer");
  }
  *out_value = value;
  return iree_ok_status();
}

iree_status_t iree_vm_bytecode_assembler_lexer_parse_signed(
    iree_vm_bytecode_assembler_lexer_t* lexer, uint8_t width,
    uint64_t* out_bits) {
  iree_vm_bytecode_assembler_lexer_skip_space(lexer);
  const bool is_negative = lexer->cursor < lexer->end && *lexer->cursor == '-';
  if (is_negative) ++lexer->cursor;
  const char* magnitude_begin = lexer->cursor;
  uint64_t magnitude = 0;
  IREE_RETURN_IF_ERROR(iree_vm_bytecode_assembler_lexer_parse_unsigned(
      lexer, 8, IREE_VM_BYTECODE_ASSEMBLER_RADIX_DECIMAL, &magnitude));
  if (is_negative && magnitude_begin < lexer->end &&
      iree_vm_bytecode_assembler_lexer_is_space(*magnitude_begin)) {
    return iree_vm_bytecode_assembler_lexer_error(
        lexer, "signed integer sign must be adjacent to its magnitude");
  }
  const uint8_t bit_count = width * 8;
  const uint64_t maximum_positive =
      bit_count == 64 ? INT64_MAX : (UINT64_C(1) << (bit_count - 1)) - 1;
  const uint64_t maximum_negative = maximum_positive + 1;
  if ((!is_negative && magnitude > maximum_positive) ||
      (is_negative && magnitude > maximum_negative)) {
    return iree_vm_bytecode_assembler_lexer_error(
        lexer, "signed integer does not fit its field");
  }
  const uint64_t mask =
      bit_count == 64 ? UINT64_MAX : (UINT64_C(1) << bit_count) - 1;
  *out_bits = is_negative ? ((~magnitude + 1) & mask) : magnitude;
  return iree_ok_status();
}

typedef struct iree_vm_bytecode_assembler_lexer_writer_t {
  // Optional fragment callback.
  iree_vm_bytecode_assembler_lexer_write_fn_t write_fn;
  // Unowned callback state.
  void* user_data;
  // Number of decoded bytes observed.
  uint64_t length;
  // Buffered decoded bytes.
  uint8_t buffer[256];
  // Number of live bytes in |buffer|.
  iree_host_size_t buffer_length;
} iree_vm_bytecode_assembler_lexer_writer_t;

static iree_status_t iree_vm_bytecode_assembler_lexer_writer_flush(
    iree_vm_bytecode_assembler_lexer_writer_t* writer) {
  if (!writer->write_fn || writer->buffer_length == 0) return iree_ok_status();
  iree_status_t status = writer->write_fn(
      writer->user_data,
      iree_make_const_byte_span(writer->buffer, writer->buffer_length));
  writer->buffer_length = 0;
  return status;
}

static iree_status_t iree_vm_bytecode_assembler_lexer_writer_append(
    iree_vm_bytecode_assembler_lexer_t* lexer,
    iree_vm_bytecode_assembler_lexer_writer_t* writer, uint8_t value) {
  if (writer->length == UINT64_MAX) {
    return iree_vm_bytecode_assembler_lexer_error(lexer,
                                                  "byte literal is too large");
  }
  ++writer->length;
  if (!writer->write_fn) return iree_ok_status();
  writer->buffer[writer->buffer_length++] = value;
  if (writer->buffer_length == IREE_ARRAYSIZE(writer->buffer)) {
    return iree_vm_bytecode_assembler_lexer_writer_flush(writer);
  }
  return iree_ok_status();
}

static int iree_vm_bytecode_assembler_lexer_hex_digit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static iree_status_t iree_vm_bytecode_assembler_lexer_parse_quoted_bytes_impl(
    iree_vm_bytecode_assembler_lexer_t* lexer, bool allow_nul,
    iree_vm_bytecode_assembler_lexer_write_fn_t write_fn, void* user_data,
    uint64_t* out_length) {
  iree_vm_bytecode_assembler_lexer_skip_space(lexer);
  if (lexer->cursor == lexer->end || *lexer->cursor++ != '"') {
    return iree_vm_bytecode_assembler_lexer_error(
        lexer, "expected quoted UTF-8 bytes");
  }
  iree_vm_bytecode_assembler_lexer_writer_t writer = {
      .write_fn = write_fn,
      .user_data = user_data,
  };
  while (lexer->cursor < lexer->end && *lexer->cursor != '"') {
    uint8_t value = (uint8_t)*lexer->cursor++;
    if (value == '\\') {
      if (lexer->cursor == lexer->end) {
        return iree_vm_bytecode_assembler_lexer_error(
            lexer, "unterminated escape sequence");
      }
      const char escape = *lexer->cursor++;
      if (escape == '\\' || escape == '"') {
        value = (uint8_t)escape;
      } else if (escape == 'n') {
        value = '\n';
      } else if (escape == 'r') {
        value = '\r';
      } else if (escape == 't') {
        value = '\t';
      } else if (escape == 'x') {
        if (lexer->end - lexer->cursor < 2) {
          return iree_vm_bytecode_assembler_lexer_error(
              lexer, "truncated hexadecimal escape");
        }
        const int high =
            iree_vm_bytecode_assembler_lexer_hex_digit(lexer->cursor[0]);
        const int low =
            iree_vm_bytecode_assembler_lexer_hex_digit(lexer->cursor[1]);
        if (high < 0 || low < 0) {
          return iree_vm_bytecode_assembler_lexer_error(
              lexer, "invalid hexadecimal escape");
        }
        value = (uint8_t)((high << 4) | low);
        lexer->cursor += 2;
        if ((value >= 0x20 && value != 0x7F) || (value == 0 && !allow_nul)) {
          return iree_vm_bytecode_assembler_lexer_error(
              lexer, "noncanonical hexadecimal escape");
        }
      } else {
        return iree_vm_bytecode_assembler_lexer_error(
            lexer, "unknown escape sequence");
      }
    } else if (value < 0x20 || value == 0x7F) {
      return iree_vm_bytecode_assembler_lexer_error(
          lexer, "control byte must use a canonical escape");
    }
    IREE_RETURN_IF_ERROR(
        iree_vm_bytecode_assembler_lexer_writer_append(lexer, &writer, value));
  }
  if (lexer->cursor == lexer->end) {
    return iree_vm_bytecode_assembler_lexer_error(lexer,
                                                  "unterminated quoted bytes");
  }
  ++lexer->cursor;
  IREE_RETURN_IF_ERROR(iree_vm_bytecode_assembler_lexer_writer_flush(&writer));
  *out_length = writer.length;
  return iree_ok_status();
}

iree_status_t iree_vm_bytecode_assembler_lexer_parse_quoted_string(
    iree_vm_bytecode_assembler_lexer_t* lexer,
    iree_vm_bytecode_assembler_lexer_write_fn_t write_fn, void* user_data,
    uint64_t* out_length) {
  return iree_vm_bytecode_assembler_lexer_parse_quoted_bytes_impl(
      lexer, false, write_fn, user_data, out_length);
}

iree_status_t iree_vm_bytecode_assembler_lexer_parse_quoted_bytes(
    iree_vm_bytecode_assembler_lexer_t* lexer,
    iree_vm_bytecode_assembler_lexer_write_fn_t write_fn, void* user_data,
    uint64_t* out_length) {
  return iree_vm_bytecode_assembler_lexer_parse_quoted_bytes_impl(
      lexer, true, write_fn, user_data, out_length);
}

iree_status_t iree_vm_bytecode_assembler_lexer_parse_hex_bytes(
    iree_vm_bytecode_assembler_lexer_t* lexer,
    iree_vm_bytecode_assembler_lexer_write_fn_t write_fn, void* user_data,
    uint64_t* out_length) {
  iree_vm_bytecode_assembler_lexer_skip_space(lexer);
  if (lexer->end - lexer->cursor < 4 ||
      memcmp(lexer->cursor, "hex\"", 4) != 0) {
    return iree_vm_bytecode_assembler_lexer_error(
        lexer, "expected hexadecimal byte span");
  }
  lexer->cursor += 4;
  iree_vm_bytecode_assembler_lexer_writer_t writer = {
      .write_fn = write_fn,
      .user_data = user_data,
  };
  while (lexer->cursor < lexer->end && *lexer->cursor != '"') {
    if (lexer->end - lexer->cursor < 2) {
      return iree_vm_bytecode_assembler_lexer_error(
          lexer, "truncated hexadecimal byte");
    }
    const int high =
        iree_vm_bytecode_assembler_lexer_hex_digit(lexer->cursor[0]);
    const int low =
        iree_vm_bytecode_assembler_lexer_hex_digit(lexer->cursor[1]);
    if (high < 0 || low < 0) {
      return iree_vm_bytecode_assembler_lexer_error(lexer,
                                                    "invalid hexadecimal byte");
    }
    lexer->cursor += 2;
    IREE_RETURN_IF_ERROR(iree_vm_bytecode_assembler_lexer_writer_append(
        lexer, &writer, (uint8_t)((high << 4) | low)));
  }
  if (lexer->cursor == lexer->end) {
    return iree_vm_bytecode_assembler_lexer_error(lexer,
                                                  "unterminated hex bytes");
  }
  ++lexer->cursor;
  IREE_RETURN_IF_ERROR(iree_vm_bytecode_assembler_lexer_writer_flush(&writer));
  *out_length = writer.length;
  return iree_ok_status();
}
