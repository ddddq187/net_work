#include "byte_stream.hh"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>

// Dummy implementation of a flow-controlled in-memory byte stream.

// For Lab 0, please replace with a real implementation that passes the
// automated checks run by `make check_lab0`.

// You will need to add private members to the class declaration in `byte_stream.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}  // 占位符，编译不报错的作用

using namespace std;

// 源文件 byte_stream.cc 中构造函数的定义
ByteStream::ByteStream(size_t capacity)
    : _buffer()          // NOLINT(readability-redundant-member-init)
    ,                    // 显式默认构造 std::string
    _capacity(capacity)  // 初始化 const 成员
{}

size_t ByteStream::write(string_view data) {
    const size_t max_write = min(data.size(), remaining_capacity());
    if (max_write == 0) {
        return 0;
    }
    if (_buffer.size() - _start - _size < max_write) {
        // buffer.size是物理空间的大小，_start是逻辑起始位置，_size是有效数据长度
        // 如果物理空间的大小减去逻辑起始位置减去有效数据长度小于最大写入长度，则需要将逻辑起始位置移动到0
        _buffer.erase(0, _start);
        _start = 0;
    }
    _buffer.append(data.substr(0, max_write));
    _size += max_write;
    _bytes_written += max_write;
    return max_write;
}

//! \param[in] len bytes will be copied from the output side of the buffer
string_view ByteStream::peek_output(const size_t len) const {
    const size_t peek_len = min(len, _size);
    return {_buffer.data() + _start, peek_len};
    // return string_view(&buffer[_start], peek_len);
    //  std::string_view 可以通过 **“起始指针 + 长度”** 直接构造，无需拷贝数据（零拷贝）
    //  使用大括号初始化列表替代显式的类型构造，以简化代码并符合现代 C++ 风格
}

//! \param[in] len bytes will be removed from the output side of the buffer
void ByteStream::pop_output(const size_t len) {
    const size_t pop_len = min(len, _size);
    _start += pop_len;
    _size -= pop_len;
    _bytes_read += pop_len;
    if (_start > _capacity / 2) {
        _buffer.erase(0, _start);
        _start = 0;
    }
}

//! Read (i.e., copy and then pop) the next "len" bytes of the stream
//! \param[in] len bytes will be popped and returned
//! \returns a string
std::string ByteStream::read(const size_t len) {
    const auto view = peek_output(len);
    string result(view);
    pop_output(view.size());
    return result;
}

void ByteStream::end_input() { _input_ended = true; }

bool ByteStream::input_ended() const { return _input_ended; }

size_t ByteStream::buffer_size() const { return _size; }

bool ByteStream::buffer_empty() const { return _size == 0; }

bool ByteStream::eof() const { return buffer_empty() && _input_ended; }

size_t ByteStream::bytes_written() const { return _bytes_written; }

size_t ByteStream::bytes_read() const { return _bytes_read; }

size_t ByteStream::remaining_capacity() const { return _capacity - _size; }
