#include "service.hpp"
#include "elevate.hpp"
#include "indexer.hpp"
#include "protocol.hpp"
#include "term.hpp"

#include <windows.h>
#include <sddl.h>
#include <shellapi.h>
#include <shlobj.h>

#include <atomic>
#include <cwctype>
#include <memory>
#include <thread>
#include <vector>

using namespace term;

namespace service {

namespace {

// Exit codes of the (elevated) install/uninstall step, reported back to the
// non-elevated process that asked for it.
enum InstallResult : int {
    OK = 0,
    ERR_CANCELLED = 2,
    ERR_COPY = 3,
    ERR_SCM = 4,
    ERR_CREATE = 5,
    ERR_START = 6,
    ERR_DELETE = 7,
    ERR_ARGS = 8,
};

std::wstring errorText(DWORD err) {
    wchar_t* msg = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, err, 0, reinterpret_cast<wchar_t*>(&msg), 0, nullptr);
    std::wstring s = msg ? msg : L"error " + std::to_wstring(err);
    if (msg) LocalFree(msg);
    while (!s.empty() && (s.back() == L'\n' || s.back() == L'\r' || s.back() == L'.')) s.pop_back();
    return s;
}

// Colour only when stderr is a terminal.
std::wstring c(const wchar_t* style) {
    static const bool tty = isConsole(GetStdHandle(STD_ERROR_HANDLE));
    return tty ? style : L"";
}

void say(const wchar_t* icon_style, const std::wstring& icon, const std::wstring& msg) {
    err(c(icon_style) + icon + c(RESET) + L" " + msg + L"\n");
}
void ok(const std::wstring& msg) { say(GOOD, L"✔", msg); }
void fail(const std::wstring& msg) { say(BAD, L"✖", msg); }
void info(const std::wstring& msg) { err(c(MUTED) + msg + c(RESET) + L"\n"); }

bool validSid(const std::wstring& sid) {
    if (sid.size() < 5 || sid.size() > 200 || sid.compare(0, 2, L"S-") != 0) return false;
    for (wchar_t c : sid)
        if (!(iswdigit(c) || c == L'-' || c == L'S')) return false;
    PSID p = nullptr;
    if (!ConvertStringSidToSidW(sid.c_str(), &p)) return false;
    LocalFree(p);
    return true;
}

std::wstring normalizeDrives(const std::wstring& in) {
    std::wstring out;
    for (wchar_t c : in) {
        c = static_cast<wchar_t>(towupper(c));
        if (c >= L'A' && c <= L'Z' && out.find(c) == std::wstring::npos) out += c;
    }
    return out;
}

std::wstring programFilesDir() {
    PWSTR p = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramFiles, 0, nullptr, &p))) dir = std::wstring(p) + L"\\Findex";
    CoTaskMemFree(p);
    return dir;
}

std::wstring selfPath() {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        DWORD n = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (n < path.size()) {
            path.resize(n);
            return path;
        }
        path.resize(path.size() * 2);
    }
}

bool waitForState(SC_HANDLE svc, DWORD wanted, DWORD timeout_ms) {
    const ULONGLONG until = GetTickCount64() + timeout_ms;
    SERVICE_STATUS_PROCESS ssp;
    DWORD needed;
    while (GetTickCount64() < until) {
        if (!QueryServiceStatusEx(svc, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&ssp), sizeof(ssp), &needed))
            return false;
        if (ssp.dwCurrentState == wanted) return true;
        if (wanted == SERVICE_RUNNING && ssp.dwCurrentState == SERVICE_STOPPED) return false;
        Sleep(100);
    }
    return false;
}

bool stopService(SC_HANDLE svc) {
    SERVICE_STATUS status;
    if (!ControlService(svc, SERVICE_CONTROL_STOP, &status)) {
        const DWORD e = GetLastError();
        if (e == ERROR_SERVICE_NOT_ACTIVE) return true;
        if (e != ERROR_SERVICE_CANNOT_ACCEPT_CTRL) return false;
    }
    return waitForState(svc, SERVICE_STOPPED, 30000); // it saves the index on the way out
}

