#include "tcp_sender.hh"

#include "tcp_config.hh"

#include <random>

// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(size_t capacity, uint16_t retx_timeout, std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{random_device()()}))
    , _segments_out()
    , _initial_retransmission_timeout{retx_timeout}
    , _stream(capacity)
    , _next_seqno{0}
    , _bytes_in_flight{0}
    , _outstanding_segments()
    , _retransmission_timeout{retx_timeout}
    , _timer_backoff_exp{0} {}

uint64_t TCPSender::bytes_in_flight() const { return _bytes_in_flight; }

// Helper: enqueue a segment for transmission and track it as outstanding
void TCPSender::send_and_track(const TCPSegment &seg) {
    // enqueue for the TCPConnection to actually send
    _segments_out.push(seg);

    // only track non-empty segments (those that consume sequence space)
    if (seg.length_in_sequence_space() == 0) {
        return;
    }

    _outstanding_segments.push(seg);
    _bytes_in_flight += seg.length_in_sequence_space();

    // (re)start retransmission timer if it was not already running
    if (!_timer_running) {
        _timer_running = true;
        _time_since_last_tick = 0;
    }
}

void TCPSender::fill_window() {
    // effective window treats a zero-sized window as one byte (the "persist" behavior)
    uint16_t effective_window = _receiver_window_size == 0 ? 1 : _receiver_window_size;

    // how many sequence numbers are currently in flight
    auto in_flight = static_cast<uint64_t>(_next_seqno - _last_ackno);

    // keep sending while there is room in the window
    while (in_flight < effective_window) {
        TCPSegment seg;
        TCPHeader &hdr = seg.header();

        // initialize sequence number of this segment
        hdr.seqno = wrap(_next_seqno, _isn);

        // First, if we haven't sent SYN yet, send a SYN segment
        if (!_syn_sent) {
            hdr.syn = true;
            _syn_sent = true;
        } else {
            // normal data/FIN segments

            // if stream buffer is empty and we either can't send FIN now or already sent it, stop
            if (_stream.buffer_size() == 0 && (!_stream.eof() || _fin_sent)) {
                break;
            }

            // remaining room in the window (sequence space)
            uint64_t window_remaining = effective_window - in_flight;
            if (window_remaining == 0) {
                break;
            }

            // maximum payload we are allowed to send in this segment
            size_t payload_limit = static_cast<size_t>(min<uint64_t>(TCPConfig::MAX_PAYLOAD_SIZE, window_remaining));

            // read from the ByteStream
            string data = _stream.read(payload_limit);
            seg.payload() = Buffer(std::move(data));

            // after sending payload, recompute remaining space to see if we can also send FIN
            window_remaining = effective_window - in_flight - seg.length_in_sequence_space();

            if (_stream.eof() && !_fin_sent && window_remaining > 0) {
                hdr.fin = true;
                _fin_sent = true;
            }

            // if the segment doesn't occupy any sequence space, don't send it
            if (seg.length_in_sequence_space() == 0) {
                break;
            }
        }

        // segment is ready to be sent
        send_and_track(seg);

        // advance next_seqno by length in sequence space
        _next_seqno += seg.length_in_sequence_space();
        in_flight = static_cast<uint64_t>(_next_seqno - _last_ackno);
    }
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    // we've now seen at least one ACK from the receiver
    _has_seen_ack = true;

    // update latest window advertisement
    _receiver_window_size = window_size;

    // compute absolute acknowledgment number relative to our ISN
    const uint64_t abs_ackno = unwrap(ackno, _isn, _next_seqno);

    // ignore impossible or old acknowledgments
    if (abs_ackno > _next_seqno || abs_ackno <= _last_ackno) {
        return;
    }

    _last_ackno = abs_ackno;

    // remove fully acknowledged segments from the outstanding queue
    bool any_newly_acked = false;
    while (!_outstanding_segments.empty()) {
        const TCPSegment &front = _outstanding_segments.front();
        const uint64_t seg_start = unwrap(front.header().seqno, _isn, _last_ackno);
        const uint64_t seg_end = seg_start + front.length_in_sequence_space();

        if (seg_end <= _last_ackno) {
            _bytes_in_flight -= front.length_in_sequence_space();
            _outstanding_segments.pop();
            any_newly_acked = true;
        } else {
            break;
        }
    }

    // if anything new was acknowledged, reset retransmission state
    if (any_newly_acked) {
        _retransmission_timeout = _initial_retransmission_timeout;
        _timer_backoff_exp = 0;
        _consecutive_retransmissions_cnt = 0;
        if (_bytes_in_flight == 0) {
            _timer_running = false;
            _time_since_last_tick = 0;
        } else {
            _timer_running = true;
            _time_since_last_tick = 0;
        }
    }
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) {
    if (!_timer_running || _bytes_in_flight == 0) {
        return;
    }

    _time_since_last_tick += ms_since_last_tick;

    // current RTO takes into account exponential backoff
    const unsigned int current_rto = _retransmission_timeout << _timer_backoff_exp;

    if (_time_since_last_tick >= current_rto && !_outstanding_segments.empty()) {
        // timer expired: retransmit the oldest outstanding segment
        const TCPSegment seg = _outstanding_segments.front();
        _segments_out.push(seg);

        // Are we in "zero-window probing" mode? (receiver has explicitly advertised a
        // zero-size window). In this mode, the RTO must not back off and the
        // consecutive retransmission count must not increase.
        const bool zero_window_mode = _has_seen_ack && (_receiver_window_size == 0);

        if (!zero_window_mode) {
            // exponential back-off of the retransmission timeout
            _timer_backoff_exp++;
            // count toward the "too many retransmissions" limit
            _consecutive_retransmissions_cnt++;
        }

        // restart timer
        _time_since_last_tick = 0;
    }
}

unsigned int TCPSender::consecutive_retransmissions() const { return _consecutive_retransmissions_cnt; }

void TCPSender::send_empty_segment() {
    TCPSegment seg;
    seg.header().seqno = wrap(_next_seqno, _isn);
    // empty payload, no flags -- does not consume sequence space and is not tracked
    _segments_out.push(seg);
}
