#include "SocketImpl.h"
#include "NetException.h"
#include "StreamSocketImpl.h"
//#include "NumberFormatter.h"
#include <base/Utils/utils_str.h>
#include "base/Time/Timestamp.h"
#include <string.h> // FD_SET needs memset on some platforms, so we can't use <cstring>
#if defined(PIL_HAVE_FD_EPOLL)
#include <sys/epoll.h>
#elif defined(PIL_HAVE_FD_POLL)
#include <poll.h>
#endif


#if defined(sun) || defined(__sun) || defined(__sun__)
#include <unistd.h>
#include <stropts.h>
#endif

namespace pi {


SocketImpl::SocketImpl():
    _sockfd(PIL_INVALID_SOCKET),
    _blocking(true)
{
}


SocketImpl::SocketImpl(pil_socket_t sockfd):
    _sockfd(sockfd),
    _blocking(true)
{
}


SocketImpl::~SocketImpl()
{
    close();
}


SocketImpl* SocketImpl::acceptConnection(SocketAddress& clientAddr)
{
    if (_sockfd == PIL_INVALID_SOCKET) throw InvalidSocketException();

    char buffer[SocketAddress::MAX_ADDRESS_LENGTH];
    struct sockaddr* pSA = reinterpret_cast<struct sockaddr*>(buffer);
    pil_socklen_t saLen = sizeof(buffer);
    pil_socket_t sd;
    do
    {
        sd = ::accept(_sockfd, pSA, &saLen);
    }
    while (sd == PIL_INVALID_SOCKET && lastError() == PIL_EINTR);
    if (sd != PIL_INVALID_SOCKET)
    {
        clientAddr = SocketAddress(pSA, saLen);
        return new StreamSocketImpl(sd);
    }
    error(); // will throw
    return 0;
}


void SocketImpl::connect(const SocketAddress& address)
{
    if (_sockfd == PIL_INVALID_SOCKET)
    {
        init(address.af());
    }
    int rc;
    do
    {
#if defined(PIL_VXWORKS)
        rc = ::connect(_sockfd, (sockaddr*) address.addr(), address.length());
#else
        rc = ::connect(_sockfd, address.addr(), address.length());
#endif
    }
    while (rc != 0 && lastError() == PIL_EINTR);
    if (rc != 0)
    {
        int err = lastError();
        error(err, address.toString());
    }
}


void SocketImpl::connect(const SocketAddress& address, const pi::Timespan& timeout)
{
    if (_sockfd == PIL_INVALID_SOCKET)
    {
        init(address.af());
    }
    setBlocking(false);
    try
    {
#if defined(PIL_VXWORKS)
        int rc = ::connect(_sockfd, (sockaddr*) address.addr(), address.length());
#else
        int rc = ::connect(_sockfd, address.addr(), address.length());
#endif
        if (rc != 0)
        {
            int err = lastError();
            if (err != PIL_EINPROGRESS && err != PIL_EWOULDBLOCK)
                error(err, address.toString());
            if (!poll(timeout, SELECT_READ | SELECT_WRITE | SELECT_ERROR))
                throw pi::TimeoutException("connect timed out", address.toString());
            err = socketError();
            if (err != 0) error(err);
        }
    }
    catch (pi::Exception&)
    {
        setBlocking(true);
        throw;
    }
    setBlocking(true);
}


void SocketImpl::connectNB(const SocketAddress& address)
{
    if (_sockfd == PIL_INVALID_SOCKET)
    {
        init(address.af());
    }
    setBlocking(false);
#if defined(PIL_VXWORKS)
    int rc = ::connect(_sockfd, (sockaddr*) address.addr(), address.length());
#else
    int rc = ::connect(_sockfd, address.addr(), address.length());
#endif
    if (rc != 0)
    {
        int err = lastError();
        if (err != PIL_EINPROGRESS && err != PIL_EWOULDBLOCK)
            error(err, address.toString());
    }
}


void SocketImpl::bind(const SocketAddress& address, bool reuseAddress)
{
    if (_sockfd == PIL_INVALID_SOCKET)
    {
        init(address.af());
    }
    if (reuseAddress)
    {
        setReuseAddress(true);
        setReusePort(true);
    }
#if defined(PIL_VXWORKS)
    int rc = ::bind(_sockfd, (sockaddr*) address.addr(), address.length());
#else
    int rc = ::bind(_sockfd, address.addr(), address.length());
#endif
    if (rc != 0) error(address.toString());
}


void SocketImpl::bind6(const SocketAddress& address, bool reuseAddress, bool ipV6Only)
{
#if defined(PIL_HAVE_IPv6)
    if (address.family() != IPAddress::IPv6)
        throw pi::InvalidArgumentException("SocketAddress must be an IPv6 address");

    if (_sockfd == PIL_INVALID_SOCKET)
    {
        init(address.af());
    }
#ifdef IPV6_V6ONLY
    setOption(IPPROTO_IPV6, IPV6_V6ONLY, ipV6Only ? 1 : 0);
#else
    if (ipV6Only) throw pi::NotImplementedException("IPV6_V6ONLY not defined.");
#endif
    if (reuseAddress)
    {
        setReuseAddress(true);
        setReusePort(true);
    }
    int rc = ::bind(_sockfd, address.addr(), address.length());
    if (rc != 0) error(address.toString());
#else
    throw pi::NotImplementedException("No IPv6 support available");
#endif
}


void SocketImpl::listen(int backlog)
{
    if (_sockfd == PIL_INVALID_SOCKET) throw InvalidSocketException();

    int rc = ::listen(_sockfd, backlog);
    if (rc != 0) error();
}


void SocketImpl::close()
{
    if (_sockfd != PIL_INVALID_SOCKET)
    {
        pil_closesocket(_sockfd);
        _sockfd = PIL_INVALID_SOCKET;
    }
}


void SocketImpl::shutdownReceive()
{
    if (_sockfd == PIL_INVALID_SOCKET) throw InvalidSocketException();

    int rc = ::shutdown(_sockfd, 0);
    if (rc != 0) error();
}


void SocketImpl::shutdownSend()
{
    if (_sockfd == PIL_INVALID_SOCKET) throw InvalidSocketException();

    int rc = ::shutdown(_sockfd, 1);
    if (rc != 0) error();
}


void SocketImpl::shutdown()
{
    if (_sockfd == PIL_INVALID_SOCKET) throw InvalidSocketException();

    int rc = ::shutdown(_sockfd, 2);
    if (rc != 0) error();
}


int SocketImpl::sendBytes(const void* buffer, int length, int flags)
{
#if defined(PIL_BROKEN_TIMEOUTS)
    if (_sndTimeout.totalMicroseconds() != 0)
    {
        if (!poll(_sndTimeout, SELECT_WRITE))
            throw TimeoutException();
    }
#endif

    int rc;
    do
    {
        if (_sockfd == PIL_INVALID_SOCKET) throw InvalidSocketException();
        rc = ::send(_sockfd, reinterpret_cast<const char*>(buffer), length, flags);
    }
    while (_blocking && rc < 0 && lastError() == PIL_EINTR);
    if (rc < 0) error();
    return rc;
}


int SocketImpl::receiveBytes(void* buffer, int length, int flags)
{
#if defined(PIL_BROKEN_TIMEOUTS)
    if (_recvTimeout.totalMicroseconds() != 0)
    {
        if (!poll(_recvTimeout, SELECT_READ))
            throw TimeoutException();
    }
#endif

    int rc;
    do
    {
        if (_sockfd == PIL_INVALID_SOCKET) throw InvalidSocketException();
        rc = ::recv(_sockfd, reinterpret_cast<char*>(buffer), length, flags);
    }
    while (_blocking && rc < 0 && lastError() == PIL_EINTR);
    if (rc < 0)
    {
        int err = lastError();
        if (err == PIL_EAGAIN && !_blocking)
            ;
        else if (err == PIL_EAGAIN || err == PIL_ETIMEDOUT)
            throw TimeoutException(err);
        else
            error(err);
    }
    return rc;
}


int SocketImpl::sendTo(const void* buffer, int length, const SocketAddress& address, int flags)
{
    int rc;
    do
    {
        if (_sockfd == PIL_INVALID_SOCKET) throw InvalidSocketException();
#if defined(PIL_VXWORKS)
        rc = ::sendto(_sockfd, (char*) buffer, length, flags, (sockaddr*) address.addr(), address.length());
#else
        rc = ::sendto(_sockfd, reinterpret_cast<const char*>(buffer), length, flags, address.addr(), address.length());
#endif
    }
    while (_blocking && rc < 0 && lastError() == PIL_EINTR);
    if (rc < 0) error();
    return rc;
}


int SocketImpl::receiveFrom(void* buffer, int length, SocketAddress& address, int flags)
{
#if defined(PIL_BROKEN_TIMEOUTS)
    if (_recvTimeout.totalMicroseconds() != 0)
    {
        if (!poll(_recvTimeout, SELECT_READ))
            throw TimeoutException();
    }
#endif

    char abuffer[SocketAddress::MAX_ADDRESS_LENGTH];
    struct sockaddr* pSA = reinterpret_cast<struct sockaddr*>(abuffer);
    pil_socklen_t saLen = sizeof(abuffer);
    int rc;
    do
    {
        if (_sockfd == PIL_INVALID_SOCKET) throw InvalidSocketException();
        rc = ::recvfrom(_sockfd, reinterpret_cast<char*>(buffer), length, flags, pSA, &saLen);
    }
    while (_blocking && rc < 0 && lastError() == PIL_EINTR);
    if (rc >= 0)
    {
        address = SocketAddress(pSA, saLen);
    }
    else
    {
        int err = lastError();
        if (err == PIL_EAGAIN && !_blocking)
            ;
        else if (err == PIL_EAGAIN || err == PIL_ETIMEDOUT)
            throw TimeoutException(err);
        else
            error(err);
    }
    return rc;
}


void SocketImpl::sendUrgent(unsigned char data)
{
    if (_sockfd == PIL_INVALID_SOCKET) throw InvalidSocketException();

    int rc = ::send(_sockfd, reinterpret_cast<const char*>(&data), sizeof(data), MSG_OOB);
    if (rc < 0) error();
}


int SocketImpl::available()
{
    int result;
    ioctl(FIONREAD, result);
    return result;
}


bool SocketImpl::secure() const
{
    return false;
}


bool SocketImpl::poll(const pi::Timespan& timeout, int mode)
{
    pil_socket_t sockfd = _sockfd;
    if (sockfd == PIL_INVALID_SOCKET) throw InvalidSocketException();

#if defined(PIL_HAVE_FD_EPOLL)

    int epollfd = epoll_create(1);
    if (epollfd < 0)
    {
        char buf[1024];
        strerror_r(errno, buf, sizeof(buf));
        error(std::string("Can't create epoll queue: ") + buf);
    }

    struct epoll_event evin;
    memset(&evin, 0, sizeof(evin));

    if (mode & SELECT_READ)
        evin.events |= EPOLLIN;
    if (mode & SELECT_WRITE)
        evin.events |= EPOLLOUT;
    if (mode & SELECT_ERROR)
        evin.events |= EPOLLERR;

    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, sockfd, &evin) < 0)
    {
        char buf[1024];
        strerror_r(errno, buf, sizeof(buf));
        ::close(epollfd);
        error(std::string("Can't insert socket to epoll queue: ") + buf);
    }

    pi::Timespan remainingTime(timeout);
    int rc;
    do
    {
        struct epoll_event evout;
        memset(&evout, 0, sizeof(evout));

        pi::Timestamp start;
        rc = epoll_wait(epollfd, &evout, 1, remainingTime.totalMilliseconds());
        if (rc < 0 && lastError() == PIL_EINTR)
        {
            pi::Timestamp end;
            pi::Timespan waited = end - start;
            if (waited < remainingTime)
                remainingTime -= waited;
            else
                remainingTime = 0;
        }
    }
    while (rc < 0 && lastError() == PIL_EINTR);

    ::close(epollfd);
    if (rc < 0) error();
    return rc > 0;

