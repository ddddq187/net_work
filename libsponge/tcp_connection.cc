#include "tcp_connection.hh"

#include <iostream>

using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const {
    // 返回 sender 的剩余容量
    return _sender.stream_in().remaining_capacity();
}

size_t TCPConnection::bytes_in_flight() const {
    // 返回 sender 中正在传输的字节数
    return _sender.bytes_in_flight();
}

size_t TCPConnection::unassembled_bytes() const {
    // 返回 receiver 中未组装的字节数
    return _receiver.unassembled_bytes();
}

size_t TCPConnection::time_since_last_segment_received() const {
    // 返回自上次收到段以来的时间
    return _time_since_last_segment_received;
}

void TCPConnection::segment_received(const TCPSegment &seg) {
    // 如果连接已被重置，忽略所有段
    if (_is_reset) {
        return;
    }

    // 更新收到段的时间
    _time_since_last_segment_received = 0;

    // 如果收到 RST 标志，重置连接
    if (seg.header().rst) {
        _is_reset = true;
        _sender.stream_in().set_error();
        _receiver.stream_out().set_error();
        return;
    }

    // 在 LISTEN 状态（receiver 没有 ackno）收到 ACK（没有 SYN），应该忽略
    if (seg.header().ack && !_receiver.ackno().has_value() && !seg.header().syn) {
        // 忽略 ACK，不发送 RST（根据 relaxed 测试的要求）
        return;
    }

    // 将段传递给 receiver 处理
    _receiver.segment_received(seg);

    // 如果收到 FIN，检查是否是被动关闭（CLOSE_WAIT 状态）
    // 被动关闭：receiver 收到 FIN，但 sender 还没有发送 FIN
    if (seg.header().fin && _receiver.stream_out().input_ended() && !_sender.stream_in().eof()) {
        // 被动关闭，设置 linger_after_streams_finish = false
        _linger_after_streams_finish = false;
    }

    // 如果收到 ACK，更新 sender 的确认
    if (seg.header().ack) {
        _sender.ack_received(seg.header().ackno, seg.header().win);
        // 窗口可能更新了，尝试填充窗口发送更多数据
        _sender.fill_window();
    }

    // 处理 SYN 的情况
    if (seg.header().syn) {
        // 情况1：被动连接（LISTEN 状态），收到 SYN，需要发送 SYN-ACK
        if (_receiver.ackno().has_value() && _sender.next_seqno_absolute() == 0) {
            // 在 LISTEN 状态收到 SYN，需要发送 SYN-ACK
            _sender.fill_window();
        }
        // 情况2：主动连接（SYN_SENT 状态），收到 SYN（没有 ACK），需要发送 ACK
        // 注意：如果收到 SYN-ACK，上面的 ack_received 已经处理了 ACK
        else if (!seg.header().ack && _sender.next_seqno_absolute() > 0) {
            // 主动连接收到 SYN，需要发送 ACK
            _sender.send_empty_segment();
        }
    }

    // 发送 sender 产生的段（如果有）
    while (!_sender.segments_out().empty()) {
        TCPSegment seg_to_send = _sender.segments_out().front();
        _sender.segments_out().pop();
        send_segment(seg_to_send);
        _segments_out.push(seg_to_send);
    }

    // 如果 receiver 有 ackno，且我们收到了需要确认的段，需要发送 ACK
    if (_receiver.ackno().has_value() && _sender.segments_out().empty()) {
        bool need_ack = false;

        // 情况1：收到 SYN-ACK（SYN 和 ACK 都设置），且 sender 已经发送了 SYN
        if (seg.header().syn && seg.header().ack && _sender.next_seqno_absolute() > 0) {
            need_ack = true;
        }
        // 情况2：收到数据或 FIN，需要发送 ACK
        else if ((seg.payload().size() > 0 || seg.header().fin) && _sender.next_seqno_absolute() > 0) {
            need_ack = true;
        }
        // 情况3：收到 FIN+ACK，需要发送 ACK
        else if (seg.header().fin && seg.header().ack && _sender.next_seqno_absolute() > 0) {
            need_ack = true;
        }

        if (need_ack) {
            _sender.send_empty_segment();
            TCPSegment empty_seg = _sender.segments_out().front();
            _sender.segments_out().pop();
            send_segment(empty_seg);
            _segments_out.push(empty_seg);
        }
    }
}

