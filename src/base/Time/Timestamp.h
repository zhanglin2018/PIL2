#ifndef PIL_Timestamp_INCLUDED
#define PIL_Timestamp_INCLUDED


#include "../Environment.h"
#include "../Types/Int.h"
#include <ctime>

#ifdef PIL_OS_FAMILY_WINDOWS
#include "../Platform/UnWindows.h"
#elif defined(PIL_OS_FAMILY_UNIX)
#include "time.h"
#endif


namespace pi {

class PIL_API Timestamp
    /// A Timestamp stores a monotonic* time value
    /// with (theoretical) microseconds resolution.
    /// Timestamps can be compared with each other
    /// and simple arithmetics are supported.
    ///
    /// [*] Note that Timestamp values are only monotonic as
    /// long as the systems's clock is monotonic as well
    /// (and not, e.g. set back).
    ///
    /// Timestamps are UTC (Coordinated Universal Time)
    /// based and thus independent of the timezone
    /// in effect on the system.
{
public:
    typedef Int64 TimeVal;    /// monotonic UTC time value in microsecond resolution
    typedef Int64 UtcTimeVal; /// monotonic UTC time value in 100 nanosecond resolution
    typedef Int64 TimeDiff;   /// difference between two timestamps in microseconds

    Timestamp();
        /// Creates a timestamp with the current time.

    Timestamp(TimeVal tv);
        /// Creates a timestamp from the given time value.

    Timestamp(const Timestamp& other);
        /// Copy constructor.

    ~Timestamp();
        /// Destroys the timestamp

    Timestamp& operator = (const Timestamp& other);
    Timestamp& operator = (TimeVal tv);

    void swap(Timestamp& timestamp);
        /// Swaps the Timestamp with another one.

    void update();
        /// Updates the Timestamp with the current time.

    bool operator == (const Timestamp& ts) const;
    bool operator != (const Timestamp& ts) const;
    bool operator >  (const Timestamp& ts) const;
    bool operator >= (const Timestamp& ts) const;
    bool operator <  (const Timestamp& ts) const;
    bool operator <= (const Timestamp& ts) const;

    Timestamp  operator +  (TimeDiff d) const;
    Timestamp  operator -  (TimeDiff d) const;
    TimeDiff   operator -  (const Timestamp& ts) const;
    Timestamp& operator += (TimeDiff d);
    Timestamp& operator -= (TimeDiff d);

    std::time_t epochTime() const;
        /// Returns the timestamp expressed in time_t.
        /// time_t base time is midnight, January 1, 1970.
        /// Resolution is one second.

    UtcTimeVal utcTime() const;
        /// Returns the timestamp expressed in UTC-based
        /// time. UTC base time is midnight, October 15, 1582.
        /// Resolution is 100 nanoseconds.

    TimeVal epochMicroseconds() const;
        /// Returns the timestamp expressed in microseconds
        /// since the Unix epoch, midnight, January 1, 1970.

    operator TimeVal ()
    {
        return _ts;
    }

    double  timestampF() const;
        /// Returns the timestamp expressed in seconds
        /// since the Unix epoch, midnight, January 1, 1970.

    TimeDiff elapsed() const;
        /// Returns the time elapsed since the time denoted by
        /// the timestamp. Equivalent to Timestamp() - *this.

    bool isElapsed(TimeDiff interval) const;
        /// Returns true iff the given interval has passed
        /// since the time denoted by the timestamp.

    static Timestamp fromEpochTime(std::time_t t);
        /// Creates a timestamp from a std::time_t.

    static Timestamp fromUtcTime(UtcTimeVal val);
        /// Creates a timestamp from a UTC time value.

    static TimeVal resolution();
        /// Returns the resolution in units per second.
        /// Since the timestamp has microsecond resolution,
        /// the returned value is always 1000000.

    /// Returns the timestamp expressed in seconds
    /// since the Unix epoch, midnight, January 1, 1970.
    static double  getTimestampF() {
        Timestamp now;
        return now.timestampF();
    }


#if defined(_WIN32)
    static Timestamp fromFileTimeNP(UInt32 fileTimeLow, UInt32 fileTimeHigh);
    void toFileTimeNP(UInt32& fileTimeLow, UInt32& fileTimeHigh) const;
#endif

private:
    TimeVal _ts;
};


//
// inlines
//
inline bool Timestamp::operator == (const Timestamp& ts) const
{
    return _ts == ts._ts;
}


inline bool Timestamp::operator != (const Timestamp& ts) const
{
    return _ts != ts._ts;
}


inline bool Timestamp::operator >  (const Timestamp& ts) const
{
    return _ts > ts._ts;
}


inline bool Timestamp::operator >= (const Timestamp& ts) const
{
    return _ts >= ts._ts;
}


inline bool Timestamp::operator <  (const Timestamp& ts) const
{
    return _ts < ts._ts;
}


inline bool Timestamp::operator <= (const Timestamp& ts) const
{
    return _ts <= ts._ts;
}


inline Timestamp Timestamp::operator + (Timestamp::TimeDiff d) const
{
    return Timestamp(_ts + d);
}


inline Timestamp Timestamp::operator - (Timestamp::TimeDiff d) const
{
    return Timestamp(_ts - d);
}


inline Timestamp::TimeDiff Timestamp::operator - (const Timestamp& ts) const
{
    return _ts - ts._ts;
}


inline Timestamp& Timestamp::operator += (Timestamp::TimeDiff d)
{
    _ts += d;
    return *this;
}


inline Timestamp& Timestamp::operator -= (Timestamp::TimeDiff d)
{
    _ts -= d;
    return *this;
}


inline std::time_t Timestamp::epochTime() const
{
    return std::time_t(_ts/resolution());
}


inline Timestamp::UtcTimeVal Timestamp::utcTime() const
{
    return _ts*10 + (TimeDiff(0x01b21dd2) << 32) + 0x13814000;
}


inline Timestamp::TimeVal Timestamp::epochMicroseconds() const
{
    return _ts;
}

inline double Timestamp::timestampF() const
{
    return _ts*1e-6;
}

inline Timestamp::TimeDiff Timestamp::elapsed() const
{
    Timestamp now;
    return now - *this;
}


inline bool Timestamp::isElapsed(Timestamp::TimeDiff interval) const
{
    Timestamp now;
    Timestamp::TimeDiff diff = now - *this;
    return diff >= interval;
}


inline Timestamp::TimeVal Timestamp::resolution()
{
    return 1000000;
}


inline void swap(Timestamp& s1, Timestamp& s2)
{
    s1.swap(s2);
}

inline void sleep(const Timestamp::TimeVal& microseconds)
{
#ifdef PIL_OS_FAMILY_UNIX
    struct timespec tp;
    tp.tv_sec  = microseconds / 1000000;
    tp.tv_nsec = ( microseconds % 1000000 ) * 1000;

    nanosleep(&tp, &tp);
#else

    HANDLE timer;
    LARGE_INTEGER ft;

    // Convert to 100 nanosecond interval, negative value indicates relative time
    ft.QuadPart = -(10*microseconds);

    timer = CreateWaitableTimer(NULL, TRUE, NULL);
    SetWaitableTimer(timer, &ft, 0, NULL, NULL, 0);
    WaitForSingleObject(timer, INFINITE);
    CloseHandle(timer);
#endif
}

} // namespace PIL


#endif // PIL_Timestamp_INCLUDED
