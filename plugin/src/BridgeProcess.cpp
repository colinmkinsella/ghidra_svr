#include "BridgeProcess.h"
#include <binaryninjaapi.h>
#include <sstream>
#include <stdexcept>
#include <vector>

#ifdef _WIN32
// ============================================================================
// Windows implementation
// ============================================================================

static std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring out(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), n);
    return out;
}

std::string BridgeProcess::buildClasspath(const std::string& bridgeJar,
                                          const std::string& ghidraHome) {
    static const char* kFramework[] = {
        "FileSystem", "DB", "Generic", "Utility", "SoftwareModeling", nullptr
    };
    std::string cp = bridgeJar;
    for (int i = 0; kFramework[i]; ++i) {
        cp += ';';
        cp += ghidraHome + "/Ghidra/Framework/" + kFramework[i] + "/lib/*";
    }
    // GhidraServer.jar contains server-side RMI stubs (e.g. RemoteOutputBlockStreamHandle)
    // that the client must have on its classpath to deserialise write-mode buffer handles.
    cp += ';';
    cp += ghidraHome + "/Ghidra/Features/GhidraServer/lib/*";
    return cp;
}

bool BridgeProcess::start(const std::string& javaExe,
                           const std::string& bridgeJar,
                           const std::string& ghidraHome,
                           bool trustAll,
                           std::string& errorOut) {
    stop();

    // ---- stdout pipe -------------------------------------------------------
    SECURITY_ATTRIBUTES sa{};
    sa.nLength              = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle       = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    HANDLE hRead = INVALID_HANDLE_VALUE, hWrite = INVALID_HANDLE_VALUE;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
        errorOut = "CreatePipe failed: " + std::to_string(GetLastError());
        return false;
    }
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    HANDLE hErrRead = INVALID_HANDLE_VALUE, hErrWrite = INVALID_HANDLE_VALUE;
    if (!CreatePipe(&hErrRead, &hErrWrite, &sa, 0)) {
        errorOut = "CreatePipe(stderr) failed: " + std::to_string(GetLastError());
        CloseHandle(hRead); CloseHandle(hWrite);
        return false;
    }
    SetHandleInformation(hErrRead, HANDLE_FLAG_INHERIT, 0);

    // ---- command line ------------------------------------------------------
    std::string exe  = javaExe.empty() ? "java" : javaExe;
    std::string cp   = buildClasspath(bridgeJar, ghidraHome);

    std::ostringstream cmd;
    cmd << '"' << exe << '"'
        << " -cp \"" << cp << "\""
        << " com.ghidra_svr.bridge.BridgeMain"
        << " --port 0";   // 0 = OS picks a free port
    if (trustAll) cmd << " --trust-all";

    std::wstring wCmd = widen(cmd.str());

    // ---- STARTUPINFO -------------------------------------------------------
    STARTUPINFOW si{};
    si.cb          = sizeof(STARTUPINFOW);
    si.dwFlags     = STARTF_USESTDHANDLES;
    si.hStdOutput  = hWrite;
    si.hStdError   = hErrWrite;
    si.hStdInput   = INVALID_HANDLE_VALUE;

    // ---- launch ------------------------------------------------------------
    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(
        nullptr, wCmd.data(),
        nullptr, nullptr,
        /*inheritHandles=*/TRUE,
        /*creationFlags=*/CREATE_NO_WINDOW,
        nullptr, nullptr,
        &si, &pi);

    CloseHandle(hWrite);
    CloseHandle(hErrWrite);

    if (!ok) {
        errorOut = "CreateProcessW failed: " + std::to_string(GetLastError());
        CloseHandle(hRead); CloseHandle(hErrRead);
        return false;
    }

    m_pi          = pi;
    m_hStdoutRead = hRead;
    m_hStderrRead = hErrRead;

    // ---- wait for READY ----------------------------------------------------
    if (!readReadyLine(errorOut)) {
        stop();
        return false;
    }

    // ---- start background thread to drain stderr into BN log ---------------
    startStderrLogger();
    return true;
}

void BridgeProcess::startStderrLogger() {
    if (m_hStderrRead == INVALID_HANDLE_VALUE) return;
    m_hStderrThread = CreateThread(nullptr, 0, stderrLoggerThread, this, 0, nullptr);
}

DWORD WINAPI BridgeProcess::stderrLoggerThread(LPVOID param) {
    auto* self = static_cast<BridgeProcess*>(param);
    char buf[4096];
    std::string line;
    DWORD bytesRead;
    while (ReadFile(self->m_hStderrRead, buf, sizeof(buf), &bytesRead, nullptr) && bytesRead > 0) {
        for (DWORD i = 0; i < bytesRead; ++i) {
            if (buf[i] == '\n') {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (!line.empty())
                    BinaryNinja::LogInfo("bridge: %s", line.c_str());
                line.clear();
            } else {
                line += buf[i];
            }
        }
    }
    if (!line.empty())
        BinaryNinja::LogInfo("bridge: %s", line.c_str());
    return 0;
}