bool TCPConnection::active() const {
    // 如果连接已被重置，不活跃
    if (_is_reset) {
        return false;
    }

    // 如果 sender 的流还在运行（未结束或未完全确认），连接活跃
    if (!_sender.stream_in().eof() || _sender.bytes_in_flight() > 0) {
        return true;
    }

    // 如果 receiver 的流还在运行，连接活跃
    if (!_receiver.stream_out().eof()) {
        return true;
    }

    // 如果两个流都已结束，检查是否需要 linger
    // 如果启用了 linger，在 linger 期间保持活跃
    if (_linger_after_streams_finish) {
        return true;
    }

    return false;
}

size_t TCPConnection::write(const string &data) {
    // 将数据写入 sender 的流
    size_t bytes_written = _sender.stream_in().write(data);
    // 尝试填充窗口发送数据
    _sender.fill_window();
    // 将 sender 产生的段发送出去
    while (!_sender.segments_out().empty()) {
        TCPSegment seg = _sender.segments_out().front();
        _sender.segments_out().pop();
        send_segment(seg);
        _segments_out.push(seg);
    }
    return bytes_written;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
    // 更新自上次收到段以来的时间
    _time_since_last_segment_received += ms_since_last_tick;

    // 如果连接已被重置，不需要做任何事情
    if (_is_reset) {
        return;
    }

    // 调用 sender 的 tick 来处理重传
    _sender.tick(ms_since_last_tick);

    // 检查是否超过最大重传次数，如果是则重置连接并发送 RST
    if (_sender.consecutive_retransmissions() > TCPConfig::MAX_RETX_ATTEMPTS) {
        _is_reset = true;
        _sender.stream_in().set_error();
        _receiver.stream_out().set_error();
        // 发送 RST 段
        TCPSegment rst_seg;
        rst_seg.header().rst = true;
        rst_seg.header().seqno = _sender.next_seqno();
        if (_receiver.ackno().has_value()) {
            rst_seg.header().ack = true;
            rst_seg.header().ackno = _receiver.ackno().value();
        }
        send_segment(rst_seg);
        _segments_out.push(rst_seg);
        return;
    }

    // 将 sender 产生的重传段发送出去
    while (!_sender.segments_out().empty()) {
        TCPSegment seg = _sender.segments_out().front();
        _sender.segments_out().pop();
        send_segment(seg);
        _segments_out.push(seg);
    }

    // 处理 linger 超时：如果两个流都已结束，且没有未确认的字节，且 linger 时间已过
    if (_sender.stream_in().eof() && _receiver.stream_out().eof() && _sender.bytes_in_flight() == 0) {
        // linger 时间为 10 * RTO
        if (_linger_after_streams_finish && _time_since_last_segment_received >= 10 * _cfg.rt_timeout) {
            // linger 时间已过，可以关闭连接
            _linger_after_streams_finish = false;
        }
    }
}

void TCPConnection::end_input_stream() {
    // 结束 sender 的输入流
    _sender.stream_in().end_input();
    // 尝试发送 FIN（通过 fill_window）
    _sender.fill_window();
    // 将 sender 产生的段发送出去
    while (!_sender.segments_out().empty()) {
        TCPSegment seg = _sender.segments_out().front();
        _sender.segments_out().pop();
        send_segment(seg);
        _segments_out.push(seg);
    }
}

void TCPConnection::connect() {
    // 发起连接：调用 sender 的 fill_window 发送 SYN
    _sender.fill_window();
    // 将 sender 产生的 SYN 段发送出去
    while (!_sender.segments_out().empty()) {
        TCPSegment seg = _sender.segments_out().front();
        _sender.segments_out().pop();
        send_segment(seg);
        _segments_out.push(seg);
    }
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";

            // 发送 RST 段
            TCPSegment rst_seg;
            rst_seg.header().rst = true;
            rst_seg.header().seqno = _sender.next_seqno();
            // 如果 receiver 有 ackno，设置 ackno
            if (_receiver.ackno().has_value()) {
                rst_seg.header().ack = true;
                rst_seg.header().ackno = _receiver.ackno().value();
            }
            send_segment(rst_seg);
            _segments_out.push(rst_seg);
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}

// 辅助函数：发送段，填充 receiver 的 ackno 和 window size
void TCPConnection::send_segment(TCPSegment &seg) {
    // 如果 receiver 有 ackno，设置 ack 标志和 ackno
    if (_receiver.ackno().has_value()) {
        seg.header().ack = true;
        seg.header().ackno = _receiver.ackno().value();
    }
    // 设置 window size（限制在 UINT16_MAX）
    size_t win_size = _receiver.window_size();
    seg.header().win = static_cast<uint16_t>(min(win_size, static_cast<size_t>(UINT16_MAX)));
}
