#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <sstream>
#include "StringUtils.h"

#ifdef _WIN32
    #include <windows.h>
    #include <ShlObj.h>
    #include "SafeHandle.h"
#elif __linux__
    #include <dlfcn.h>
    #include <fstream>
    #include <memory>
    #include <unistd.h>
#endif

// Enable if the launcher should convert the path of the launch.lua to a "short" windows path.
// This caused an issue with how SimpleGraphic.dll compares the path to its own, so settings and builds weren't found,
// so it's disabled for now.
constexpr auto USE_SHORT_PATHS = false;

#ifdef _WIN32
std::vector<std::wstring> ParseCommandLine()
{
    std::vector<std::wstring> commandLine;
    int dwNumArgs = 0;
    LPWSTR *wszCommandLineParams = CommandLineToArgvW(GetCommandLineW(), &dwNumArgs);
    if (wszCommandLineParams != nullptr)
    {
        commandLine.reserve(dwNumArgs);
        for (int dwArg = 0; dwArg < dwNumArgs; dwArg++)
        {
            commandLine.push_back(wszCommandLineParams[dwArg]);
        }
        LocalFree(wszCommandLineParams);
    }
    return commandLine;
}
#elif __linux__
std::vector<std::wstring> ParseCommandLine(int argc, char** argv)
{
    std::vector<std::wstring> commandLine;
    if (argv != nullptr)
    {
        commandLine.reserve(argc);
        for (int arg = 0; arg < argc; arg++)
        {
            std::string str(argv[arg]);
            std::wstring ws(str.begin(), str.end());
            commandLine.push_back(ws);
        }
    }
    return commandLine;
}
#endif

std::string ConvertToUTF8(std::wstring param)
{
#ifdef _WIN32
    int dwUTF8Size = WideCharToMultiByte(CP_UTF8, 0, param.c_str(), (int)param.size(), NULL, 0, NULL, NULL);
    std::string value(dwUTF8Size, 0);
    WideCharToMultiByte(CP_UTF8, 0, param.c_str(), (int)param.size(), value.data(), dwUTF8Size, NULL, NULL);
#elif __linux__
    int dwUTF8Size = std::wcstombs(nullptr, param.c_str(), 0);
    std::string value(dwUTF8Size, 0);
    std::wcstombs(value.data(), param.c_str(), dwUTF8Size);
#endif

    return value;
}

