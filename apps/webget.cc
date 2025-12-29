#include "address.hh"
#include "tcp_sponge_socket.hh"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

using namespace std;

void get_URL(const string &host, const string &path) {
    // 连接到指定主机的 HTTP 服务，发送请求，并读取到 EOF 为止的全部响应。

    CS144TCPSocket tcp_sock{};                     // 创建一个 TCP 套接字
    tcp_sock.connect(Address(host, "http"));  // 解析主机 + "http"(80端口)，并建立 TCP 连接

    string request = "GET " + path + " HTTP/1.1\r\n";  // 请求行：方法、路径、协议版本，以 CRLF 结尾
    request += "Host: " + host + "\r\n";               // 必需的 Host 头（HTTP/1.1 规范要求）
    request += "Connection: close\r\n\r\n";            // 告诉服务端响应后关闭连接；空行分隔头与体
    tcp_sock.write(request);                           // 发送完整的 HTTP 请求报文

    tcp_sock.shutdown(SHUT_WR);  // 半关闭写方向，表示不再发送数据

    while (!tcp_sock.eof()) {     // 循环读取直到对端关闭（到达 EOF）
        cout << tcp_sock.read();  // 追加打印每次读取到的字节序列
    }

    tcp_sock.wait_until_closed();  // 等待 TCP 连接完全关闭
}

int main(int argc, char *argv[]) {
    try {
        if (argc <= 0) {
            abort();  // For sticklers: don't try to access argv[0] if argc <= 0.
        }

        // The program takes two command-line arguments: the hostname and "path" part of the URL.
        // Print the usage message unless there are these two arguments (plus the program name
        // itself, so arg count = 3 in total).
        if (argc != 3) {
            cerr << "Usage: " << argv[0] << " HOST PATH\n";
            cerr << "\tExample: " << argv[0] << " stanford.edu /class/cs144\n";
            return EXIT_FAILURE;
        }

        // Get the command-line arguments.
        const string host = argv[1];
        const string path = argv[2];

        // Call the student-written function.
        get_URL(host, path);
    } catch (const exception &e) {
        cerr << e.what() << "\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