// Re-runs this executable elevated with `args`, waits for it and returns its
// exit code (or ERR_CANCELLED if the UAC prompt was declined).
int runElevated(const std::wstring& args) {
    const std::wstring exe = selfPath();
    SHELLEXECUTEINFOW sei = {sizeof(sei)};
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    sei.lpVerb = L"runas";
    sei.lpFile = exe.c_str();
    sei.lpParameters = args.c_str();
    sei.nShow = SW_HIDE;
    if (!ShellExecuteExW(&sei) || !sei.hProcess) return ERR_CANCELLED;
    WaitForSingleObject(sei.hProcess, INFINITE);
    DWORD code = ERR_SCM;
    GetExitCodeProcess(sei.hProcess, &code);
    CloseHandle(sei.hProcess);
    return static_cast<int>(code);
}

// ---------------------------------------------------------------------------
// The running service

struct ServiceState {
    std::wstring allowed_sid;
    std::wstring drives;
    SERVICE_STATUS_HANDLE status_handle = nullptr;
    SERVICE_STATUS status = {};
    HANDLE stop_event = nullptr;
    std::vector<std::unique_ptr<USNIndexer>> indexers;
    std::vector<wchar_t> indexer_drives;
    std::atomic<int> active_clients{0};
};
ServiceState g;

void reportStatus(DWORD state, DWORD wait_hint = 0) {
    g.status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g.status.dwCurrentState = state;
    g.status.dwControlsAccepted =
        (state == SERVICE_RUNNING) ? (SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN | SERVICE_ACCEPT_PRESHUTDOWN) : 0;
    g.status.dwWin32ExitCode = NO_ERROR;
    g.status.dwWaitHint = wait_hint;
    if (state == SERVICE_START_PENDING || state == SERVICE_STOP_PENDING) ++g.status.dwCheckPoint;
    else g.status.dwCheckPoint = 0;
    SetServiceStatus(g.status_handle, &g.status);
}

DWORD WINAPI controlHandler(DWORD control, DWORD, LPVOID, LPVOID) {
    switch (control) {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
    case SERVICE_CONTROL_PRESHUTDOWN:
        reportStatus(SERVICE_STOP_PENDING, 30000);
        SetEvent(g.stop_event);
        return NO_ERROR;
    case SERVICE_CONTROL_INTERROGATE: return NO_ERROR;
    default: return ERROR_CALL_NOT_IMPLEMENTED;
    }
}

USNIndexer* indexerFor(wchar_t drive) {
    drive = static_cast<wchar_t>(towupper(drive));
    for (size_t i = 0; i < g.indexer_drives.size(); ++i)
        if (g.indexer_drives[i] == drive) return g.indexers[i].get();
    return nullptr;
}