bool ReadHeaderFromFile(const std::wstring &path, char* szHeader, int headerSize)
{
#ifdef _WIN32
    // Open the file (SafeHandle will close the file when exiting scope).
    SafeHandle hFile = CreateFile(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (!hFile.IsValid())
    {
        return false;
    }

    DWORD dwBytesRead = 0;
    if (!ReadFile(hFile.Get(), szHeader, headerSize - 1, &dwBytesRead, nullptr))
    {
        return false;
    }
    szHeader[dwBytesRead] = 0;
    hFile.Close();
#elif __linux__
    std::string pathString = ConvertToUTF8(path);
    for (size_t i = 0; i < pathString.length(); i++)
    {
        if (pathString[i] == '\\')
        {
            pathString[i] = '/';
        }
    }

    std::ifstream file(pathString.c_str());
    if (!file.is_open())
    {
        return false;
    }

    auto bytesRead = file.readsome(szHeader, headerSize - 1);
    if (bytesRead < 0)
    {
        return false;
    }

    szHeader[bytesRead] = 0;
    file.close();
#endif

    return true;
}

bool IsValidLuaFile(const std::wstring &path, std::string &firstLine)
{
    // Read the first 255 bytes of the file, which should be enough to encompass the first line
    const int headerSize = 256;
    char szHeader[headerSize]{};
    bool success = ReadHeaderFromFile(path, szHeader, headerSize);

    // Check for UTF8 BOM
    char *szHeaderCur = szHeader;
    if (szHeaderCur[0] == 0xEF && szHeaderCur[1] == 0xBB && szHeaderCur[2] == 0xBF)
    {
        szHeaderCur += 3;
    }

    // Launcher lua files must start with a #@ directive which determines the dll to launch.
    if (szHeaderCur[0] != '#' || szHeaderCur[1] != '@')
    {
        return false;
    }

    // Find the end of the line
    char *szNewLinePos = strchr(szHeaderCur, '\n');
    if (szNewLinePos == nullptr)
    {
        return false;
    }
    // Trim any whitespace at the end of the line.
    while (*szNewLinePos == '\n' || *szNewLinePos == '\r' || isspace(*szNewLinePos))
    {
        szNewLinePos--;
    }

    firstLine = std::string(szHeaderCur, szNewLinePos + 1);
    return true;
}

bool InsertPath(std::vector<std::wstring> &commandLine, std::wstring &path)
{
#ifdef _WIN32
    if constexpr (USE_SHORT_PATHS)
    {
        DWORD requiredLength = GetShortPathName(path.c_str(), nullptr, 0);
        if (requiredLength == 0)
        {
            return false;
        }

        std::wstring shortPath(requiredLength, L'\0');
        requiredLength = GetShortPathName(path.c_str(), shortPath.data(), requiredLength);
        if (requiredLength == 0)
        {
            return false;
        }

        commandLine.insert(commandLine.begin() + 1, shortPath);
    }
    else
#endif
    {
#ifndef _WIN32
        for (size_t i = 0; i < path.length(); i++)
        {
            if (path[i] == L'\\')
            {
                path[i] = L'/';
            }
        }
#endif
        commandLine.insert(commandLine.begin() + 1, path);
    }
    return true;
}

bool FindLaunchLua(std::wstring basePath, std::vector<std::wstring> &commandLine, std::string &firstLine)
{
    // Unify path separator characters
    for (size_t i = 0; i < basePath.length(); i++)
    {
        if (basePath[i] == L'/')
        {
            basePath[i] = L'\\';
        }
    }

    // Remove the trailing slash if it exists
    if (basePath[basePath.size() - 1] == L'\\')
    {
        basePath = basePath.substr(0, basePath.size() - 1);
    }

    // Look for Launch.lua directly in the base path
    std::wstring launchPath = basePath + L"\\Launch.lua";
    if (IsValidLuaFile(launchPath, firstLine))
    {
        return InsertPath(commandLine, launchPath);
    }

    // Look for src\\Launch.lua
    launchPath = basePath + L"\\src\\Launch.lua";
    if (IsValidLuaFile(launchPath, firstLine))
    {
        return InsertPath(commandLine, launchPath);
    }

    // If the base path ends with "runtime" then strip that off, append "src" and look for Launch.lua there
    static const std::wstring runtime = L"runtime";
    if (basePath.length() > runtime.length() + 1)
    {
        // Find the last slash
        const size_t lastSlash = basePath.find_last_of(L'\\');
        if (lastSlash != std::wstring::npos)
        {
            // Extract the full subdirectory name
            std::wstring subDir = basePath.substr(lastSlash + 1);
            for (size_t i = 0; i < subDir.size(); i++)
            {
                subDir[i] = towlower(subDir[i]);
            }
            if (subDir == runtime)
            {
                std::wstring parentPath = basePath.substr(0, lastSlash);
                launchPath = parentPath + L"\\src\\Launch.lua";
                if (IsValidLuaFile(launchPath, firstLine))
                {
                    return InsertPath(commandLine, launchPath);
                }
            }
        }
    }
    return false;
}

bool InsertLaunchLua(std::vector<std::wstring> &commandLine, std::string &firstLine)
{
    // Determine if the first command-line parameter is the location of a valid launcher lua file
    if (commandLine.size() > 1 && IsValidLuaFile(commandLine[1], firstLine))
    {
        // Convert the path of the file to the long version
        if (commandLine[1].size() > 3 && iswupper(commandLine[1][0]) && commandLine[1][1] == ':' && commandLine[1][2] == '\\')
        {
#ifdef _WIN32
            wchar_t wszLongPath[MAX_PATH]{};
            if (GetLongPathName(commandLine[1].c_str(), wszLongPath, MAX_PATH) != 0)
            {
                commandLine[1] = wszLongPath;
            }
#endif
        }
        return true;
    }

    // Search for the Launch.lua file in various locations it may exist

    // Look in the same directory as the executable
    {
#ifdef _WIN32
        wchar_t wszModuleFilename[MAX_PATH]{};
        if (GetModuleFileName(nullptr, wszModuleFilename, MAX_PATH) > 0)
        {
            wchar_t *wszLastSlash = wcsrchr(wszModuleFilename, '\\');
            if (wszLastSlash != nullptr)
            {
                std::wstring basePath(wszModuleFilename, wszLastSlash + 1);
                if (FindLaunchLua(basePath, commandLine, firstLine))
                {
                    return true;
                }
            }
        }
#elif __linux__
        char executablePath[4096]{};
        if (readlink("/proc/self/exe", executablePath, 4095))
        {
            char *szLastSlash = strrchr(executablePath, '/');
            if (szLastSlash != nullptr)
            {
                std::wstring basePath(executablePath, szLastSlash + 1);
                if (FindLaunchLua(basePath, commandLine, firstLine))
                {
                    return true;
                }
            }
        }
#endif
    }

#ifdef _WIN32
    // Check for the registry key left by the installer
    // HKCU\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Path of Building Community[ (PoE2)]\InstallLocation
    {
        DWORD dwType = 0;
        DWORD dwSize = MAX_PATH;
        wchar_t wszValue[MAX_PATH]{};
#ifndef GAMEVERSION_2
        DWORD dwStatus = RegGetValue(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Path of Building Community", L"InstallLocation", RRF_RT_REG_SZ, &dwType, wszValue, &dwSize);
#else
        DWORD dwStatus = RegGetValue(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Path of Building Community (PoE2)", L"InstallLocation", RRF_RT_REG_SZ, &dwType, wszValue, &dwSize);
#endif
        if (dwStatus == ERROR_SUCCESS && dwSize > sizeof(wchar_t))
        {
            // Strip the quotes around the value
            std::wstring basePath = wszValue[0] == L'\"' ? std::wstring(wszValue + 1, wszValue + dwSize / sizeof(wchar_t) - 2) : std::wstring(wszValue, wszValue + dwSize / sizeof(wchar_t) - 1);
            if (FindLaunchLua(basePath, commandLine, firstLine))
            {
                return true;
            }
        }
    }

    // Look in the %APPDATA% folder, which is where the PoB Fork installer puts the lua files
    {
        wchar_t wszAppDataPath[MAX_PATH]{};
        if (SHGetSpecialFolderPath(nullptr, wszAppDataPath, CSIDL_APPDATA, false))
        {
            std::wstring basePath(wszAppDataPath);
#ifndef GAMEVERSION_2
            basePath += L"\\Path of Building Community\\";
#else
            basePath += L"\\Path of Building Community (PoE2)\\";
#endif
            if (FindLaunchLua(basePath, commandLine, firstLine))
            {
                return true;
            }
        }
    }
#ifndef GAMEVERSION_2
    // Look in the %PROGRAMDATA% folder, which is where the original PoB installer puts the lua files
    {
        wchar_t wszAppDataPath[MAX_PATH]{};
        if (SHGetSpecialFolderPath(nullptr, wszAppDataPath, CSIDL_COMMON_APPDATA, false))
        {
            std::wstring basePath(wszAppDataPath);
            basePath += L"\\Path of Building\\";
            if (FindLaunchLua(basePath, commandLine, firstLine))
            {
                return true;
            }
        }
    }
#endif
#endif

    return false;
}

bool isDevScript(std::wstring scriptPath)
{
    const auto finalSlash = scriptPath.find_last_of(L'\\');
    if (finalSlash == std::wstring::npos || finalSlash == 0)
    {
        return false;
    }
    const auto nextToLastSlash = scriptPath.find_last_of(L'\\', finalSlash - 1);
    if (nextToLastSlash == std::wstring::npos)
    {
        return false;
    }
    return scriptPath.compare(nextToLastSlash + 1, finalSlash - 1 - nextToLastSlash, L"src") == 0;
}

std::vector<std::string> ConvertToUTF8(std::vector<std::wstring> commandLine)
{
    std::vector<std::string> commandLineUTF8;
    if (commandLine.size() > 0)
    {
        commandLineUTF8.reserve(commandLine.size());
        for (const std::wstring &param : commandLine)
        {
            std::string paramUTF8 = ConvertToUTF8(param);
            commandLineUTF8.emplace_back(std::move(paramUTF8));
        }
    }
    return commandLineUTF8;
}

void InitConsole()
{
    static bool initialized = false;
    if (initialized)
    {
        return;
    }

#ifdef _WIN32
    AllocConsole();
    FILE *fDummy = nullptr;
    freopen_s(&fDummy, "CONIN$", "r", stdin);
    freopen_s(&fDummy, "CONOUT$", "w", stderr);
    freopen_s(&fDummy, "CONOUT$", "w", stdout);
#endif
    initialized = true;
}

// Entry function
#ifdef _WIN32
int CALLBACK wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ PWSTR lpCmdLine, _In_ int nCmdShow)
#elif __linux__
int main(int argc, char** argv)
#endif
{
    // Store all the commandline parameters into a vector for easy manipulation
    #if _WIN32
    std::vector<std::wstring> commandLine = ParseCommandLine();
    #elif __linux__
    std::vector<std::wstring> commandLine = ParseCommandLine(argc, argv);
    #endif

    // Determine if the first parameter is a path to a valid lua file and insert one if it is not
    std::string firstLine;
    if (!InsertLaunchLua(commandLine, firstLine))
    {
        InitConsole();
        wprintf(L"ERROR: Could not find a valid launcher lua file.");
        return 1;
    }

    // Determine the DLL to load
    std::wstring dllName(firstLine.begin() + 2, firstLine.end());
    trim(dllName);
#ifdef _WIN32
    if (wcsstr(dllName.c_str(), L".dll") == nullptr)
    {
        dllName += L".dll";
    }
#elif __linux__
    if (wcsstr(dllName.c_str(), L".so") == nullptr)
    {
        dllName += L".so";
    }
#endif

    if (isDevScript(commandLine[1])) {
#ifdef _WIN32
        LoadLibrary(L"lua51.dll");
#elif __linux__
        dlopen("libluajit-5.1.so", 0);
#endif
    }

    // Load the DLL
#ifdef _WIN32
    HMODULE hDLL = LoadLibrary(dllName.c_str());
#elif __linux__
    std::string dllNameString = ConvertToUTF8(dllName);
    void* hDLL = dlopen(dllNameString.c_str(), 0);
#endif
    if (hDLL == nullptr)
    {
        InitConsole();
        wprintf(L"ERROR: Could not find dll named '%ls'\n", dllName.c_str());
#ifdef _WIN32
        system("pause");
#endif
        return 1;
    }

    // Look for a valid entry point to the dll
    typedef int (*PFNRUNLUAFILEPROC)(int, char **);
#ifdef _WIN32
    PFNRUNLUAFILEPROC RunLuaFile = (PFNRUNLUAFILEPROC)GetProcAddress(hDLL, "RunLuaFileAsWin");
#elif __linux__
    PFNRUNLUAFILEPROC RunLuaFile = (PFNRUNLUAFILEPROC)dlsym(hDLL, "RunLuaFileAsWin");
#endif
    if (!RunLuaFile) {
        InitConsole();
#ifdef _WIN32
        SetConsoleTitle(commandLine[1].c_str());
        RunLuaFile = (PFNRUNLUAFILEPROC)GetProcAddress(hDLL, "RunLuaFileAsConsole");
#elif __linux__
        RunLuaFile = (PFNRUNLUAFILEPROC)dlsym(hDLL, "RunLuaFileAsConsole");
#endif
    }
    if (!RunLuaFile) {
        wprintf(L"ERROR: DLL '%ls' does not appear to be a Path of Building dll.\n", dllName.c_str());
#ifdef _WIN32
        FreeLibrary(hDLL);
#elif __linux
        dlclose(hDLL);
#endif
        return 1;
    }

    // Create a utf8 version of the commandline parameters
    std::vector<std::string> commandLineUTF8 = ConvertToUTF8(commandLine);

    // Remove the first commandline argument as the scripts don't care about that.
    commandLineUTF8.erase(commandLineUTF8.begin());

    // Convert the commandline parameters to a form the DLL can understand
    size_t dwTotalParamSize = 0;
    for (const std::string &param : commandLineUTF8)
    {
        dwTotalParamSize += param.size() + 1;
    }
    size_t dwNumParams = commandLineUTF8.size();
    std::unique_ptr<char[]> pParamBuf = std::make_unique<char[]>(dwTotalParamSize);
    char *pCurParamBufLoc = pParamBuf.get();

    std::unique_ptr<char *[]> ppParamList = std::make_unique<char *[]>(dwNumParams);
    for (size_t i = 0; i < dwNumParams; i++)
    {
        ppParamList[i] = pCurParamBufLoc;

        const std::string &param = commandLineUTF8[i];
        memcpy(pCurParamBufLoc, param.c_str(), param.size() + 1);
        pCurParamBufLoc += param.size() + 1;
    }

    // Call into the DLL
    int dwStatus = RunLuaFile((int)dwNumParams, ppParamList.get());

    // Cleanup the DLL
#ifdef _WIN32
    FreeLibrary(hDLL);
#elif __linux__
    dlclose(hDLL);
#endif

    return dwStatus;
}
