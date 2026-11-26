#pragma once

// Where search results come from: the indexer running in this process, a
// snapshot loaded from disk, or the Findex service through its pipe.

#include "indexer.hpp"

#include <windows.h>

#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

class IndexSource {
public:
    virtual ~IndexSource() = default;

    virtual std::vector<SearchResult> search(const std::wstring& query, const SearchOptions& options,
                                             size_t* total) = 0;
    virtual size_t fileCount() = 0;
    virtual IndexState state() = 0;
    virtual std::wstring lastError() = 0;
    virtual uint64_t generation() = 0; // changes whenever the index changes

    // Call regularly (every UI tick): lets remote sources refresh their status.
    virtual void poll() {}
    // Short label for the UI ("service", "snapshot"); empty for the local indexer.
    virtual std::wstring label() { return L""; }
    // True when close() will spend a moment saving the index.
    virtual bool savesOnClose() { return false; }
    virtual void close() {}
    // Called from the console close handler: finish up within `timeout_ms`.
    virtual void emergencyStop(DWORD /*timeout_ms*/) {}
};

// Indexer running on a thread of this process (needs administrator rights),
// or a read-only snapshot of the saved index.
class LocalSource : public IndexSource {
public:
    static std::unique_ptr<LocalSource> start(wchar_t drive, bool force_rescan);
    static std::unique_ptr<LocalSource> snapshot(wchar_t drive);
    ~LocalSource() override;

    std::vector<SearchResult> search(const std::wstring& q, const SearchOptions& o, size_t* total) override {
        return indexer.search(q, o, total);
    }
    size_t fileCount() override { return indexer.fileCount(); }
    IndexState state() override { return indexer.state(); }
    std::wstring lastError() override { return indexer.lastError(); }
    uint64_t generation() override { return indexer.generation(); }
    std::wstring label() override { return is_snapshot ? L"saved index, not live" : L""; }
    bool savesOnClose() override { return worker.joinable() && indexer.state() == IndexState::Ready; }
    void close() override;
    void emergencyStop(DWORD timeout_ms) override;

private:
    LocalSource() = default;
    USNIndexer indexer;
    std::thread worker;
    HANDLE done_event = nullptr;
    bool is_snapshot = false;
};

// Talks to the Findex service. No administrator rights needed.
class ServiceClient : public IndexSource {
public:
    // Returns nullptr and fills `why` when the service can't be used.
    static std::unique_ptr<ServiceClient> connect(wchar_t drive, std::wstring& why);
    ~ServiceClient() override;

    std::vector<SearchResult> search(const std::wstring& q, const SearchOptions& o, size_t* total) override;
    size_t fileCount() override { return count; }
    IndexState state() override { return st; }
    std::wstring lastError() override { return error; }
    uint64_t generation() override { return gen; }
    void poll() override;
    std::wstring label() override { return L"service"; }
    void close() override;

private:
    ServiceClient() = default;
    bool queryStatus();
    void lost();

    HANDLE pipe = INVALID_HANDLE_VALUE;
    wchar_t drive = L'C';
    IndexState st = IndexState::Loading;
    size_t count = 0;
    uint64_t gen = 0;
    std::wstring error;
    std::chrono::steady_clock::time_point last_poll{};
};