#elif defined(PIL_HAVE_FD_POLL)

    pollfd pollBuf;

    memset(&pollBuf, 0, sizeof(pollfd));
    pollBuf.fd = _sockfd;
    if (mode & SELECT_READ) pollBuf.events |= POLLIN;
    if (mode & SELECT_WRITE) pollBuf.events |= POLLOUT;

    pi::Timespan remainingTime(timeout);
    int rc;
    do
    {
        pi::Timestamp start;
        rc = ::poll(&pollBuf, 1, remainingTime.totalMilliseconds());

        if (rc < 0 && lastError() == PIL_EINTR)
        {
            pi::Timestamp end;
            pi::Timespan waited = end - start;
            if (waited < remainingTime)
                remainingTime -= waited;
            else
                remainingTime = 0;
        }
    }
    while (rc < 0 && lastError() == PIL_EINTR);

#else

    fd_set fdRead;
    fd_set fdWrite;
    fd_set fdExcept;
    FD_ZERO(&fdRead);
    FD_ZERO(&fdWrite);
    FD_ZERO(&fdExcept);
    if (mode & SELECT_READ)
    {
        FD_SET(sockfd, &fdRead);
    }
    if (mode & SELECT_WRITE)
    {
        FD_SET(sockfd, &fdWrite);
    }
    if (mode & SELECT_ERROR)
    {
        FD_SET(sockfd, &fdExcept);
    }
    pi::Timespan remainingTime(timeout);
    int errorCode = PIL_ENOERR;
    int rc;
    do
    {
        struct timeval tv;
        tv.tv_sec  = (long) remainingTime.totalSeconds();
        tv.tv_usec = (long) remainingTime.useconds();
        pi::Timestamp start;
        rc = ::select(int(sockfd) + 1, &fdRead, &fdWrite, &fdExcept, &tv);
        if (rc < 0 && (errorCode = lastError()) == PIL_EINTR)
        {
            pi::Timestamp end;
            pi::Timespan waited = end - start;
            if (waited < remainingTime)
                remainingTime -= waited;
            else
                remainingTime = 0;
        }
    }
    while (rc < 0 && errorCode == PIL_EINTR);
    if (rc < 0) error(errorCode);
    return rc > 0;

