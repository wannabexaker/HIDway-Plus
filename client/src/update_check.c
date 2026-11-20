/* One read-only update check per start; see update_check.h. */
#define WIN32_LEAN_AND_MEAN
#include "update_check.h"
#include <string.h>
#include <wchar.h>

static char result_tag[HW_UPDATE_TAG_MAX];
static HWND result_window;
static UINT result_message;
static volatile LONG started;

int hw_update_tag_valid(const char *tag)
{
    if (!tag || tag[0] != 'v')
        return 0;
    const char *p = tag + 1;
    for (int part = 0; part < 3; part++) {
        int digits = 0;
        while (*p >= '0' && *p <= '9') {
            if (++digits > 6)
                return 0;
            p++;
        }
        if (!digits)
            return 0;
        if (part < 2 && *p++ != '.')
            return 0;
    }
    return *p == 0;
}

int hw_update_parse(const char *output, size_t length, char *tag, size_t tag_cap)
{
    static const char prefix[] = "HIDWAY-UPDATE state=";
    char line[HW_UPDATE_OUTPUT_MAX];
    if (tag && tag_cap)
        tag[0] = 0;
    if (!output || length >= sizeof line)
        return HW_UPDATE_FAILED;
    if (length && output[length - 1] == '\n')
        length--;
    if (length && output[length - 1] == '\r')
        length--;
    memcpy(line, output, length);
    line[length] = 0;
    /* one line of printable ASCII: no NUL, CR, LF or control bytes inside */
    for (size_t i = 0; i < length; i++)
        if ((unsigned char)line[i] < 0x20 || (unsigned char)line[i] > 0x7e)
            return HW_UPDATE_FAILED;
    if (strncmp(line, prefix, sizeof prefix - 1))
        return HW_UPDATE_FAILED;
    char *state = line + sizeof prefix - 1, *space = strchr(state, ' ');
    if (!space || strncmp(space, " tag=", 5))
        return HW_UPDATE_FAILED;
    *space = 0;
    const char *value = space + 5;
    if (strchr(value, ' '))
        return HW_UPDATE_FAILED;
    if (!strcmp(state, "Available")) {
        if (!hw_update_tag_valid(value) || !tag || strlen(value) >= tag_cap)
            return HW_UPDATE_FAILED;
        strcpy(tag, value);
        return HW_UPDATE_AVAILABLE;
    }
    if (value[0] && !hw_update_tag_valid(value))
        return HW_UPDATE_FAILED;
    if (!strcmp(state, "Current") || !strcmp(state, "None") || !strcmp(state, "Older") ||
        !strcmp(state, "Preview"))
        return HW_UPDATE_SILENT;
    return HW_UPDATE_FAILED; /* Error, RateLimited and anything unknown */
}

/* This executable's folder; 0 when the path does not fit. */
static int exe_folder(wchar_t *folder, size_t cap)
{
    DWORD n = GetModuleFileNameW(NULL, folder, (DWORD)cap);
    if (!n || n >= cap)
        return 0;
    wchar_t *slash = wcsrchr(folder, L'\\');
    if (!slash)
        return 0;
    *slash = 0;
    return 1;
}

static int regular_file(const wchar_t *folder, const wchar_t *name)
{
    wchar_t path[2048];
    if (wcslen(folder) + wcslen(name) + 2 > 2048)
        return 0;
    swprintf(path, 2048, L"%ls\\%ls", folder, name);
    DWORD a = GetFileAttributesW(path);
    return a != INVALID_FILE_ATTRIBUTES && !(a & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT));
}

int hw_update_scripts_present(void)
{
    wchar_t folder[2048];
    return exe_folder(folder, 2048) && regular_file(folder, L"hidway-updates.ps1") &&
           regular_file(folder, L"hidway-updater.psm1");
}