void handleClient(HANDLE pipe) {
    HANDLE io_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::vector<uint8_t> frame;

    while (proto::readFrame(pipe, frame, io_event, g.stop_event)) {
        proto::Cursor c(frame);
        uint8_t op = 0;
        uint16_t drive = 0;
        if (!c.get(op) || !c.get(drive)) break;
        USNIndexer* idx = indexerFor(static_cast<wchar_t>(drive));

        proto::Buffer reply;
        if (op == proto::OP_STATUS) {
            reply.put(static_cast<uint8_t>(idx ? 1 : 0));
            if (idx) {
                reply.put(static_cast<uint8_t>(idx->state()));
                reply.put(static_cast<uint64_t>(idx->fileCount()));
                reply.put(static_cast<uint64_t>(idx->generation()));
                reply.putStr(idx->lastError());
            }
            if (!proto::writeFrame(pipe, reply, io_event, g.stop_event)) break;
        } else if (op == proto::OP_SEARCH) {
            uint8_t filter = 0, rank = 1;
            uint64_t limit = 0;
            std::wstring query;
            if (!c.get(filter) || !c.get(rank) || !c.get(limit) || !c.getStr(query) || filter > 2) break;

            if (!idx) {
                reply.put(static_cast<uint8_t>(0));
                if (!proto::writeFrame(pipe, reply, io_event, g.stop_event)) break;
                continue;
            }
            SearchOptions o;
            o.filter = static_cast<SearchFilter>(filter);
            o.rank = rank != 0;
            o.limit = static_cast<size_t>(limit);
            size_t total = 0;
            const auto results = idx->search(query, o, &total);

            reply.put(static_cast<uint8_t>(1));
            reply.put(static_cast<uint64_t>(total));
            reply.put(static_cast<uint64_t>(results.size()));
            bool sent = proto::writeFrame(pipe, reply, io_event, g.stop_event);

            for (size_t i = 0; sent && i < results.size(); i += proto::RESULTS_PER_FRAME) {
                const size_t n = std::min<size_t>(proto::RESULTS_PER_FRAME, results.size() - i);
                proto::Buffer b;
                b.put(static_cast<uint32_t>(n));
                for (size_t k = i; k < i + n; ++k) {
                    b.put(results[k].frn);
                    b.put(static_cast<uint8_t>(results[k].is_directory ? 1 : 0));
                    b.putStr(results[k].name);
                    b.putStr(results[k].path);
                }
                sent = proto::writeFrame(pipe, b, io_event, g.stop_event);
            }
            if (!sent) break;
        } else {
            break; // unknown request: drop the client
        }
    }

    DisconnectNamedPipe(pipe);
    CloseHandle(pipe);
    CloseHandle(io_event);
    --g.active_clients;
}

void pipeServer() {
    // Only SYSTEM, administrators and the user the service was installed for.
    // The user gets read/write data but NOT FILE_CREATE_PIPE_INSTANCE, so it
    // can't create a fake server instance of this pipe.
    std::wstring sddl = L"D:P(A;;GA;;;SY)(A;;GA;;;BA)";
    if (!g.allowed_sid.empty()) sddl += L"(A;;0x0012019B;;;" + g.allowed_sid + L")";
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &sd, nullptr)) return;
    SECURITY_ATTRIBUTES sa = {sizeof(sa), sd, FALSE};

    HANDLE connect_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    bool first = true;

    while (WaitForSingleObject(g.stop_event, 0) != WAIT_OBJECT_0) {
        HANDLE pipe = CreateNamedPipeW(proto::PIPE_NAME,
                                       PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED |
                                           (first ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0),
                                       PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                       PIPE_UNLIMITED_INSTANCES, 64 * 1024, 64 * 1024, 0, &sa);
        if (pipe == INVALID_HANDLE_VALUE) {
            if (first) break; // someone else owns the name: refuse to serve
            if (WaitForSingleObject(g.stop_event, 1000) == WAIT_OBJECT_0) break;
            continue;
        }
        first = false;

        OVERLAPPED ov = {};
        ResetEvent(connect_event);
        ov.hEvent = connect_event;
        bool connected = ConnectNamedPipe(pipe, &ov) != FALSE;
        if (!connected) {
            const DWORD e = GetLastError();
            if (e == ERROR_PIPE_CONNECTED) {
                connected = true;
            } else if (e == ERROR_IO_PENDING) {
                HANDLE handles[2] = {connect_event, g.stop_event};
                if (WaitForMultipleObjects(2, handles, FALSE, INFINITE) == WAIT_OBJECT_0) {
                    DWORD dummy;
                    connected = GetOverlappedResult(pipe, &ov, &dummy, FALSE) != FALSE;
                } else {
                    CancelIoEx(pipe, &ov);
                    DWORD dummy;
                    GetOverlappedResult(pipe, &ov, &dummy, TRUE);
                }
            }
        }
        if (!connected) {
            CloseHandle(pipe);
            continue;
        }
        ++g.active_clients;
        std::thread(handleClient, pipe).detach();
    }

    CloseHandle(connect_event);
    LocalFree(sd);
}