#endif // PIL_HAVE_FD_EPOLL
}


void SocketImpl::setSendBufferSize(int size)
{
    setOption(SOL_SOCKET, SO_SNDBUF, size);
}


int SocketImpl::getSendBufferSize()
{
    int result;
    getOption(SOL_SOCKET, SO_SNDBUF, result);
    return result;
}


void SocketImpl::setReceiveBufferSize(int size)
{
    setOption(SOL_SOCKET, SO_RCVBUF, size);
}


int SocketImpl::getReceiveBufferSize()
{
    int result;
    getOption(SOL_SOCKET, SO_RCVBUF, result);
    return result;
}


void SocketImpl::setSendTimeout(const pi::Timespan& timeout)
{
#if defined(_WIN32) && !defined(PIL_BROKEN_TIMEOUTS)
    int value = (int) timeout.totalMilliseconds();
    setOption(SOL_SOCKET, SO_SNDTIMEO, value);
#elif defined(PIL_BROKEN_TIMEOUTS)
    _sndTimeout = timeout;
#else
    setOption(SOL_SOCKET, SO_SNDTIMEO, timeout);
#endif
}

pi::Timespan SocketImpl::getSendTimeout()
{
    Timespan result;
#if defined(_WIN32) && !defined(PIL_BROKEN_TIMEOUTS)
    int value;
    getOption(SOL_SOCKET, SO_SNDTIMEO, value);
    result = Timespan::TimeDiff(value)*1000;
#elif defined(PIL_BROKEN_TIMEOUTS)
    result = _sndTimeout;
#else
    getOption(SOL_SOCKET, SO_SNDTIMEO, result);
#endif
    return result;
}


