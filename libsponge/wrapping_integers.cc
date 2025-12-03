#include "wrapping_integers.hh"
#include <cstdint>

// Dummy implementation of a 32-bit wrapping integer

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! Transform an "absolute" 64-bit sequence number (zero-indexed) into a WrappingInt32
//! \param n The input absolute 64-bit sequence number
//! \param isn The initial sequence number
WrappingInt32 wrap(uint64_t n, WrappingInt32 isn) {
    // n是64位绝对序号，转成uint32_t后 + ISN的原始值，溢出自动绕回
    return WrappingInt32{static_cast<uint32_t>(n) + isn.raw_value()};
}

//! Transform a WrappingInt32 into an "absolute" 64-bit sequence number (zero-indexed)
//! \param n The relative sequence number
//! \param isn The initial sequence number
//! \param checkpoint A recent absolute 64-bit sequence number
//! \returns the 64-bit sequence number that wraps to `n` and is closest to `checkpoint`
//!
//! \note Each of the two streams of the TCP connection has its own ISN. One stream
//! runs from the local TCPSender to the remote TCPReceiver and has one ISN,
//! and the other stream runs from the remote TCPSender to the local TCPReceiver and
//! has a different ISN.
// 32 位序列号会绕回（比如 seqno=0 可能对应绝对序号 0、2³²、2³³…），因此需要一个 “参考点（checkpoint）”——
// 上一次已知的绝对序号，找最接近这个 checkpoint 的绝对序号。
uint64_t unwrap(WrappingInt32 n, WrappingInt32 isn, uint64_t checkpoint) {
    // 计算当前序列号n与checkpoint_seq之间的“最小步长”
    int32_t min_step=n-wrap(checkpoint,isn);
    // 把步长加到checkpoint上，得到候选绝对序号
    int64_t ret=checkpoint+min_step;
    //如果候选序号为负（说明步长是反向绕回的），加上2³²修正为正
    return ret>=0?static_cast<uint64_t>(ret):ret+(1UL<<32);
}