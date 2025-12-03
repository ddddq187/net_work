#include "tcp_receiver.hh"

#include "tcp_header.hh"

#include <cstdint>
#include <optional>

using namespace std;

void TCPReceiver::segment_received(const TCPSegment &seg) {
    const TCPHeader &hdr = seg.header();
    const string &data = seg.payload().copy();//调用buffer类的copy函数

    // 如果没有收到 SYN，忽略所有段（除了 SYN 段）
    if (!_isn.has_value()) {//isn是初始序列号，标志连接是否建立
        if (!hdr.syn) {//tcp头部的syn'标志,是否是连接请求的syn段
            return;
        }
        _isn = hdr.seqno;//是syn段就把序列号赋值给_isn
    }

    // 计算 checkpoint：下一个期望接收的绝对序列号
    // checkpoint = 已写入的字节数 + 1（SYN 占一个序列号）
    uint64_t checkpoint = stream_out().bytes_written() + 1;

    // 将相对序列号转换为绝对序列号（TCP 序列空间，从 SYN 自身开始计数）
    uint64_t abs_seq = unwrap(hdr.seqno, _isn.value(), checkpoint);

    // 计算在字节流中的下标：
    //  - 字节流是从第一个数据字节开始按 0 计数
    //  - SYN 本身占用一个序列号，但不对应任何数据字节
    // 结论：流下标 = abs_seq - 1 + (hdr.syn ? 1 : 0)
    //   * 仅 SYN: abs_seq = 0, hdr.syn = 1 -> index = 0（但 data 为空，不写入）
    //   * SYN 后的第一个数据段: abs_seq = 1, hdr.syn = 0 -> index = 0
    uint64_t stream_idx = abs_seq + (hdr.syn ? 1 : 0) - 1;

    // 将数据推送到重组器，FIN 标志作为 eof 参数
    _reassembler.push_substring(data, stream_idx, hdr.fin);
}

optional<WrappingInt32> TCPReceiver::ackno() const {
    // 如果没有收到 SYN，返回空
    if (!_isn.has_value()) {
        return {};
    }

    // 计算已接收的字节数
    uint64_t bytes_received = stream_out().bytes_written();

    // ackno 的绝对序列号 = 已接收字节数 + 1（SYN 占一个序列号）
    // 如果流已结束（收到 FIN），还需要再加 1（FIN 占一个序列号）
    uint64_t abs_ackno = bytes_received + 1;
    if (stream_out().input_ended()) {
        abs_ackno += 1;
    }

    // 将绝对序列号转换为相对序列号（WrappingInt32）
    return wrap(abs_ackno, _isn.value());
}

size_t TCPReceiver::window_size() const {
    // 窗口大小 = 总容量 - 已重组但未消费的字节数
    return _capacity - stream_out().buffer_size();
}