void SocketImpl::setReceiveTimeout(const pi::Timespan& timeout)
{
#ifndef PIL_BROKEN_TIMEOUTS
#if defined(_WIN32)
    int value = (int) timeout.totalMilliseconds();
    setOption(SOL_SOCKET, SO_RCVTIMEO, value);
#else
    setOption(SOL_SOCKET, SO_RCVTIMEO, timeout);
#endif
#else
    _recvTimeout = timeout;
#endif
}


pi::Timespan SocketImpl::getReceiveTimeout()
{
    Timespan result;
#if defined(_WIN32) && !defined(PIL_BROKEN_TIMEOUTS)
    int value;
    getOption(SOL_SOCKET, SO_RCVTIMEO, value);
    result = Timespan::TimeDiff(value)*1000;
#elif defined(PIL_BROKEN_TIMEOUTS)
    result = _recvTimeout;
#else
    getOption(SOL_SOCKET, SO_RCVTIMEO, result);
#endif
    return result;
}


SocketAddress SocketImpl::address()
{
    if (_sockfd == PIL_INVALID_SOCKET) throw InvalidSocketException();

    char buffer[SocketAddress::MAX_ADDRESS_LENGTH];
    struct sockaddr* pSA = reinterpret_cast<struct sockaddr*>(buffer);
    pil_socklen_t saLen = sizeof(buffer);
    int rc = ::getsockname(_sockfd, pSA, &saLen);
    if (rc == 0)
        return SocketAddress(pSA, saLen);
    else
        error();
    return SocketAddress();
}


