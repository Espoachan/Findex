#include "source.hpp"
#include "protocol.hpp"
#include "service.hpp"

// ---------------------------------------------------------------------------
// LocalSource

std::unique_ptr<LocalSource> LocalSource::start(wchar_t drive, bool force_rescan) {
    std::unique_ptr<LocalSource> s(new LocalSource());
    s->done_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    LocalSource* raw = s.get();
    s->worker = std::thread([raw, drive, force_rescan] {
        raw->indexer.run(drive, force_rescan);
        SetEvent(raw->done_event);
    });
    return s;
}

std::unique_ptr<LocalSource> LocalSource::snapshot(wchar_t drive) {
    std::unique_ptr<LocalSource> s(new LocalSource());
    s->is_snapshot = true;
    if (!s->indexer.loadSnapshot(drive)) return nullptr;
    return s;
}

LocalSource::~LocalSource() {
    close();
    if (done_event) CloseHandle(done_event);
}

void LocalSource::close() {
    if (worker.joinable()) {
        indexer.stop();
        worker.join();
    }
}

void LocalSource::emergencyStop(DWORD timeout_ms) {
    indexer.stop();
    if (done_event) WaitForSingleObject(done_event, timeout_ms);
}

// ---------------------------------------------------------------------------
// ServiceClient

std::unique_ptr<ServiceClient> ServiceClient::connect(wchar_t drive, std::wstring& why) {
    DWORD service_pid = 0;
    const service::Status status = service::query(&service_pid);
    if (status == service::Status::NotInstalled) {
        why = L"the Findex service is not installed";
        return nullptr;
    }
    if (status != service::Status::Running) {
        why = L"the Findex service is installed but not running";
        return nullptr;
    }

    HANDLE pipe = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 20; ++attempt) {
        // SECURITY_IDENTIFICATION: the server may identify us but never act as us.
        pipe = CreateFileW(proto::PIPE_NAME, GENERIC_READ | FILE_WRITE_DATA, 0, nullptr, OPEN_EXISTING,
                           SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
        if (pipe != INVALID_HANDLE_VALUE) break;
        const DWORD err = GetLastError();
        if (err == ERROR_PIPE_BUSY) WaitNamedPipeW(proto::PIPE_NAME, 500);
        else if (err == ERROR_FILE_NOT_FOUND) Sleep(50); // between two pipe instances
        else if (err == ERROR_ACCESS_DENIED) {
            why = L"access to the Findex service was denied (it was installed for another user)";
            return nullptr;
        } else break;
    }
    if (pipe == INVALID_HANDLE_VALUE) {
        why = L"could not connect to the Findex service";
        return nullptr;
    }

    // Make sure the other end really is the service and not some process that
    // grabbed the pipe name.
    ULONG server_pid = 0;
    if (!GetNamedPipeServerProcessId(pipe, &server_pid) || server_pid != service_pid) {
        CloseHandle(pipe);
        why = L"the pipe is not owned by the Findex service";
        return nullptr;
    }

    std::unique_ptr<ServiceClient> c(new ServiceClient());
    c->pipe = pipe;
    c->drive = static_cast<wchar_t>(towupper(drive));
    if (!c->queryStatus()) {
        why = c->error.empty() ? L"the Findex service did not answer" : c->error;
        return nullptr;
    }
    return c;
}

ServiceClient::~ServiceClient() { close(); }

void ServiceClient::close() {
    if (pipe != INVALID_HANDLE_VALUE) {
        CloseHandle(pipe);
        pipe = INVALID_HANDLE_VALUE;
    }
}

void ServiceClient::lost() {
    close();
    st = IndexState::Error;
    error = L"lost the connection to the Findex service";
    ++gen;
}

bool ServiceClient::queryStatus() {
    if (pipe == INVALID_HANDLE_VALUE) return false;
    proto::Buffer req;
    req.put(proto::OP_STATUS);
    req.put(static_cast<uint16_t>(drive));
    std::vector<uint8_t> reply;
    if (!proto::writeFrame(pipe, req) || !proto::readFrame(pipe, reply)) {
        lost();
        return false;
    }
    proto::Cursor c(reply);
    uint8_t found = 0;
    if (!c.get(found)) {
        lost();
        return false;
    }
    if (!found) {
        error = std::wstring(L"drive ") + drive + L": is not indexed by the service "
                L"(reinstall it with: findex --install-service " + drive + L")";
        st = IndexState::Error;
        return false;
    }
    uint8_t state = 0;
    uint64_t n = 0, g = 0;
    std::wstring err;
    if (!c.get(state) || !c.get(n) || !c.get(g) || !c.getStr(err)) {
        lost();
        return false;
    }
    st = static_cast<IndexState>(state);
    count = static_cast<size_t>(n);
    gen = g;
    error = err;
    return true;
}

void ServiceClient::poll() {
    const auto now = std::chrono::steady_clock::now();
    if (now - last_poll < std::chrono::milliseconds(250)) return;
    last_poll = now;
    queryStatus();
}

std::vector<SearchResult> ServiceClient::search(const std::wstring& q, const SearchOptions& o, size_t* total) {
    std::vector<SearchResult> results;
    if (total) *total = 0;
    if (pipe == INVALID_HANDLE_VALUE) return results;

    proto::Buffer req;
    req.put(proto::OP_SEARCH);
    req.put(static_cast<uint16_t>(drive));
    req.put(static_cast<uint8_t>(o.filter));
    req.put(static_cast<uint8_t>(o.rank ? 1 : 0));
    req.put(static_cast<uint64_t>(o.limit));
    req.putStr(q);

    std::vector<uint8_t> frame;
    if (!proto::writeFrame(pipe, req) || !proto::readFrame(pipe, frame)) {
        lost();
        return results;
    }
    proto::Cursor head(frame);
    uint8_t found = 0;
    uint64_t t = 0, n = 0;
    if (!head.get(found) || (found && (!head.get(t) || !head.get(n)))) {
        lost();
        return results;
    }
    if (!found) return results;
    if (total) *total = static_cast<size_t>(t);

    results.reserve(static_cast<size_t>(std::min<uint64_t>(n, 1u << 20)));
    while (results.size() < n) {
        if (!proto::readFrame(pipe, frame)) {
            lost();
            break;
        }
        proto::Cursor c(frame);
        uint32_t batch = 0;
        if (!c.get(batch) || batch == 0) {
            lost();
            break;
        }
        for (uint32_t i = 0; i < batch; ++i) {
            SearchResult r;
            uint8_t dir = 0;
            if (!c.get(r.frn) || !c.get(dir) || !c.getStr(r.name) || !c.getStr(r.path)) {
                lost();
                return results;
            }
            r.is_directory = dir != 0;
            results.push_back(std::move(r));
        }
    }
    return results;
}
