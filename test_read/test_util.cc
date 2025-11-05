#include "../tests/test_err_if.hh"
#include "../tests/test_should_be.hh"
#include "util.hh"

#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <sstream>
#include <cstdlib>
#include <string>
#include <sys/socket.h>
#include <thread>

using std::cerr;
using std::endl;
using std::exception;
using std::string;

static void test_timestamp_ms() {
    const uint64_t t1 = timestamp_ms();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    const uint64_t t2 = timestamp_ms();
    test_err_if(t2 < t1, "timestamp_ms should be monotonic non-decreasing");
}

static void test_system_call() {
    // Expect throw on error without mask
    bool threw = false;
    try {
        (void)SystemCall("recv", ::recv(-1, nullptr, 0, 0));
    } catch (const exception &) {
        threw = true;
    }
    test_should_be(threw, true);

    // Expect no throw when errno is masked (EBADF)
    const int rv = SystemCall("recv", ::recv(-1, nullptr, 0, 0), EBADF);
    test_should_be(rv < 0, true);
}

static uint16_t checksum_of(const string &data) {
    InternetChecksum ck;
    ck.add(std::string_view{data});
    return ck.value();
}

static void test_internet_checksum_basic() {
    // Empty data should yield 0xFFFF
    InternetChecksum ck_empty;
    test_should_be(ck_empty.value(), static_cast<uint16_t>(0xFFFF));

    // Known value for "abcd" (0x61 0x62 0x63 0x64)
    const string abcd = "abcd";
    test_should_be(checksum_of(abcd), static_cast<uint16_t>(0x3B39));
}

static void test_internet_checksum_verification_zero() {
    // Compute checksum over payload with checksum bytes zeroed, then
    // verify that including the checksum makes the result zero.
    string payload = "hello, checksum";  // arbitrary payload

    // Build a block with two zero bytes (checksum field) appended
    string with_zero_ck = payload;
    with_zero_ck.push_back('\0');
    with_zero_ck.push_back('\0');

    const uint16_t csum = checksum_of(with_zero_ck);

    // Now place the checksum into the last two bytes in network order
    string with_ck = payload;
    with_ck.push_back(static_cast<char>(csum >> 8));
    with_ck.push_back(static_cast<char>(csum & 0xFF));

    InternetChecksum verifier;
    verifier.add(std::string_view{with_ck});
    test_should_be(verifier.value(), static_cast<uint16_t>(0x0000));
}

static void test_hexdump_basic() {
    const string data = "ABC";  // 0x41 0x42 0x43

    // Capture cout
    std::ostringstream capture;
    std::streambuf *old_buf = std::cout.rdbuf(capture.rdbuf());

    hexdump(data.data(), data.size());

    // Restore cout
    std::cout.rdbuf(old_buf);

    const string out = capture.str();
    // Lightly assert key substrings to avoid brittleness
    test_err_if(out.find("00000000:") == string::npos, "hexdump should print offset column");
    test_err_if(out.find("41 42 43") == string::npos, "hexdump should print byte hex values");
    test_err_if(out.find(" ABC") == string::npos, "hexdump should print ASCII gutter");
}

int main() {
    try {
        test_timestamp_ms();
        test_system_call();
        test_internet_checksum_basic();
        test_internet_checksum_verification_zero();
        test_hexdump_basic();
    } catch (const exception &e) {
        cerr << "Exception: " << e.what() << endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