SocketAddress SocketImpl::peerAddress()
{
    if (_sockfd == PIL_INVALID_SOCKET) throw InvalidSocketException();

    char buffer[SocketAddress::MAX_ADDRESS_LENGTH];
    struct sockaddr* pSA = reinterpret_cast<struct sockaddr*>(buffer);
    pil_socklen_t saLen = sizeof(buffer);
    int rc = ::getpeername(_sockfd, pSA, &saLen);
    if (rc == 0)
        return SocketAddress(pSA, saLen);
    else
        error();
    return SocketAddress();
}


void SocketImpl::setOption(int level, int option, int value)
{
    setRawOption(level, option, &value, sizeof(value));
}


void SocketImpl::setOption(int level, int option, unsigned value)
{
    setRawOption(level, option, &value, sizeof(value));
}


void SocketImpl::setOption(int level, int option, unsigned char value)
{
    setRawOption(level, option, &value, sizeof(value));
}


void SocketImpl::setOption(int level, int option, const IPAddress& value)
{
    setRawOption(level, option, value.addr(), value.length());
}


void SocketImpl::setOption(int level, int option, const pi::Timespan& value)
{
    struct timeval tv;
    tv.tv_sec  = (long) value.totalSeconds();
    tv.tv_usec = (long) value.useconds();

    setRawOption(level, option, &tv, sizeof(tv));
}


void SocketImpl::setRawOption(int level, int option, const void* value, pil_socklen_t length)
{
    if (_sockfd == PIL_INVALID_SOCKET) throw InvalidSocketException();

#if defined(PIL_VXWORKS)
    int rc = ::setsockopt(_sockfd, level, option, (char*) value, length);
#else
    int rc = ::setsockopt(_sockfd, level, option, reinterpret_cast<const char*>(value), length);
#endif
    if (rc == -1) error();
}


void SocketImpl::getOption(int level, int option, int& value)
{
    pil_socklen_t len = sizeof(value);
    getRawOption(level, option, &value, len);
}


void SocketImpl::getOption(int level, int option, unsigned& value)
{
    pil_socklen_t len = sizeof(value);
    getRawOption(level, option, &value, len);
}


void SocketImpl::getOption(int level, int option, unsigned char& value)
{
    pil_socklen_t len = sizeof(value);
    getRawOption(level, option, &value, len);
}


void SocketImpl::getOption(int level, int option, pi::Timespan& value)
{
    struct timeval tv;
    pil_socklen_t len = sizeof(tv);
    getRawOption(level, option, &tv, len);
    value.assign(tv.tv_sec, tv.tv_usec);
}


void SocketImpl::getOption(int level, int option, IPAddress& value)
{
    char buffer[IPAddress::MAX_ADDRESS_LENGTH];
    pil_socklen_t len = sizeof(buffer);
    getRawOption(level, option, buffer, len);
    value = IPAddress(buffer, len);
}


void SocketImpl::getRawOption(int level, int option, void* value, pil_socklen_t& length)
{
    if (_sockfd == PIL_INVALID_SOCKET) throw InvalidSocketException();

    int rc = ::getsockopt(_sockfd, level, option, reinterpret_cast<char*>(value), &length);
    if (rc == -1) error();
}


void SocketImpl::setLinger(bool on, int seconds)
{
    struct linger l;
    l.l_onoff  = on ? 1 : 0;
    l.l_linger = seconds;
    setRawOption(SOL_SOCKET, SO_LINGER, &l, sizeof(l));
}


void SocketImpl::getLinger(bool& on, int& seconds)
{
    struct linger l;
    pil_socklen_t len = sizeof(l);
    getRawOption(SOL_SOCKET, SO_LINGER, &l, len);
    on      = l.l_onoff != 0;
    seconds = l.l_linger;
}


void SocketImpl::setNoDelay(bool flag)
{
    int value = flag ? 1 : 0;
    setOption(IPPROTO_TCP, TCP_NODELAY, value);
}


