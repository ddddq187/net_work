#include "stream_reassembler.hh"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string>

using namespace std;

StreamReassembler::StreamReassembler(const size_t capacity)
    : _output(capacity), _capacity(capacity), _next_index(0), _eof_index(nullopt), _unassembled_segments() {}

void StreamReassembler::push_substring(const string &data,  // NOLINT(readability-function-cognitive-complexity)
                                       const size_t index,
                                       const bool eof) {
    // 1. 处理EOF标记：记录流结束位置
    if (eof) {
        _eof_index = static_cast<uint64_t>(index) + data.size();
    }

    const auto seg_start = static_cast<uint64_t>(index);
    const uint64_t seg_end = seg_start + data.size();

    // 2. 过滤已完全组装的段
    if (seg_end <= _next_index) {
        if (_eof_index.has_value() && _next_index >= _eof_index.value() && empty()) {
            _output.end_input();
        }
        return;
    }

    // 3. 计算剩余可用容量（总容量 - 已组装未读取的缓冲字节）
    const size_t buffer_used = _output.buffer_size();
    const size_t remaining_cap = _capacity - buffer_used;
    const uint64_t max_allowed_idx = _next_index + remaining_cap;

    // 4. 过滤超容量的段
    if (seg_start >= max_allowed_idx) {
        if (_eof_index.has_value() && _next_index >= _eof_index.value() && empty()) {
            _output.end_input();
        }
        return;
    }

    // 5. 确定当前段的有效范围（裁剪到容量内+未组装区域）
    const uint64_t valid_start = max(seg_start, _next_index);
    const uint64_t valid_end = min(seg_end, max_allowed_idx);
    const size_t valid_len = valid_end - valid_start;

    // 6. 提取有效数据（无有效数据则返回）
    if (valid_len == 0) {
        if (_eof_index.has_value() && _next_index >= _eof_index.value() && empty()) {
            _output.end_input();
        }
        return;
    }
    const string valid_data = data.substr(valid_start - seg_start, valid_len);

    // 7. 处理与已存储未组装段的重叠（前面的段）
    uint64_t new_seg_start = valid_start;
    string new_seg_data = valid_data;
    auto iter = _unassembled_segments.lower_bound(new_seg_start);

    // 检查前一个段是否重叠
    if (iter != _unassembled_segments.begin()) {
        auto prev_iter = prev(iter);
        const uint64_t prev_seg_end = prev_iter->first + prev_iter->second.size();
        if (prev_seg_end > new_seg_start) {
            // 前一个段覆盖当前段的前半部分：裁剪当前段
            const size_t overlap_len = prev_seg_end - new_seg_start;
            if (overlap_len >= new_seg_data.size()) {
                // 当前段完全被覆盖，直接返回
                if (_eof_index.has_value() && _next_index >= _eof_index.value() && empty()) {
                    _output.end_input();
                }
                return;
            }
            new_seg_data = new_seg_data.substr(overlap_len);
            new_seg_start = prev_seg_end;
        }
    }

    // 8. 处理与已存储未组装段的重叠（后面的段）
    while (iter != _unassembled_segments.end() && iter->first < new_seg_start + new_seg_data.size()) {
        const uint64_t curr_iter_start = iter->first;
        const uint64_t curr_iter_end = curr_iter_start + iter->second.size();

        if (curr_iter_start < new_seg_start + new_seg_data.size()) {
            // 当前段覆盖后面段的前半部分：删除被覆盖的段
            if (curr_iter_end <= new_seg_start + new_seg_data.size()) {
                iter = _unassembled_segments.erase(iter);
            } else {
                // 后面段覆盖当前段的后半部分：裁剪当前段
                const size_t keep_len = curr_iter_start - new_seg_start;
                new_seg_data = new_seg_data.substr(0, keep_len);
                break;
            }
        } else {
            break;
        }
    }

    // 9. 插入处理后的有效段到未组装集合
    if (!new_seg_data.empty()) {
        _unassembled_segments[new_seg_start] = new_seg_data;
    }

    // 10. 尝试将有序段写入输出流
    while (!_unassembled_segments.empty()) {
        auto first_iter = _unassembled_segments.begin();
        const uint64_t first_seg_start = first_iter->first;

        // 第一个未组装段不是下一个需要的段：退出循环
        if (first_seg_start != _next_index) {
            break;
        }

        // 写入输出流
        const string &seg_data = first_iter->second;
        const size_t written_len = _output.write(seg_data);
        _next_index += written_len;

        // 删除已写入的段
        _unassembled_segments.erase(first_iter);

        // 输出流已满，剩余数据重新插入
        if (written_len < seg_data.size()) {
            _unassembled_segments[_next_index] = seg_data.substr(written_len);
            break;
        }
    }

    // 11. 检查是否所有数据已组装完成：结束输出流
    if (_eof_index.has_value() && _next_index >= _eof_index.value() && empty()) {
        _output.end_input();
    }
}

size_t StreamReassembler::unassembled_bytes() const {
    size_t total = 0;
    for (const auto &seg : _unassembled_segments) {
        total += seg.second.size();
    }
    return total;
}

bool StreamReassembler::empty() const { return _unassembled_segments.empty(); }