void WINAPI serviceMain(DWORD, LPWSTR*) {
    g.status_handle = RegisterServiceCtrlHandlerExW(NAME, controlHandler, nullptr);
    if (!g.status_handle) return;
    reportStatus(SERVICE_START_PENDING, 5000);

    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    g.stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    std::vector<std::thread> workers;
    for (wchar_t d : g.drives) {
        g.indexers.push_back(std::make_unique<USNIndexer>());
        g.indexer_drives.push_back(d);
        USNIndexer* idx = g.indexers.back().get();
        workers.emplace_back([idx, d] { idx->run(d); });
    }
    std::thread server(pipeServer);

    reportStatus(SERVICE_RUNNING);
    WaitForSingleObject(g.stop_event, INFINITE);

    // Stop: indexers save their index, clients see the pipe close.
    for (auto& idx : g.indexers) idx->stop();
    for (auto& w : workers) {
        reportStatus(SERVICE_STOP_PENDING, 30000);
        w.join();
    }
    server.join();
    for (int i = 0; i < 50 && g.active_clients > 0; ++i) Sleep(100);

    CoUninitialize();
    reportStatus(SERVICE_STOPPED);
}

} // namespace

// ---------------------------------------------------------------------------

Status query(DWORD* pid) {
    if (pid) *pid = 0;
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return Status::Unknown;
    SC_HANDLE svc = OpenServiceW(scm, NAME, SERVICE_QUERY_STATUS);
    if (!svc) {
        const DWORD e = GetLastError();
        CloseServiceHandle(scm);
        return e == ERROR_SERVICE_DOES_NOT_EXIST ? Status::NotInstalled : Status::Unknown;
    }
    SERVICE_STATUS_PROCESS ssp = {};
    DWORD needed = 0;
    Status result = Status::Unknown;
    if (QueryServiceStatusEx(svc, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&ssp), sizeof(ssp), &needed)) {
        switch (ssp.dwCurrentState) {
        case SERVICE_RUNNING: result = Status::Running; break;
        case SERVICE_START_PENDING:
        case SERVICE_CONTINUE_PENDING: result = Status::Starting; break;
        case SERVICE_STOP_PENDING:
        case SERVICE_PAUSE_PENDING: result = Status::Stopping; break;
        default: result = Status::Stopped; break;
        }
        if (pid) *pid = ssp.dwProcessId;
    }
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return result;
}

int runAsService(const std::wstring& allowed_sid, const std::wstring& drives) {
    g.allowed_sid = validSid(allowed_sid) ? allowed_sid : L"";
    g.drives = normalizeDrives(drives);
    if (g.drives.empty()) g.drives = L"C";

    SERVICE_TABLE_ENTRYW table[] = {{const_cast<LPWSTR>(NAME), serviceMain}, {nullptr, nullptr}};
    if (!StartServiceCtrlDispatcherW(table)) {
        err(L"findex: --service is used by Windows to start the Findex service; it can't be run by hand.\n");
        return 1;
    }
    return 0;
}

std::wstring currentUserSid() {
    HANDLE token = nullptr;
    std::wstring result;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return result;
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<uint8_t> buf(size);
    if (size && GetTokenInformation(token, TokenUser, buf.data(), size, &size)) {
        wchar_t* s = nullptr;
        if (ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buf.data())->User.Sid, &s)) {
            result = s;
            LocalFree(s);
        }
    }
    CloseHandle(token);
    return result;
}

