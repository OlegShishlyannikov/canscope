#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>

class BitStream {

public:
  uint8_t *toByteArray() { return m_data_; }

  void openBytes(uint8_t *bytes, size_t size) {
    m_data_ = bytes;
    m_size_ = size;
  }

  void open(size_t size) { m_data_ = reinterpret_cast<uint8_t *>(std::malloc(size)); }
  void close() { std::free(m_data_); }

  void write(size_t index, size_t bits_size, size_t data) {
    index += bits_size;
    while (data) {
      m_data_[index / UINT8_WIDTH] = chbit_(m_data_[index / UINT8_WIDTH], index % UINT8_WIDTH, data & 1u);
      data >>= 1u;
      index--;
    }
  }

  size_t read(size_t index, size_t bits_size) {
    size_t dat = 0;
    for (int32_t i = index; i < index + bits_size; i++) {
      dat = dat * 2 + getbit_(m_data_[i / 8], i % 8);
    }

    return dat;
  }

private:
  bool getbit_(char x, int y) { return (x >> (7 - y)) & 1; }
  size_t chbit_(size_t x, size_t i, bool v) {
    if (v) {
      return x | (1u << (7u - i));
    }
    
    return x & ~(1u << (7u - i));
  }

  uint8_t *m_data_;
  size_t m_size_;
};