bool SocketImpl::getNoDelay()
{
    int value(0);
    getOption(IPPROTO_TCP, TCP_NODELAY, value);
    return value != 0;
}


void SocketImpl::setKeepAlive(bool flag)
{
    int value = flag ? 1 : 0;
    setOption(SOL_SOCKET, SO_KEEPALIVE, value);
}


bool SocketImpl::getKeepAlive()
{
    int value(0);
    getOption(SOL_SOCKET, SO_KEEPALIVE, value);
    return value != 0;
}


void SocketImpl::setReuseAddress(bool flag)
{
    int value = flag ? 1 : 0;
    setOption(SOL_SOCKET, SO_REUSEADDR, value);
}


bool SocketImpl::getReuseAddress()
{
    int value(0);
    getOption(SOL_SOCKET, SO_REUSEADDR, value);
    return value != 0;
}


void SocketImpl::setReusePort(bool flag)
{
#ifdef SO_REUSEPORT
    try
    {
        int value = flag ? 1 : 0;
        setOption(SOL_SOCKET, SO_REUSEPORT, value);
    }
    catch (IOException&)
    {
        // ignore error, since not all implementations
        // support SO_REUSEPORT, even if the macro
        // is defined.
    }
#endif
}


bool SocketImpl::getReusePort()
{
#ifdef SO_REUSEPORT
    int value(0);
    getOption(SOL_SOCKET, SO_REUSEPORT, value);
    return value != 0;
#else
    return false;
#endif
}


void SocketImpl::setOOBInline(bool flag)
{
    int value = flag ? 1 : 0;
    setOption(SOL_SOCKET, SO_OOBINLINE, value);
}


bool SocketImpl::getOOBInline()
{
    int value(0);
    getOption(SOL_SOCKET, SO_OOBINLINE, value);
    return value != 0;
}


void SocketImpl::setBroadcast(bool flag)
{
    int value = flag ? 1 : 0;
    setOption(SOL_SOCKET, SO_BROADCAST, value);
}


bool SocketImpl::getBroadcast()
{
    int value(0);
    getOption(SOL_SOCKET, SO_BROADCAST, value);
    return value != 0;
}


void SocketImpl::setBlocking(bool flag)
{
#if !defined(PIL_OS_FAMILY_UNIX)
    int arg = flag ? 0 : 1;
    ioctl(FIONBIO, arg);
#else
    int arg = fcntl(F_GETFL);
    long flags = arg & ~O_NONBLOCK;
    if (!flag) flags |= O_NONBLOCK;
    (void) fcntl(F_SETFL, flags);
#endif
    _blocking = flag;
}


int SocketImpl::socketError()
{
    int result(0);
    getOption(SOL_SOCKET, SO_ERROR, result);
    return result;
}


void SocketImpl::init(int af)
{
    initSocket(af, SOCK_STREAM);
}


void SocketImpl::initSocket(int af, int type, int proto)
{
    pi_assert (_sockfd == PIL_INVALID_SOCKET);

    _sockfd = ::socket(af, type, proto);
    if (_sockfd == PIL_INVALID_SOCKET)
        error();

#if defined(__MACH__) && defined(__APPLE__) || defined(__FreeBSD__)
    // SIGPIPE sends a signal that if unhandled (which is the default)
    // will crash the process. This only happens on UNIX, and not Linux.
    //
    // In order to have PIL sockets behave the same across platforms, it is
    // best to just ignore SIGPIPE all together.
    setOption(SOL_SOCKET, SO_NOSIGPIPE, 1);
#endif
}


void SocketImpl::ioctl(pil_ioctl_request_t request, int& arg)
{
#if defined(_WIN32)
    int rc = ioctlsocket(_sockfd, request, reinterpret_cast<u_long*>(&arg));
#elif defined(PIL_VXWORKS)
    int rc = ::ioctl(_sockfd, request, (int) &arg);
#else
    int rc = ::ioctl(_sockfd, request, &arg);
#endif
    if (rc != 0) error();
}


void SocketImpl::ioctl(pil_ioctl_request_t request, void* arg)
{
#if defined(_WIN32)
    int rc = ioctlsocket(_sockfd, request, reinterpret_cast<u_long*>(arg));
#elif defined(PIL_VXWORKS)
    int rc = ::ioctl(_sockfd, request, (int) arg);
#else
    int rc = ::ioctl(_sockfd, request, arg);
#endif
    if (rc != 0) error();
}