int install(const std::wstring& drives_arg, const std::wstring& for_user_sid) {
    const std::wstring drives = normalizeDrives(drives_arg.empty() ? L"C" : drives_arg);

    if (!isAdministrator()) {
        // One UAC prompt; the elevated copy does the work and reports back.
        info(L"Installing the Findex service needs administrator rights (only this once)…");
        const int rc = runElevated(L"--install-service " + drives + L" --for-user " + currentUserSid());
        switch (rc) {
        case OK: break;
        case ERR_CANCELLED: fail(L"Cancelled: the administrator prompt was declined."); return rc;
        case ERR_COPY: fail(L"Could not copy findex.exe to " + programFilesDir() + L"."); return rc;
        case ERR_SCM:
        case ERR_CREATE: fail(L"Windows refused to register the service."); return rc;
        case ERR_START: fail(L"The service was installed but did not start. Try: sc start Findex"); return rc;
        default: fail(L"Installation failed (code " + std::to_wstring(rc) + L")."); return rc;
        }
        ok(L"Findex service installed and running " + c(MUTED) + L"(indexing " + drives + L")" + c(RESET));
        info(L"From now on findex opens without the administrator prompt.");
        info(L"Tip: add " + programFilesDir() + L" to your PATH to run findex from anywhere.");
        return OK;
    }

    const std::wstring sid = for_user_sid.empty() ? currentUserSid() : for_user_sid;
    if (!validSid(sid)) {
        fail(L"Invalid user SID.");
        return ERR_ARGS;
    }

    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE);
    if (!scm) {
        fail(L"Could not open the service manager: " + errorText(GetLastError()));
        return ERR_SCM;
    }

    // Stop the old version first: its exe is locked while it runs.
    SC_HANDLE svc = OpenServiceW(scm, NAME, SERVICE_ALL_ACCESS);
    if (svc) stopService(svc);

    // The service runs as SYSTEM, so its exe must live where only admins can
    // write (a copy in the user's build folder could be swapped by anyone).
    const std::wstring dir = programFilesDir();
    const std::wstring target = dir + L"\\findex.exe";
    const std::wstring self = selfPath();
    CreateDirectoryW(dir.c_str(), nullptr);
    if (_wcsicmp(self.c_str(), target.c_str()) != 0 && !CopyFileW(self.c_str(), target.c_str(), FALSE)) {
        fail(L"Could not copy findex.exe to " + dir + L": " + errorText(GetLastError()));
        if (svc) CloseServiceHandle(svc);
        CloseServiceHandle(scm);
        return ERR_COPY;
    }

    const std::wstring cmd = L"\"" + target + L"\" --service --user " + sid + L" --drives " + drives;
    if (svc) {
        if (!ChangeServiceConfigW(svc, SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START, SERVICE_ERROR_NORMAL,
                                  cmd.c_str(), nullptr, nullptr, nullptr, nullptr, nullptr, DISPLAY_NAME)) {
            fail(L"Could not update the service: " + errorText(GetLastError()));
            CloseServiceHandle(svc);
            CloseServiceHandle(scm);
            return ERR_CREATE;
        }
    } else {
        svc = CreateServiceW(scm, NAME, DISPLAY_NAME, SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS,
                             SERVICE_AUTO_START, SERVICE_ERROR_NORMAL, cmd.c_str(), nullptr, nullptr, nullptr,
                             nullptr /* LocalSystem */, nullptr);
        if (!svc) {
            fail(L"Could not create the service: " + errorText(GetLastError()));
            CloseServiceHandle(scm);
            return ERR_CREATE;
        }
    }

    SERVICE_DESCRIPTIONW desc = {const_cast<LPWSTR>(
        L"Keeps the Findex file index up to date (NTFS MFT + USN journal) so searches are instant.")};
    ChangeServiceConfig2W(svc, SERVICE_CONFIG_DESCRIPTION, &desc);

    // Restart automatically if it ever crashes.
    SC_ACTION actions[3] = {{SC_ACTION_RESTART, 5000}, {SC_ACTION_RESTART, 30000}, {SC_ACTION_NONE, 0}};
    SERVICE_FAILURE_ACTIONSW failure = {};
    failure.dwResetPeriod = 24 * 3600;
    failure.cActions = 3;
    failure.lpsaActions = actions;
    ChangeServiceConfig2W(svc, SERVICE_CONFIG_FAILURE_ACTIONS, &failure);

    // Give the service time to save its index when Windows shuts down.
    SERVICE_PRESHUTDOWN_INFO pre = {20000};
    ChangeServiceConfig2W(svc, SERVICE_CONFIG_PRESHUTDOWN_INFO, &pre);

    int rc = OK;
    if (!StartServiceW(svc, 0, nullptr) && GetLastError() != ERROR_SERVICE_ALREADY_RUNNING) {
        fail(L"The service was installed but could not start: " + errorText(GetLastError()));
        rc = ERR_START;
    } else if (!waitForState(svc, SERVICE_RUNNING, 15000)) {
        fail(L"The service was installed but did not start.");
        rc = ERR_START;
    } else {
        ok(L"Findex service installed and running (indexing " + drives + L")");
    }
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return rc;
}