bool BridgeProcess::readReadyLine(std::string& errorOut) {
    std::string line;
    char ch;
    DWORD bytesRead;

    while (true) {
        if (!ReadFile(m_hStdoutRead, &ch, 1, &bytesRead, nullptr) || bytesRead == 0) {
            // Drain stderr so we can report the JVM error.
            std::string stderrText;
            char buf[4096]; DWORD n = 0;
            while (PeekNamedPipe(m_hStderrRead, nullptr, 0, nullptr, &n, nullptr) && n > 0) {
                DWORD rd = 0;
                if (!ReadFile(m_hStderrRead, buf, std::min(n, (DWORD)sizeof(buf)), &rd, nullptr) || rd == 0) break;
                stderrText.append(buf, rd);
            }
            errorOut = "Bridge process exited before sending READY";
            if (!stderrText.empty()) errorOut += ":\n" + stderrText;
            return false;
        }
        if (ch == '\n') {
            // Remove trailing CR if present.
            if (!line.empty() && line.back() == '\r') line.pop_back();

            static const char kPrefix[] = "READY port=";
            if (line.compare(0, sizeof(kPrefix) - 1, kPrefix) == 0) {
                m_port = std::stoi(line.substr(sizeof(kPrefix) - 1));
                BinaryNinja::LogInfo("bridge ready: %s", line.c_str());
                return true;
            }
            // Any other stdout lines are informational.
            BinaryNinja::LogInfo("bridge stdout: %s", line.c_str());
            line.clear();
        } else {
            line += ch;
        }
    }
}

void BridgeProcess::stop() {
    if (m_pi.hProcess != nullptr) {
        TerminateProcess(m_pi.hProcess, 0);
        WaitForSingleObject(m_pi.hProcess, 2000);
        CloseHandle(m_pi.hProcess);
        CloseHandle(m_pi.hThread);
        m_pi = {};
    }
    // Close stderr read-end first so the logger thread unblocks from ReadFile.
    if (m_hStderrRead != INVALID_HANDLE_VALUE) {
        CloseHandle(m_hStderrRead);
        m_hStderrRead = INVALID_HANDLE_VALUE;
    }
    if (m_hStderrThread != INVALID_HANDLE_VALUE) {
        WaitForSingleObject(m_hStderrThread, 2000);
        CloseHandle(m_hStderrThread);
        m_hStderrThread = INVALID_HANDLE_VALUE;
    }
    if (m_hStdoutRead != INVALID_HANDLE_VALUE) {
        CloseHandle(m_hStdoutRead);
        m_hStdoutRead = INVALID_HANDLE_VALUE;
    }
    m_port = -1;
}

bool BridgeProcess::isRunning() const {
    if (m_pi.hProcess == nullptr) return false;
    DWORD exitCode = 0;
    if (!GetExitCodeProcess(m_pi.hProcess, &exitCode)) return false;
    return exitCode == STILL_ACTIVE;
}

#else
// ============================================================================
// POSIX implementation (Linux / macOS)
// ============================================================================
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

std::string BridgeProcess::buildClasspath(const std::string& bridgeJar,
                                          const std::string& ghidraHome) {
    static const char* kFramework[] = {
        "FileSystem", "DB", "Generic", "Utility", "SoftwareModeling", nullptr
    };
    std::string cp = bridgeJar;
    for (int i = 0; kFramework[i]; ++i) {
        cp += ':';
        cp += ghidraHome + "/Ghidra/Framework/" + kFramework[i] + "/lib/*";
    }
    cp += ':';
    cp += ghidraHome + "/Ghidra/Features/GhidraServer/lib/*";
    return cp;
}

bool BridgeProcess::start(const std::string& javaExe,
                           const std::string& bridgeJar,
                           const std::string& ghidraHome,
                           bool trustAll,
                           std::string& errorOut) {
    stop();

    int pipefd[2];
    if (pipe(pipefd) != 0) {
        errorOut = std::string("pipe() failed: ") + strerror(errno);
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        errorOut = std::string("fork() failed: ") + strerror(errno);
        close(pipefd[0]);
        close(pipefd[1]);
        return false;
    }

    if (pid == 0) {
        // Child: redirect stdout to write end of pipe.
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);

        std::string exe = javaExe.empty() ? "java" : javaExe;
        std::string cp  = buildClasspath(bridgeJar, ghidraHome);

        std::vector<std::string> argStrs = {
            exe, "-cp", cp, "com.ghidra_svr.bridge.BridgeMain", "--port", "0"
        };
        if (trustAll) argStrs.push_back("--trust-all");

        std::vector<char*> argv;
        for (auto& s : argStrs) argv.push_back(s.data());
        argv.push_back(nullptr);

        execvp(argv[0], argv.data());
        _exit(127); // execvp failed
    }

    // Parent: close write end.
    close(pipefd[1]);
    m_pid    = pid;
    m_pipeFd = pipefd[0];

    if (!readReadyLine(errorOut)) {
        stop();
        return false;
    }
    return true;
}

bool BridgeProcess::readReadyLine(std::string& errorOut) {
    std::string line;
    char ch;
    while (true) {
        ssize_t n = read(m_pipeFd, &ch, 1);
        if (n <= 0) {
            errorOut = "Bridge process exited before sending READY";
            return false;
        }
        if (ch == '\n') {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            static const char kPrefix[] = "READY port=";
            if (line.compare(0, sizeof(kPrefix) - 1, kPrefix) == 0) {
                m_port = std::stoi(line.substr(sizeof(kPrefix) - 1));
                return true;
            }
            line.clear();
        } else {
            line += ch;
        }
    }
}

void BridgeProcess::stop() {
    if (m_pid > 0) {
        kill(m_pid, SIGTERM);
        waitpid(m_pid, nullptr, 0);
        m_pid = -1;
    }
    if (m_pipeFd >= 0) {
        close(m_pipeFd);
        m_pipeFd = -1;
    }
    m_port = -1;
}

bool BridgeProcess::isRunning() const {
    if (m_pid <= 0) return false;
    return waitpid(m_pid, nullptr, WNOHANG) == 0;
}

#endif // _WIN32
