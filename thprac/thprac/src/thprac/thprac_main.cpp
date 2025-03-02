#include "thprac_main.h"
#include "thprac_games.h"
#include "thprac_gui_locale.h"
#include "thprac_launcher_wnd.h"
#include "thprac_launcher_games.h"
#include "thprac_launcher_utils.h"
#include "thprac_launcher_cfg.h"
#include "thprac_load_exe.h"
#include <Windows.h>
#include <algorithm>
#include <metrohash128.h>
#include <psapi.h>
#include <string>
#include <tlhelp32.h>

#pragma comment(lib, "psapi.lib")

namespace THPrac {
enum thprac_prompt_t {
    PR_FAILED,
    PR_INFO_ATTACHED,
    PR_INFO_NO_GAME_FOUND,
    PR_ASK_IF_ATTACH,
    PR_ASK_IF_CONTINUE,
    PR_ASK_USE_VPATCH,
    PR_ERR_NO_GAME_FOUND,
    PR_ERR_ATTACH_FAILED,
    PR_ERR_RUN_FAILED,
};

bool CheckMutex(const char* mutex_name)
{
    auto result = OpenMutexA(SYNCHRONIZE, FALSE, mutex_name);
    if (result) {
        CloseHandle(result);
        return true;
    }
    return false;
}

bool CheckIfAnyGame()
{
    if (CheckMutex("Touhou Koumakyou App") || CheckMutex("Touhou YouYouMu App") || CheckMutex("Touhou 08 App") || CheckMutex("Touhou 10 App") || CheckMutex("Touhou 11 App") || CheckMutex("Touhou 12 App") || CheckMutex("th17 App") || CheckMutex("th18 App"))
        return true;
    return false;
}

int MsgBox(const char* title, const char* text, int flag)
{
    auto titleU16 = utf8_to_utf16(title);
    auto textU16 = utf8_to_utf16(text);
    return MessageBoxW(NULL, textU16.c_str(), titleU16.c_str(), flag);
}

int MsgBoxWnd(const char* title, const char* text, int flag)
{
    auto titleU16 = utf8_to_utf16(title);
    auto textU16 = utf8_to_utf16(text);
    return LauncherWndMsgBox(titleU16.c_str(), textU16.c_str(), flag);
}

bool PromptUser(thprac_prompt_t info, THGameSig* gameSig = nullptr)
{
    switch (info) {
    case THPrac::PR_FAILED:
        return true;
    case THPrac::PR_INFO_ATTACHED:
        MsgBox(XSTR(THPRAC_PR_COMPLETE), XSTR(THPRAC_PR_INFO_ATTACHED), MB_ICONASTERISK | MB_OK | MB_SYSTEMMODAL);
        return true;
    case THPrac::PR_INFO_NO_GAME_FOUND:
        MsgBoxWnd(XSTR(THPRAC_PR_COMPLETE), XSTR(THPRAC_PR_ERR_NO_GAME), MB_ICONASTERISK | MB_OK | MB_SYSTEMMODAL);
        return true;
    case THPrac::PR_ASK_IF_ATTACH: {
        if (!gameSig) {
            return false;
        }
        char gameExeStr[256];
        sprintf_s(gameExeStr, XSTR(THPRAC_PR_ASK_ATTACH), gameSig->idStr);
        if (MsgBox(XSTR(THPRAC_PR_APPLY), gameExeStr, MB_YESNO | MB_ICONQUESTION | MB_SYSTEMMODAL) == IDYES) {
            return true;
        }
        return false;
    }
    case THPrac::PR_ASK_IF_CONTINUE:
        if (MsgBox(XSTR(THPRAC_PR_CONTINUE), XSTR(THPRAC_PR_ASK_CONTINUE), MB_YESNO | MB_ICONQUESTION | MB_SYSTEMMODAL) == IDYES) {
            return true;
        }
        return false;
    case THPrac::PR_ERR_NO_GAME_FOUND:
        MsgBox(XSTR(THPRAC_PR_ERROR), XSTR(THPRAC_PR_ERR_NO_GAME), MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
        return true;
    case THPrac::PR_ERR_ATTACH_FAILED:
        MsgBox(XSTR(THPRAC_PR_ERROR), XSTR(THPRAC_PR_ERR_ATTACH), MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
        return true;
    case THPrac::PR_ERR_RUN_FAILED:
        MsgBox(XSTR(THPRAC_PR_ERROR), XSTR(THPRAC_PR_ERR_RUN), MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
        return true;
    default:
        break;
    }

    return false;
}

THGameSig* CheckOngoingGame(PROCESSENTRY32& proc)
{
    // Eliminate impossible process
    if (strcmp("ûg½.exe", proc.szExeFile) && strcmp("|·½¼tÄ§à_.exe", proc.szExeFile)) {
        if (proc.szExeFile[0] != 't' || proc.szExeFile[1] != 'h')
            return nullptr;
        if (proc.szExeFile[2] < 0x30 || proc.szExeFile[2] > 0x39)
            return nullptr;
        if (proc.szExeFile[3] < 0x30 || proc.szExeFile[3] > 0x39)
            return nullptr;
        if (proc.szExeFile[4] == '.') {
            if (*(int32_t*)(&proc.szExeFile[5]) != 0x00657865)
                return nullptr;
        } else if (proc.szExeFile[4] >= 0x30 && proc.szExeFile[4] <= 0x39) {
            if (proc.szExeFile[5] != '.')
                return nullptr;
            if (*(int32_t*)(&proc.szExeFile[6]) != 0x00657865)
                return nullptr;
        } else {
            return nullptr;
        }
    }

    // Open the related process
    auto hProc = OpenProcess(
        //PROCESS_SUSPEND_RESUME |
        PROCESS_QUERY_INFORMATION | PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE,
        FALSE,
        proc.th32ProcessID);
    if (!hProc)
        return nullptr;

    // Check THPrac signature
    DWORD sigAddr = 0;
    DWORD sigCheck = 0;
    DWORD bytesReadRPM;
    ReadProcessMemory(hProc, (void*)0x40003c, &sigAddr, 4, &bytesReadRPM);
    if (bytesReadRPM != 4 || !sigAddr) {
        CloseHandle(hProc);
        return nullptr;
    }
    ReadProcessMemory(hProc, (void*)(0x400000 + sigAddr - 4), &sigCheck, 4, &bytesReadRPM);
    if (bytesReadRPM != 4 || sigCheck) {
        CloseHandle(hProc);
        return nullptr;
    }

    ExeSig sig;
    if (GetExeInfoEx((size_t)hProc, sig)) {
        for (auto& gameDef : gGameDefs) {
            if (gameDef.catagory != CAT_MAIN && gameDef.catagory != CAT_SPINOFF_STG) {
                continue;
            }
            if (gameDef.exeSig.textSize != sig.textSize || gameDef.exeSig.timeStamp != sig.timeStamp) {
                continue;
            }
            return &gameDef;
        }
    }
    CloseHandle(hProc);
    return nullptr;
}

bool WriteTHPracSig(HANDLE hProc)
{
    DWORD sigAddr = 0;
    DWORD bytesReadRPM;
    ReadProcessMemory(hProc, (void*)0x40003c, &sigAddr, 4, &bytesReadRPM);
    if (bytesReadRPM != 4 || !sigAddr)
        return false;
    sigAddr += 0x400000;
    sigAddr -= 4;

    constexpr DWORD thpracSig = 'CARP';
    DWORD bytesWrote;
    DWORD oldProtect;
    if (!VirtualProtectEx(hProc, (void*)sigAddr, 4, PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;
    if (!WriteProcessMemory(hProc, (void*)sigAddr, &thpracSig, 4, &bytesWrote))
        return false;
    if (!VirtualProtectEx(hProc, (void*)sigAddr, 4, oldProtect, &oldProtect))
        return false;

    return true;
}

bool ApplyTHPracToProc(PROCESSENTRY32& proc)
{
    // Open the related process
    auto hProc = OpenProcess(
        //PROCESS_SUSPEND_RESUME |
        PROCESS_QUERY_INFORMATION | PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE,
        FALSE,
        proc.th32ProcessID);
    if (!hProc)
        return false;

    auto result = (WriteTHPracSig(hProc) && THPrac::LoadSelf(hProc));
    CloseHandle(hProc);

    return result;
}

bool CheckIfGameExistEx(THGameSig& gameSig, const wchar_t* name)
{
    bool result = false;
    HANDLE hFile = INVALID_HANDLE_VALUE;
    DWORD fileSize = 0;
    HANDLE hFileMap = NULL;
    void* pFileMapView = nullptr;

    // Open the file.
    hFile = CreateFileW(name, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        goto end;
    fileSize = GetFileSize(hFile, NULL);
    if (fileSize > (1 << 23))
        goto end; // Pass if the file is too large.
    hFileMap = CreateFileMappingA(hFile, NULL, PAGE_READONLY, 0, fileSize, NULL);
    if (!hFileMap)
        goto end;
    pFileMapView = MapViewOfFile(hFileMap, FILE_MAP_READ, 0, 0, fileSize);
    if (!pFileMapView)
        goto end;

    ExeSig exeSig;
    GetExeInfo(pFileMapView, fileSize, exeSig);
    for (int i = 0; i < 4; ++i) {
        if (exeSig.metroHash[i] != gameSig.exeSig.metroHash[i]) {
            goto end;
        }
    }
    result = true;

end:
    if (pFileMapView)
        UnmapViewOfFile(pFileMapView);
    if (hFileMap)
        CloseHandle(hFileMap);
    if (hFile != INVALID_HANDLE_VALUE)
        CloseHandle(hFile);
    return result;
}

bool CheckIfGameExist(THGameSig& gameSig, std::wstring& name)
{
    if (!strcmp(gameSig.idStr, "th06")) {
        if (CheckIfGameExistEx(gameSig, L"ûg½.exe")) {
            name = L"ûg½.exe";
            return true;
        } else if (CheckIfGameExistEx(gameSig, L"|·½¼tÄ§à_.exe")) {
            name = L"|·½¼tÄ§à_.exe";
            return true;
        }
    }
    std::wstring n = utf8_to_utf16(gameSig.idStr);
    n += L".exe";
    if (CheckIfGameExistEx(gameSig, n.c_str())) {
        name = n;
        return true;
    }
    return false;
}

bool CheckVpatch(THGameSig& gameSig)
{
    auto attr = GetFileAttributesW(gameSig.vPatchStr);
    if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY))
        return false;
    return true;
}

bool RunGameReflective(THGameSig& gameSig, std::wstring& name, bool useVpatch)
{
    HINSTANCE executeResult = (HINSTANCE)100;

    if (useVpatch) {
        executeResult = ShellExecuteW(NULL, L"open", L"vpatch.exe", NULL, NULL, SW_SHOW);
    } else {
        executeResult = ShellExecuteW(NULL, L"open", name.c_str(), NULL, NULL, SW_SHOW);
    }

    if (executeResult <= (HINSTANCE)32) {
        return false;
    }

    if ((gameSig.catagory == CAT_MAIN || gameSig.catagory == CAT_SPINOFF_STG) && useVpatch) {
        for (int i = 0; i < 20; ++i) {
            if (CheckIfAnyGame()) {
                THGameSig* gameSig = nullptr;
                PROCESSENTRY32 entry;
                entry.dwSize = sizeof(PROCESSENTRY32);
                HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);
                if (Process32First(snapshot, &entry)) {
                    do {
                        gameSig = CheckOngoingGame(entry);
                        if (gameSig) {
                            if (ApplyTHPracToProc(entry)) {
                                CloseHandle(snapshot);
                                return true;
                            } else {
                                PromptUser(PR_ERR_ATTACH_FAILED);
                                CloseHandle(snapshot);
                                return true;
                            }
                        }
                    } while (Process32Next(snapshot, &entry));
                }
            }
            Sleep(500);
        }
    }

    return true;
}

bool RunGameWithTHPrac(THGameSig& gameSig, std::wstring& name)
{
    std::string vPatchName = utf16_to_utf8(gameSig.vPatchStr);
    auto vp = LoadLibraryA(vPatchName.c_str());
    auto isVpatchValid = GetProcAddress(vp, "_Initialize@4");
    bool useReflectiveLaunch = false;
    LauncherSettingGet("reflective_launch", useReflectiveLaunch);
    if (useReflectiveLaunch) {
        return RunGameReflective(gameSig, name, isVpatchValid);
    }

    STARTUPINFOW startup_info;
    PROCESS_INFORMATION proc_info;
    memset(&startup_info, 0, sizeof(startup_info));
    startup_info.cb = sizeof(startup_info);
    CreateProcessW(name.c_str(), NULL, NULL, NULL, NULL, CREATE_SUSPENDED, NULL, NULL, &startup_info, &proc_info);

    if (isVpatchValid) {
        auto vpNameLength = strlen(vPatchName.c_str()) + 1;
        auto pLoadLibrary = GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA");
        auto remoteStr = VirtualAllocEx(proc_info.hProcess, NULL, vpNameLength, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        WriteProcessMemory(proc_info.hProcess, remoteStr, vPatchName.c_str(), vpNameLength, NULL);
        auto t = CreateRemoteThread(proc_info.hProcess, NULL, 0, (LPTHREAD_START_ROUTINE)pLoadLibrary, remoteStr, 0, NULL);
        WaitForSingleObject(t, INFINITE);
        VirtualFreeEx(proc_info.hProcess, remoteStr, 0, MEM_RELEASE);
    }

    auto result = (WriteTHPracSig(proc_info.hProcess) && THPrac::LoadSelf(proc_info.hProcess));
    if (!result)
        TerminateThread(proc_info.hThread, -1);
    else
        ResumeThread(proc_info.hThread);

    CloseHandle(proc_info.hThread);
    CloseHandle(proc_info.hProcess);
    return result;
}

bool FindOngoingGame(bool prompt)
{
    bool hasPrompted = false;

    if (CheckIfAnyGame()) {
        THGameSig* gameSig = nullptr;
        PROCESSENTRY32 entry;
        entry.dwSize = sizeof(PROCESSENTRY32);
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);
        if (Process32First(snapshot, &entry)) {
            do {
                gameSig = CheckOngoingGame(entry);
                if (gameSig) {
                    hasPrompted = true;
                    if (PromptUser(PR_ASK_IF_ATTACH, gameSig)) {
                        if (ApplyTHPracToProc(entry)) {
                            PromptUser(PR_INFO_ATTACHED);
                            CloseHandle(snapshot);
                            return true;
                        } else {
                            PromptUser(PR_ERR_ATTACH_FAILED);
                            CloseHandle(snapshot);
                            return true;
                        }
                    }
                }
            } while (Process32Next(snapshot, &entry));
        }
    }

    if (prompt && !hasPrompted) {
        PromptUser(PR_INFO_NO_GAME_FOUND);
    }

    return false;
}

bool FindAndRunGame(bool prompt)
{
    std::wstring name;
    for (auto& sig : gGameDefs) {
        if (CheckIfGameExist(sig, name)) {
            if (prompt) {
                char gameExeStr[256];
                sprintf_s(gameExeStr, XSTR(THPRAC_EXISTING_GAME_CONFIRMATION), sig.idStr);
                auto msgBoxResult = MsgBox(XSTR(THPRAC_EXISTING_GAME_CONFIRMATION_TITLE), gameExeStr, MB_YESNOCANCEL | MB_ICONINFORMATION | MB_SYSTEMMODAL);
                if (msgBoxResult == IDNO) {
                    return false;
                } else if (msgBoxResult != IDYES) {
                    return true;
                }
            }
            if (CheckIfAnyGame()) {
                if (!PromptUser(PR_ASK_IF_CONTINUE))
                    return true;
            }

            if (RunGameWithTHPrac(sig, name)) {
                return true;
            } else {
                PromptUser(PR_FAILED);
                return true;
            }
        }
    }
    return false;
}
}