int uninstall() {
    if (!isAdministrator()) {
        info(L"Removing the Findex service needs administrator rights…");
        const int rc = runElevated(L"--uninstall-service");
        if (rc == OK) ok(L"Findex service removed.");
        else if (rc == ERR_CANCELLED) fail(L"Cancelled: the administrator prompt was declined.");
        else fail(L"Could not remove the service (code " + std::to_wstring(rc) + L").");
        return rc;
    }

    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return ERR_SCM;
    SC_HANDLE svc = OpenServiceW(scm, NAME, SERVICE_STOP | SERVICE_QUERY_STATUS | DELETE);
    if (!svc) {
        const DWORD e = GetLastError();
        CloseServiceHandle(scm);
        if (e == ERROR_SERVICE_DOES_NOT_EXIST) {
            ok(L"The Findex service is not installed.");
            return OK;
        }
        fail(L"Could not open the service: " + errorText(e));
        return ERR_SCM;
    }
    stopService(svc);
    const bool deleted = DeleteService(svc) != FALSE;
    const DWORD del_err = GetLastError();
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    if (!deleted) {
        fail(L"Could not delete the service: " + errorText(del_err));
        return ERR_DELETE;
    }

    // Remove the installed copy (delayed to reboot if it's the one running).
    const std::wstring dir = programFilesDir();
    const std::wstring target = dir + L"\\findex.exe";
    if (!DeleteFileW(target.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND)
        MoveFileExW(target.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    RemoveDirectoryW(dir.c_str());

    // The service's saved index lives in SYSTEM's profile.
    wchar_t windir[MAX_PATH] = {};
    GetWindowsDirectoryW(windir, MAX_PATH);
    const std::wstring data = std::wstring(windir) + L"\\System32\\config\\systemprofile\\AppData\\Local\\Findex";
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW((data + L"\\index_*.dat*").c_str(), &fd);
    if (find != INVALID_HANDLE_VALUE) {
        do DeleteFileW((data + L"\\" + fd.cFileName).c_str());
        while (FindNextFileW(find, &fd));
        FindClose(find);
    }
    RemoveDirectoryW(data.c_str());

    ok(L"Findex service removed.");
    return OK;
}

int printStatus() {
    DWORD pid = 0;
    switch (query(&pid)) {
    case Status::NotInstalled:
        say(MUTED, L"○", L"Findex service: not installed");
        info(L"Install it with  findex --install-service  to skip the administrator prompt.");
        return 1;
    case Status::Running: say(GOOD, L"●", L"Findex service: running (pid " + std::to_wstring(pid) + L")"); break;
    case Status::Starting: say(WARN, L"●", L"Findex service: starting"); return 0;
    case Status::Stopping: say(WARN, L"●", L"Findex service: stopping"); return 0;
    case Status::Stopped: say(BAD, L"○", L"Findex service: stopped  (start it with: sc start Findex)"); return 1;
    case Status::Unknown: say(BAD, L"?", L"Findex service: could not query the service manager"); return 1;
    }
    return 0;
}

} // namespace service