/* Runs the check; returns HW_UPDATE_* and fills result_tag. */
static int run_check(void)
{
    wchar_t folder[2048], powershell[2048], command[4400];
    if (!exe_folder(folder, 2048))
        return HW_UPDATE_FAILED;
    UINT n = GetSystemDirectoryW(powershell, 1800);
    if (!n || n >= 1800)
        return HW_UPDATE_FAILED;
    wcscat(powershell, L"\\WindowsPowerShell\\v1.0\\powershell.exe");
    swprintf(command, 4400,
             L"\"%ls\" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"%ls\\hidway-updates.ps1\" -Check",
             powershell, folder);

    SECURITY_ATTRIBUTES inherit = {sizeof inherit, NULL, TRUE};
    HANDLE read_end = NULL, write_end = NULL;
    if (!CreatePipe(&read_end, &write_end, &inherit, 0))
        return HW_UPDATE_FAILED;
    SetHandleInformation(read_end, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit,
                             OPEN_EXISTING, 0, NULL);
    int result = HW_UPDATE_FAILED;
    HANDLE job = NULL;
    LPPROC_THREAD_ATTRIBUTE_LIST attributes = NULL;
    PROCESS_INFORMATION process = {0};
    if (nul == INVALID_HANDLE_VALUE)
        goto done;

    /* Only the pipe and NUL are inherited, not the client's socket or files. */
    SIZE_T size = 0;
    InitializeProcThreadAttributeList(NULL, 1, 0, &size);
    attributes = (LPPROC_THREAD_ATTRIBUTE_LIST)HeapAlloc(GetProcessHeap(), 0, size);
    HANDLE inherited[2] = {write_end, nul};
    if (!attributes || !InitializeProcThreadAttributeList(attributes, 1, 0, &size)) {
        if (attributes)
            HeapFree(GetProcessHeap(), 0, attributes);
        attributes = NULL;
        goto done;
    }
    if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof inherited,
                                   NULL, NULL))
        goto done;
    STARTUPINFOEXW startup;
    memset(&startup, 0, sizeof startup);
    startup.StartupInfo.cb = sizeof startup;
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = nul;
    startup.StartupInfo.hStdOutput = write_end;
    startup.StartupInfo.hStdError = nul;
    startup.lpAttributeList = attributes;
    if (!CreateProcessW(powershell, command, NULL, NULL, TRUE,
                        CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT, NULL, folder,
                        &startup.StartupInfo, &process))
        goto done;
    /* The child ends with the client: closing the last job handle kills it. */
    job = CreateJobObjectW(NULL, NULL);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit;
        memset(&limit, 0, sizeof limit);
        limit.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limit, sizeof limit) ||
            !AssignProcessToJobObject(job, process.hProcess)) {
            CloseHandle(job);
            job = NULL;
        }
    }
    ResumeThread(process.hThread);
    CloseHandle(write_end);
    write_end = NULL;

    char output[HW_UPDATE_OUTPUT_MAX];
    size_t length = 0;
    int overflow = 0, exited = 0;
    DWORD begin = GetTickCount();
    for (;;) {
        DWORD available = 0;
        if (PeekNamedPipe(read_end, NULL, 0, NULL, &available, NULL) && available) {
            char chunk[256];
            DWORD got = 0;
            if (!ReadFile(read_end, chunk, available < sizeof chunk ? available : sizeof chunk, &got, NULL))
                break;
            if (length + got >= sizeof output) {
                overflow = 1;
                break;
            }
            memcpy(output + length, chunk, got);
            length += got;
            continue;
        }
        if (exited)
            break; /* exited and the pipe is empty */
        if (WaitForSingleObject(process.hProcess, 50) == WAIT_OBJECT_0) {
            exited = 1; /* one more pass reads what is still buffered */
            continue;
        }
        if (GetTickCount() - begin >= HW_UPDATE_TIMEOUT_MS)
            break;
    }
    DWORD code = 1;
    if (!exited || overflow) {
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, 2000);
    } else if (GetExitCodeProcess(process.hProcess, &code) && code == 0) {
        result = hw_update_parse(output, length, result_tag, sizeof result_tag);
    }

done:
    if (process.hThread)
        CloseHandle(process.hThread);
    if (process.hProcess)
        CloseHandle(process.hProcess);
    if (job)
        CloseHandle(job);
    if (attributes) {
        DeleteProcThreadAttributeList(attributes);
        HeapFree(GetProcessHeap(), 0, attributes);
    }
    if (nul != INVALID_HANDLE_VALUE)
        CloseHandle(nul);
    if (write_end)
        CloseHandle(write_end);
    CloseHandle(read_end);
    if (result != HW_UPDATE_AVAILABLE)
        result_tag[0] = 0;
    return result;
}

static DWORD WINAPI worker(LPVOID unused)
{
    (void)unused;
    int result = run_check();
    PostMessageW(result_window, result_message, (WPARAM)result, 0);
    return 0;
}

int hw_update_check_begin(HWND window, UINT message)
{
    if (!window || !hw_update_scripts_present() || InterlockedExchange(&started, 1))
        return 0;
    result_window = window;
    result_message = message;
    /* Below normal priority: the check must never compete with input. */
    HANDLE thread = CreateThread(NULL, 0, worker, NULL, CREATE_SUSPENDED, NULL);
    if (!thread)
        return 0;
    SetThreadPriority(thread, THREAD_PRIORITY_BELOW_NORMAL);
    ResumeThread(thread);
    CloseHandle(thread);
    return 1;
}

const char *hw_update_check_tag(void)
{
    return result_tag;
}
