#pragma once

// Wire protocol between the findex client and the Findex service over a local
// named pipe. Every message is a frame: u32 length + payload (little endian).
//
//   Status request : u8 OP_STATUS, u16 drive
//   Status reply   : u8 found, [u8 state, u64 count, u64 generation, str error]
//   Search request : u8 OP_SEARCH, u16 drive, u8 filter, u8 rank, u64 limit, str query
//   Search reply   : u8 found, [u64 total, u64 n]  followed by frames of
//                    u32 count, count x (u64 frn, u8 is_dir, str name, str path)
//                    until n results were sent.
//
// str = u32 length in wchar_t + UTF-16 data.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace proto {

inline const wchar_t* const PIPE_NAME = L"\\\\.\\pipe\\findex";
constexpr uint32_t MAX_FRAME = 64u * 1024 * 1024;
constexpr uint32_t MAX_STRING = 1u << 20;
constexpr uint32_t RESULTS_PER_FRAME = 2048;

enum Op : uint8_t { OP_STATUS = 1, OP_SEARCH = 2 };

struct Buffer {
    std::vector<uint8_t> data;

    template <typename T> void put(const T& v) {
        const auto* p = reinterpret_cast<const uint8_t*>(&v);
        data.insert(data.end(), p, p + sizeof(T));
    }
    void putStr(const std::wstring& s) {
        put(static_cast<uint32_t>(s.size()));
        const auto* p = reinterpret_cast<const uint8_t*>(s.data());
        data.insert(data.end(), p, p + s.size() * sizeof(wchar_t));
    }
};

struct Cursor {
    const uint8_t* p;
    const uint8_t* end;

    explicit Cursor(const std::vector<uint8_t>& v) : p(v.data()), end(v.data() + v.size()) {}

    template <typename T> bool get(T& v) {
        if (static_cast<size_t>(end - p) < sizeof(T)) return false;
        std::memcpy(&v, p, sizeof(T));
        p += sizeof(T);
        return true;
    }
    bool getStr(std::wstring& s) {
        uint32_t n;
        if (!get(n) || n > MAX_STRING || static_cast<size_t>(end - p) < n * sizeof(wchar_t)) return false;
        s.resize(n);
        if (n) std::memcpy(s.data(), p, n * sizeof(wchar_t));
        p += n * sizeof(wchar_t);
        return true;
    }
};

// Reads/writes exactly `n` bytes. With `io_event` the handle is overlapped and
// the wait is cancelled when `stop_event` is signalled; without, plain sync I/O.
inline bool transfer(HANDLE h, bool write, void* buf, DWORD n, HANDLE io_event, HANDLE stop_event) {
    auto* p = static_cast<uint8_t*>(buf);
    while (n > 0) {
        DWORD done = 0;
        if (!io_event) {
            BOOL ok = write ? WriteFile(h, p, n, &done, nullptr) : ReadFile(h, p, n, &done, nullptr);
            if (!ok || done == 0) return false;
        } else {
            OVERLAPPED ov = {};
            ResetEvent(io_event);
            ov.hEvent = io_event;
            BOOL ok = write ? WriteFile(h, p, n, nullptr, &ov) : ReadFile(h, p, n, nullptr, &ov);
            if (!ok && GetLastError() != ERROR_IO_PENDING) return false;
            HANDLE handles[2] = {io_event, stop_event};
            DWORD w = WaitForMultipleObjects(stop_event ? 2 : 1, handles, FALSE, INFINITE);
            if (w != WAIT_OBJECT_0) {
                CancelIoEx(h, &ov);
                GetOverlappedResult(h, &ov, &done, TRUE);
                return false;
            }
            if (!GetOverlappedResult(h, &ov, &done, FALSE) || done == 0) return false;
        }
        p += done;
        n -= done;
    }
    return true;
}

inline bool writeFrame(HANDLE h, const Buffer& b, HANDLE io_event = nullptr, HANDLE stop_event = nullptr) {
    uint32_t len = static_cast<uint32_t>(b.data.size());
    std::vector<uint8_t> out(sizeof(len) + len);
    std::memcpy(out.data(), &len, sizeof(len));
    if (len) std::memcpy(out.data() + sizeof(len), b.data.data(), len);
    return transfer(h, true, out.data(), static_cast<DWORD>(out.size()), io_event, stop_event);
}

inline bool readFrame(HANDLE h, std::vector<uint8_t>& out, HANDLE io_event = nullptr, HANDLE stop_event = nullptr) {
    uint32_t len = 0;
    if (!transfer(h, false, &len, sizeof(len), io_event, stop_event)) return false;
    if (len > MAX_FRAME) return false;
    out.resize(len);
    return len == 0 || transfer(h, false, out.data(), len, io_event, stop_event);
}

} // namespace proto
