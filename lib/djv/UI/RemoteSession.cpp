// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

#include <djv/UI/RemoteSession.h>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif // NOMINMAX
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif // WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif // _WIN32

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace djv
{
    namespace ui
    {
#if defined(_WIN32)
        namespace
        {
            struct Event
            {
                bool connected = false;
                std::time_t time = 0;
            };

            //! "[I 2026-10-15 22:52:33] Name#1234 connected.", or
            //! "disconnected.". A connection that failed ends otherwise.
            std::optional<Event> parseEvent(std::string_view line)
            {
                while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
                {
                    line.remove_suffix(1);
                }
                Event out;
                const std::string_view disconnected = " disconnected.";
                const std::string_view connected = " connected.";
                if (line.ends_with(disconnected))
                {
                    out.connected = false;
                }
                else if (line.ends_with(connected))
                {
                    out.connected = true;
                }
                else
                {
                    return std::nullopt;
                }
                if (line.size() < 24 || line[0] != '[' || line[2] != ' ' || line[22] != ']')
                {
                    return std::nullopt;
                }
                const std::string stamp(line.substr(3, 19));
                std::tm tm = {};
                if (std::sscanf(
                    stamp.c_str(),
                    "%d-%d-%d %d:%d:%d",
                    &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
                    &tm.tm_hour, &tm.tm_min, &tm.tm_sec) != 6)
                {
                    return std::nullopt;
                }
                tm.tm_year -= 1900;
                tm.tm_mon -= 1;
                tm.tm_isdst = -1;
                out.time = std::mktime(&tm);
                return out;
            }

            //! The last event in the lines of the text, the last one first.
            std::optional<Event> lastEvent(std::string_view text)
            {
                size_t end = text.size();
                while (true)
                {
                    const size_t nl = 0 == end ? std::string_view::npos : text.rfind('\n', end - 1);
                    const size_t start = std::string_view::npos == nl ? 0 : nl + 1;
                    if (auto event = parseEvent(text.substr(start, end - start)))
                    {
                        return event;
                    }
                    if (std::string_view::npos == nl)
                    {
                        break;
                    }
                    end = nl;
                }
                return std::nullopt;
            }

            //! The last event in the log, read back from its end.
            std::optional<Event> lastEventFromEnd(
                const std::filesystem::path& path,
                uintmax_t size)
            {
                std::ifstream f(path, std::ios::binary);
                if (!f)
                {
                    return std::nullopt;
                }
                const uintmax_t limit = 8 * 1024 * 1024;
                const uintmax_t chunk = 64 * 1024;
                uintmax_t pos = size;
                std::string tail;
                while (pos > 0 && size - pos < limit)
                {
                    const uintmax_t step = std::min(chunk, pos);
                    pos -= step;
                    std::string buf(static_cast<size_t>(step), 0);
                    f.seekg(static_cast<std::streamoff>(pos));
                    f.read(buf.data(), static_cast<std::streamsize>(step));
                    tail = buf + tail;
                    // The first line may be cut, unless it is the start of
                    // the file: it is completed by the next chunk.
                    size_t begin = 0;
                    if (pos > 0)
                    {
                        const size_t nl = tail.find('\n');
                        if (std::string::npos == nl)
                        {
                            continue;
                        }
                        begin = nl + 1;
                    }
                    if (auto event = lastEvent(std::string_view(tail).substr(begin)))
                    {
                        return event;
                    }
                    tail.resize(begin > 0 ? begin - 1 : 0);
                }
                return std::nullopt;
            }

            //! The last event in what was added to the log, and how far it
            //! was read: to the end of its last whole line.
            std::optional<Event> lastEventAdded(
                const std::filesystem::path& path,
                uintmax_t start,
                uintmax_t size,
                uintmax_t& read)
            {
                read = start;
                std::ifstream f(path, std::ios::binary);
                if (!f || size <= start)
                {
                    return std::nullopt;
                }
                std::string buf(static_cast<size_t>(size - start), 0);
                f.seekg(static_cast<std::streamoff>(start));
                f.read(buf.data(), static_cast<std::streamsize>(buf.size()));
                const size_t cut = buf.rfind('\n');
                if (std::string::npos == cut)
                {
                    return std::nullopt;
                }
                read = start + cut + 1;
                return lastEvent(std::string_view(buf).substr(0, cut));
            }

            //! Parsec's log, per user or per computer, whichever was
            //! written last.
            std::filesystem::path getLogPath()
            {
                std::filesystem::path out;
                std::filesystem::file_time_type outTime;
                for (const char* name : { "APPDATA", "PROGRAMDATA" })
                {
                    char buf[MAX_PATH] = {};
                    const DWORD n = GetEnvironmentVariableA(name, buf, MAX_PATH);
                    if (0 == n || n >= MAX_PATH)
                    {
                        continue;
                    }
                    const std::filesystem::path path =
                        std::filesystem::path(buf) / "Parsec" / "log.txt";
                    std::error_code ec;
                    const auto time = std::filesystem::last_write_time(path, ec);
                    if (!ec && (out.empty() || time > outTime))
                    {
                        out = path;
                        outTime = time;
                    }
                }
                return out;
            }

            std::time_t getBootTime()
            {
                return std::time(nullptr) -
                    static_cast<std::time_t>(GetTickCount64() / 1000);
            }
        }

        bool isParsecConnected()
        {
            // Read once, then only what Parsec adds: a long session writes
            // megabytes after its connection.
            static std::mutex mutex;
            static std::filesystem::path statePath;
            static uintmax_t stateRead = 0;
            static uintmax_t stateSize = 0;
            static std::filesystem::file_time_type stateTime;
            static std::optional<Event> stateEvent;
            std::lock_guard<std::mutex> lock(mutex);

            const std::filesystem::path path = getLogPath();
            if (path.empty())
            {
                return false;
            }
            std::error_code ec;
            const uintmax_t size = std::filesystem::file_size(path, ec);
            if (ec)
            {
                return false;
            }
            const auto time = std::filesystem::last_write_time(path, ec);
            if (path != statePath || size < stateRead)
            {
                // A new log, or one started over: from its end.
                statePath = path;
                stateEvent = lastEventFromEnd(path, size);
                stateRead = size;
            }
            else if (size != stateSize || time != stateTime)
            {
                uintmax_t read = stateRead;
                if (auto event = lastEventAdded(path, stateRead, size, read))
                {
                    stateEvent = event;
                }
                stateRead = read;
            }
            stateSize = size;
            stateTime = time;

            // A connection from before the computer started has ended,
            // whatever the log says: Parsec had no time to write it.
            return stateEvent.has_value() &&
                stateEvent->connected &&
                stateEvent->time >= getBootTime() - 60;
        }
#else // _WIN32
        bool isParsecConnected()
        {
            return false;
        }
#endif // _WIN32
    }
}