#if defined(PIL_OS_FAMILY_UNIX)
int SocketImpl::fcntl(pil_fcntl_request_t request)
{
    int rc = ::fcntl(_sockfd, request);
    if (rc == -1) error();
    return rc;
}


int SocketImpl::fcntl(pil_fcntl_request_t request, long arg)
{
    int rc = ::fcntl(_sockfd, request, arg);
    if (rc == -1) error();
    return rc;
}
#endif


void SocketImpl::reset(pil_socket_t aSocket)
{
    _sockfd = aSocket;
}


void SocketImpl::error()
{
    int err = lastError();
    std::string empty;
    error(err, empty);
}


void SocketImpl::error(const std::string& arg)
{
    error(lastError(), arg);
}


void SocketImpl::error(int code)
{
    std::string arg;
    error(code, arg);
}


void SocketImpl::error(int code, const std::string& arg)
{
    switch (code)
    {
    case PIL_ENOERR: return;
    case PIL_ESYSNOTREADY:
        throw NetException("Net subsystem not ready", code);
    case PIL_ENOTINIT:
        throw NetException("Net subsystem not initialized", code);
    case PIL_EINTR:
        throw IOException("Interrupted", code);
    case PIL_EACCES:
        throw IOException("Permission denied", code);
    case PIL_EFAULT:
        throw IOException("Bad address", code);
    case PIL_EINVAL:
        throw InvalidArgumentException(code);
    case PIL_EMFILE:
        throw IOException("Too many open files", code);
    case PIL_EWOULDBLOCK:
        throw IOException("Operation would block", code);
    case PIL_EINPROGRESS:
        throw IOException("Operation now in progress", code);
    case PIL_EALREADY:
        throw IOException("Operation already in progress", code);
    case PIL_ENOTSOCK:
        throw IOException("Socket operation attempted on non-socket", code);
    case PIL_EDESTADDRREQ:
        throw NetException("Destination address required", code);
    case PIL_EMSGSIZE:
        throw NetException("Message too long", code);
    case PIL_EPROTOTYPE:
        throw NetException("Wrong protocol type", code);
    case PIL_ENOPROTOOPT:
        throw NetException("Protocol not available", code);
    case PIL_EPROTONOSUPPORT:
        throw NetException("Protocol not supported", code);
    case PIL_ESOCKTNOSUPPORT:
        throw NetException("Socket type not supported", code);
    case PIL_ENOTSUP:
        throw NetException("Operation not supported", code);
    case PIL_EPFNOSUPPORT:
        throw NetException("Protocol family not supported", code);
    case PIL_EAFNOSUPPORT:
        throw NetException("Address family not supported", code);
    case PIL_EADDRINUSE:
        throw NetException("Address already in use", arg, code);
    case PIL_EADDRNOTAVAIL:
        throw NetException("Cannot assign requested address", arg, code);
    case PIL_ENETDOWN:
        throw NetException("Network is down", code);
    case PIL_ENETUNREACH:
        throw NetException("Network is unreachable", code);
    case PIL_ENETRESET:
        throw NetException("Network dropped connection on reset", code);
    case PIL_ECONNABORTED:
        throw ConnectionAbortedException(code);
    case PIL_ECONNRESET:
        throw ConnectionResetException(code);
    case PIL_ENOBUFS:
        throw IOException("No buffer space available", code);
    case PIL_EISCONN:
        throw NetException("Socket is already connected", code);
    case PIL_ENOTCONN:
        throw NetException("Socket is not connected", code);
    case PIL_ESHUTDOWN:
        throw NetException("Cannot send after socket shutdown", code);
    case PIL_ETIMEDOUT:
        throw TimeoutException(code);
    case PIL_ECONNREFUSED:
        throw ConnectionRefusedException(arg, code);
    case PIL_EHOSTDOWN:
        throw NetException("Host is down", arg, code);
    case PIL_EHOSTUNREACH:
        throw NetException("No route to host", arg, code);
#if defined(PIL_OS_FAMILY_UNIX)
    case EPIPE:
        throw IOException("Broken pipe", code);
    case EBADF:
        throw IOException("Bad socket descriptor", code);
#endif
    default:
        throw IOException(pi::itos(code), arg, code);
    }
}


} // namespace pi
