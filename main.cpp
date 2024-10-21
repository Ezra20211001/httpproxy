

#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS


#include <list>
#include <thread>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <WinSock2.h>
#include <ws2ipdef.h>
#include <WS2tcpip.h>
#include <Windows.h>
#pragma comment(lib, "ws2_32.lib")

#define LOG(fmt, ...) printf("[%s %s] [%s] " fmt "\n" , __DATE__,__TIME__, __FUNCTION__, ##__VA_ARGS__);

#define BREAK_IF(expr, fmt, ...)                    \
    if (expr)                                       \
    {                                               \
        printf("[%s %s] [%s] " fmt ": %d\n" ,       \
                __DATE__,__TIME__, __FUNCTION__,    \
                ##__VA_ARGS__, WSAGetLastError());  \
        break;                                      \
    }

inline bool set_sock_nob(SOCKET _s)
{
    u_long opt = 1;
    return ioctlsocket(_s, FIONBIO, &opt) != SOCKET_ERROR;
}

inline bool valid_socket(SOCKET _s)
{
    return INVALID_SOCKET != _s;
}

inline void close_socket(SOCKET& _s)
{
    if (valid_socket(_s))
    {
        closesocket(_s);
        _s = INVALID_SOCKET;
    }
}

inline bool read_line(const char* _src, int _slen, char* _dst, int _dlen, int &_nread)
{
    _nread = 0;
    bool success = false;
    for (int i = 0; i < _slen; ++i)
    {
        _dst[i] = _src[i];
        if (_dst[i] == '\n')
        {
            _nread = i;
            _dst[i + 1] = '\0';
            success = true;
            break;
        }
    }

    return success;
}

bool read_header(const char* _src, int _slen, char* _dst, int _dlen, int &_nread)
{
    const char* p = _src;
    int left = _slen;
    bool success = false;

    while (true)
    {
        char line[1024] = { 0 };
        int nread = 0;
        if (read_line(p, left, line, sizeof(line), nread))
        {
            p += nread;
            left -= nread;
            memcpy(_dst, line, nread);
        }
        else
        {
            break;
        }

        //读到了空行，http头结束
        if (strcmp(line, "\r\n") == 0 || strcmp(line, "\n") == 0)
        {
            success = true;
            break;
        }
    }

    _nread = _slen - left;
    return success;
}

bool extract_host(char* _header, char (&_host)[128], int &_port)
{
    char* _p = strstr(_header, "CONNECT");  /* 在 CONNECT 方法中解析 隧道主机名称及端口号 */
    if (_p)
    {
        char* _p1 = strchr(_p, ' ');
        char* _p2 = strchr(_p1 + 1, ':');
        char* _p3 = strchr(_p1 + 1, ' ');

        if (_p2)
        {
            char sport[32] = { 0 };
            strncpy(_host, _p1 + 1, (int)(_p2 - _p1) - 1);
            strncpy(sport, _p2 + 1, (int)(_p3 - _p2) - 1);
            _port = atoi(sport);
        }
        else
        {
            strncpy(_host, _p1 + 1, (int)(_p3 - _p1) - 1);
            _port = 80;
        }
    }
    else
    {
        char* p = strstr(_header, "Host:");
        if (!p)
        {
            return false;
        }

        char* p1 = strchr(p, '\n');
        if (!p1)
        {
            return false;
        }

        char* p2 = strchr(p + 5, ':'); /* 5是指'Host:'的长度 */
        if (p2 && p2 < p1)
        {
            char sport[32] = { 0 };
            strncpy(sport, p2 + 1, (int)(p1 - p2 - 1));
            _port = atoi(sport);
            strncpy(_host, p + 5 + 1, (int)(p2 - p - 5 - 1)); //Host:
        }
        else
        {
            strncpy(_host, p + 5 + 1, (int)(p1 - p - 5 - 1 - 1));
            _port = 80;
        }
    }

    return true;
}

/* 代理中的完整URL转发前需改成 path 的形式 */
void rewrite_header(char* _header, int _len)
{
    char* p = strstr(_header, "http://");
    char* p0 = strchr(p, '\0');
    char* p5 = strstr(_header, "HTTP/"); /* "HTTP/" 是协议标识 如 "HTTP/1.1" */
    if (p)
    {
        char* p1 = strchr(p + 7, '/');
        if (p1 && (p5 > p1))
        {
            //转换url到 path
            memcpy(p, p1, (int)(p0 - p1));
            int l = _len - (p1 - p);
            _header[l] = '\0';
        }
        else
        {
            char* p2 = strchr(p, ' ');  //GET http://3g.sina.com.cn HTTP/1.1
            memcpy(p + 1, p2, (int)(p0 - p2));
            *p = '/';  //url 没有路径使用根
            int l = _len - (p2 - p) + 1;
            _header[l] = '\0';
        }
    }
}

SOCKET create_connection(const char *_host, int _port)
{
    SOCKET sock = INVALID_SOCKET;
    bool success = false;

    do
    {
        sock = socket(AF_INET, SOCK_STREAM, 0);
        BREAK_IF(!valid_socket(sock), "socket failed");

        hostent*  host = gethostbyname(_host);
        BREAK_IF(host == NULL, "gethostbyname[%s] failed", _host);

        sockaddr_in saddr;
        memset(&saddr, 0, sizeof(saddr));
        saddr.sin_family = AF_INET;
        memcpy(&saddr.sin_addr.s_addr, host->h_addr, host->h_length);
        saddr.sin_port = htons(_port);

        char ip[INET_ADDRSTRLEN] = { 0 }; // 足够大以存储IPv4地址的字符串形式
        inet_ntop(AF_INET, &(saddr.sin_addr), ip, INET_ADDRSTRLEN);
        LOG("host[%s]<====>ip[%s], port[%d]", _host, ip, _port);

        BREAK_IF(connect(sock, (sockaddr*)&saddr, sizeof(saddr)) < 0, "connect[%s:%d] failed", ip, _port);
        success = true;
    } while (0);

    if (!success)
    {
        close_socket(sock);
    }

    return sock;
}

SOCKET create_listener(const char *_ip, int _port)
{
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(_ip);
    addr.sin_port = htons(_port);

    SOCKET ssock = INVALID_SOCKET;
    bool success = false;

    do
    {
        ssock = socket(AF_INET, SOCK_STREAM, 0);
        BREAK_IF(!valid_socket(ssock), "socket err");
        BREAK_IF(bind(ssock, (sockaddr*)&addr, sizeof(addr)) < 0, "bind err");
        BREAK_IF(listen(ssock, 5) < 0, "listen err");
        success = true;
    } while (0);

    if (!success)
    {
        close_socket(ssock);
    }

    return ssock;
}


inline bool tunnel_establishd(SOCKET _to)
{
    const char resp[] = "HTTP/1.1 200 Connection Established\r\n\r\n";
    if (send(_to, resp, sizeof(resp) - 1, 0) < 0)
    {
        LOG("Send http tunnel response failed");
        return false;
    }
    return true;
}

inline bool forward_data(char* _data, int _len, SOCKET _to)
{
    char *p = _data;
    while (_len > 0)
    {
        int n = send(_to, p, _len, 0);
        if (n > 0)
        {
            p += n;
            _len -= n;
        }
        else
        {
            if (n < 0)
            {
                int err = WSAGetLastError();
                if (err == EINTR || err == EWOULDBLOCK)
                {
                    continue;
                }
            }
            return false;
        }
    }
    return true;
}

#define SHUTDOWN_ASSERT(expr)   \
if (!(expr)) {                  \
    shutdown(remote, SD_BOTH);  \
    shutdown(remote, SD_BOTH);  \
    break;                      \
}

void proxy(SOCKET _local)
{
    SOCKET remote = INVALID_SOCKET;
    SOCKET local = _local;
    set_sock_nob(local);

    const int BUF_LEN = 4096;
    char *buff = new char[BUF_LEN];
    int datalen = 0;

    while (true)
    {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(local, &readfds);
        if (valid_socket(remote))
        {
            FD_SET(remote, &readfds);
        }

        int nfds = select(0, &readfds, nullptr, nullptr, nullptr);
        if (nfds <= 0)
        {
            LOG("select err: %d\n", GetLastError());
            break;
        }

        if (FD_ISSET(local, &readfds))
        {
            char* p = buff + datalen;
            int len = BUF_LEN - datalen;
            int n = recv(local, p, len, 0);
            SHUTDOWN_ASSERT(n > 0);
            datalen += n;

            if (INVALID_SOCKET == remote)
            {
                char header[2048] = { 0 };
                int nread = 0;
                if (!read_header(buff, datalen, header, sizeof(header), nread))
                {
                    continue;
                }

                char host[128] = { 0 };
                int port = 0;
                SHUTDOWN_ASSERT(extract_host(header, host, port));
                SHUTDOWN_ASSERT((remote = create_connection(host, port)) != INVALID_SOCKET);
                set_sock_nob(remote);

                if (strstr(header, "CONNECT"))
                {
                    SHUTDOWN_ASSERT(tunnel_establishd(local));
                }
                else
                {
                    rewrite_header(p, nread);
                    SHUTDOWN_ASSERT(forward_data(p, datalen, remote));
                }
            }
            else
            {
                SHUTDOWN_ASSERT(forward_data(p, datalen, remote));
            }
            datalen = 0;
        }

        if (FD_ISSET(remote, &readfds))
        {
            char tmp[2048];
            int n = recv(remote, tmp, sizeof(tmp), 0);
            SHUTDOWN_ASSERT(n > 0);
            SHUTDOWN_ASSERT(forward_data(tmp, n, local));
        }
    }

    close_socket(local);
    close_socket(remote);
    delete[] buff;
}

const char* const ADDR = "127.0.0.1";
const int PORT = 7890;
int main(int argc, char* argv[])
{
    WSADATA wsadata;
    if (WSAStartup(MAKEWORD(2, 2), &wsadata) < 0)
    {
        return 0;
    }

    SOCKET ssock = create_listener(ADDR, PORT);
    if (ssock != INVALID_SOCKET)
    {
        while (true)
        {
            sockaddr_in addr;
            int len = sizeof(addr);
            SOCKET fd = accept(ssock, (sockaddr*)&addr, &len);
            std::thread(proxy, fd).detach();
        }

        close_socket(ssock);
    }

    WSACleanup();
    system("pause");
    return 0;
}
