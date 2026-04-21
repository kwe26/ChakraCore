//-------------------------------------------------------------------------------------------------------
// Copyright (C) ChakraCore Project Contributors. All rights reserved.
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------
#include "JsrtPch.h"
#include "ChakraCore.h"
#include "ChakraFfi.h"
#include "ChakraHttpServer.h"

#ifdef __valid
#undef __valid
#endif

#include <cstdlib>
#include <cstring>
#include <cctype>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#include <sys/stat.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#ifndef _WIN32
#include <dlfcn.h>
#endif

// Forward declarations
CHAKRA_API JsInstallFfi(_Out_opt_ JsValueRef* ffiObject);
CHAKRA_API JsInstallHttpServer(_Out_opt_ JsValueRef* serverObject);
CHAKRA_API JsInstallHttpServerMulti(_Out_opt_ JsValueRef* serverObject);

namespace
{
    JsValueRef CHAKRA_CALLBACK HttpServerOnCallback(
        _In_ JsValueRef callee,
        _In_ bool isConstructCall,
        _In_ JsValueRef *arguments,
        _In_ unsigned short argumentCount,
        _In_opt_ void *callbackState);
    JsValueRef CHAKRA_CALLBACK HttpServerEndCallback(
        _In_ JsValueRef callee,
        _In_ bool isConstructCall,
        _In_ JsValueRef *arguments,
        _In_ unsigned short argumentCount,
        _In_opt_ void *callbackState);

#ifdef _WIN32
    typedef HMODULE RustLibraryHandle;
#else
    typedef void* RustLibraryHandle;
#endif

#ifdef _WIN32
    const char* const kRustPackageLibraryName = "chakra_packages.dll";
    const char* const kRustFfiLibraryName = "chakra_ffi.dll";
    const char* const kRustHttpServerLibraryName = "chakra_httpserver.dll";
#elif defined(__APPLE__)
    const char* const kRustPackageLibraryName = "libchakra_packages.dylib";
    const char* const kRustFfiLibraryName = "libchakra_ffi.dylib";
    const char* const kRustHttpServerLibraryName = "libchakra_httpserver.dylib";
#else
    const char* const kRustPackageLibraryName = "libchakra_packages.so";
    const char* const kRustFfiLibraryName = "libchakra_ffi.so";
    const char* const kRustHttpServerLibraryName = "libchakra_httpserver.so";
#endif

    const char* const kUnknownPackageMessage =
        "Unknown system package. Available modules: chakra:info, chakra:fs, chakra:reqwest, chakra:es2020, chakra:es2021.";

    std::map<std::string, JsValueRef> g_requireFileModuleCache;

#ifdef _WIN32
    typedef const char* (__cdecl *ChakraInfoVersionFn)();
    typedef const char* (__cdecl *ChakraLastErrorFn)();
    typedef void (__cdecl *ChakraStringFreeFn)(char*);
    typedef char* (__cdecl *ChakraFsReadFileUtf8Fn)(const char*);
    typedef int (__cdecl *ChakraFsWriteFileUtf8Fn)(const char*, const char*);
    typedef int (__cdecl *ChakraFsExistsFn)(const char*);
    typedef char* (__cdecl *ChakraReqwestGetTextFn)(const char*);
    typedef char* (__cdecl *ChakraReqwestPostTextFn)(const char*, const char*);
    typedef char* (__cdecl *ChakraReqwestFetchTextFn)(const char*, const char*, const char*);
    typedef int (__cdecl *ChakraReqwestDownloadFetchParallelFn)(const char*, const char*, int);
    typedef char* (__cdecl *ChakraEsAnalyzeFn)(const char*);
    typedef ChakraFfiHandle (__cdecl *ChakraFfiDlopenFn)(const uint8_t*, size_t);
    typedef void* (__cdecl *ChakraFfiDlsymFn)(ChakraFfiHandle, const uint8_t*, size_t);
    typedef int (__cdecl *ChakraFfiDlcloseFn)(ChakraFfiHandle);
    typedef uint64_t (__cdecl *ChakraFfiCallFn)(void*, uint32_t, const uint64_t*);
    typedef uint8_t* (__cdecl *ChakraFfiGetLastErrorFn)();
    typedef void (__cdecl *ChakraFfiFreeStringFn)(uint8_t*);
    typedef ChakraHttpServerHandle (__cdecl *ChakraHttpServeSingleFn)(uint16_t, const uint8_t*, size_t);
    typedef ChakraHttpServerHandle (__cdecl *ChakraHttpServeMultiFn)(uint16_t, const uint8_t*, size_t, uint32_t);
    typedef int (__cdecl *ChakraHttpStartFn)(ChakraHttpServerHandle);
    typedef int (__cdecl *ChakraHttpOnRouteFn)(ChakraHttpServerHandle, const uint8_t*, size_t);
    typedef int (__cdecl *ChakraHttpStopFn)(ChakraHttpServerHandle);
    typedef uint8_t* (__cdecl *ChakraHttpAcceptFn)(ChakraHttpServerHandle);
    typedef int (__cdecl *ChakraHttpRespondFn)(ChakraHttpServerHandle, uint64_t, uint16_t, const uint8_t*, size_t, const uint8_t*, size_t);
    typedef void (__cdecl *ChakraHttpFreeStringFn)(uint8_t*);
#else
    typedef const char* (*ChakraInfoVersionFn)();
    typedef const char* (*ChakraLastErrorFn)();
    typedef void (*ChakraStringFreeFn)(char*);
    typedef char* (*ChakraFsReadFileUtf8Fn)(const char*);
    typedef int (*ChakraFsWriteFileUtf8Fn)(const char*, const char*);
    typedef int (*ChakraFsExistsFn)(const char*);
    typedef char* (*ChakraReqwestGetTextFn)(const char*);
    typedef char* (*ChakraReqwestPostTextFn)(const char*, const char*);
    typedef char* (*ChakraReqwestFetchTextFn)(const char*, const char*, const char*);
    typedef int (*ChakraReqwestDownloadFetchParallelFn)(const char*, const char*, int);
    typedef char* (*ChakraEsAnalyzeFn)(const char*);
    typedef ChakraFfiHandle (*ChakraFfiDlopenFn)(const uint8_t*, size_t);
    typedef void* (*ChakraFfiDlsymFn)(ChakraFfiHandle, const uint8_t*, size_t);
    typedef int (*ChakraFfiDlcloseFn)(ChakraFfiHandle);
    typedef uint64_t (*ChakraFfiCallFn)(void*, uint32_t, const uint64_t*);
    typedef uint8_t* (*ChakraFfiGetLastErrorFn)();
    typedef void (*ChakraFfiFreeStringFn)(uint8_t*);
    typedef ChakraHttpServerHandle (*ChakraHttpServeSingleFn)(uint16_t, const uint8_t*, size_t);
    typedef ChakraHttpServerHandle (*ChakraHttpServeMultiFn)(uint16_t, const uint8_t*, size_t, uint32_t);
    typedef int (*ChakraHttpStartFn)(ChakraHttpServerHandle);
    typedef int (*ChakraHttpOnRouteFn)(ChakraHttpServerHandle, const uint8_t*, size_t);
    typedef int (*ChakraHttpStopFn)(ChakraHttpServerHandle);
    typedef uint8_t* (*ChakraHttpAcceptFn)(ChakraHttpServerHandle);
    typedef int (*ChakraHttpRespondFn)(ChakraHttpServerHandle, uint64_t, uint16_t, const uint8_t*, size_t, const uint8_t*, size_t);
    typedef void (*ChakraHttpFreeStringFn)(uint8_t*);
#endif

    struct RustPackageApi
    {
        bool initialized;
        RustLibraryHandle library;
        ChakraInfoVersionFn infoVersion;
        ChakraLastErrorFn lastError;
        ChakraStringFreeFn stringFree;
        ChakraFsReadFileUtf8Fn fsReadFileUtf8;
        ChakraFsWriteFileUtf8Fn fsWriteFileUtf8;
        ChakraFsExistsFn fsExists;
        ChakraReqwestGetTextFn reqwestGetText;
        ChakraReqwestPostTextFn reqwestPostText;
        ChakraReqwestFetchTextFn reqwestFetchText;
        ChakraReqwestDownloadFetchParallelFn reqwestDownloadFetchParallel;
        ChakraEsAnalyzeFn es2020Analyze;
        ChakraEsAnalyzeFn es2021Analyze;
        ChakraEsAnalyzeFn es2021Transform;
    };

    RustPackageApi g_rustPackageApi =
    {
        false,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr
    };

    std::once_flag g_rustPackageInitializeFlag;

    struct FfiApi
    {
        bool initialized;
        RustLibraryHandle library;
        ChakraFfiDlopenFn dlopen;
        ChakraFfiDlsymFn dlsym;
        ChakraFfiDlcloseFn dlclose;
        ChakraFfiCallFn call;
        ChakraFfiGetLastErrorFn getLastError;
        ChakraFfiFreeStringFn freeString;
    };

    FfiApi g_ffiApi =
    {
        false,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr
    };

    std::once_flag g_ffiInitializeFlag;

    struct HttpServerApi
    {
        bool initialized;
        RustLibraryHandle library;
        ChakraHttpServeSingleFn serveSingle;
        ChakraHttpServeMultiFn serveMulti;
        ChakraHttpStartFn start;
        ChakraHttpOnRouteFn onRoute;
        ChakraHttpStopFn stop;
        ChakraHttpAcceptFn accept;
        ChakraHttpRespondFn respond;
        ChakraHttpFreeStringFn freeString;
    };

    HttpServerApi g_httpServerApi =
    {
        false,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr
    };

    std::once_flag g_httpServerInitializeFlag;

    std::string JoinPath(const std::string& left, const std::string& right)
    {
        if (left.empty())
        {
            return right;
        }

        const char lastCharacter = left[left.length() - 1];
        if (lastCharacter == '/' || lastCharacter == '\\')
        {
            return left + right;
        }

        return left + "/" + right;
    }

    bool IsPathSeparator(const char ch)
    {
        return ch == '/' || ch == '\\';
    }

    bool IsAbsolutePathForRequire(const std::string& path)
    {
        if (path.empty())
        {
            return false;
        }

#ifdef _WIN32
        if (path.length() >= 2 && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':')
        {
            return true;
        }

        if (path.length() >= 2 && IsPathSeparator(path[0]) && IsPathSeparator(path[1]))
        {
            return true;
        }
#endif

        return IsPathSeparator(path[0]);
    }

    bool HasExtension(const std::string& path)
    {
        const size_t separator = path.find_last_of("/\\");
        const size_t dot = path.find_last_of('.');
        return dot != std::string::npos && (separator == std::string::npos || dot > separator);
    }

    bool EndsWithCaseInsensitive(const std::string& value, const std::string& suffix)
    {
        if (suffix.length() > value.length())
        {
            return false;
        }

        const size_t offset = value.length() - suffix.length();
        for (size_t i = 0; i < suffix.length(); ++i)
        {
            const int left = std::tolower(static_cast<unsigned char>(value[offset + i]));
            const int right = std::tolower(static_cast<unsigned char>(suffix[i]));
            if (left != right)
            {
                return false;
            }
        }

        return true;
    }

    std::string NormalizeRequireSpecifier(std::string specifier)
    {
        const std::string commaJsonSuffix = ",json";
        if (EndsWithCaseInsensitive(specifier, commaJsonSuffix))
        {
            specifier = specifier.substr(0, specifier.length() - commaJsonSuffix.length()) + ".json";
        }

        return specifier;
    }

    std::string GetCurrentWorkingDirectoryString()
    {
        char cwd[2048];
#ifdef _WIN32
        if (_getcwd(cwd, static_cast<int>(_countof(cwd))) != nullptr)
#else
        if (getcwd(cwd, sizeof(cwd)) != nullptr)
#endif
        {
            return std::string(cwd);
        }

        return std::string();
    }

    bool IsRegularFile(const char* path)
    {
        if (path == nullptr || path[0] == '\0')
        {
            return false;
        }

#ifdef _WIN32
        struct _stat fileInfo;
        if (_stat(path, &fileInfo) != 0)
        {
            return false;
        }

        return (fileInfo.st_mode & _S_IFREG) != 0;
#else
        struct stat fileInfo;
        if (stat(path, &fileInfo) != 0)
        {
            return false;
        }

        return S_ISREG(fileInfo.st_mode);
#endif
    }

    bool TryReadUtf8File(const std::string& path, std::string* contents)
    {
        if (contents == nullptr)
        {
            return false;
        }

        std::ifstream file(path.c_str(), std::ios::in | std::ios::binary);
        if (!file.good())
        {
            return false;
        }

        std::ostringstream stream;
        stream << file.rdbuf();
        *contents = stream.str();
        return true;
    }

    std::string EscapeForSingleQuotedJsString(const std::string& text)
    {
        std::string escaped;
        escaped.reserve(text.length() + 8);
        for (size_t i = 0; i < text.length(); ++i)
        {
            const char ch = text[i];
            switch (ch)
            {
            case '\\': escaped += "\\\\"; break;
            case '\'': escaped += "\\'"; break;
            case '\r': escaped += "\\r"; break;
            case '\n': escaped += "\\n"; break;
            case '\t': escaped += "\\t"; break;
            default: escaped += ch; break;
            }
        }

        return escaped;
    }

    bool TryResolveRequirePath(const std::string& rawSpecifier, const std::string& baseDirectory, std::string* resolvedFullPath)
    {
        if (resolvedFullPath == nullptr)
        {
            return false;
        }

        const std::string specifier = NormalizeRequireSpecifier(rawSpecifier);
        std::string effectiveBaseDirectory = baseDirectory;
        if (effectiveBaseDirectory.empty())
        {
            effectiveBaseDirectory = GetCurrentWorkingDirectoryString();
        }

        std::string candidatePath = specifier;
        if (!IsAbsolutePathForRequire(specifier))
        {
            candidatePath = JoinPath(effectiveBaseDirectory, specifier);
        }

        std::vector<std::string> candidates;
        if (HasExtension(candidatePath))
        {
            candidates.push_back(candidatePath);
        }
        else
        {
            candidates.push_back(candidatePath + ".js");
            candidates.push_back(candidatePath + ".json");
            candidates.push_back(JoinPath(candidatePath, "index.js"));
            candidates.push_back(JoinPath(candidatePath, "index.json"));
        }

        for (std::vector<std::string>::const_iterator it = candidates.begin(); it != candidates.end(); ++it)
        {
            if (IsRegularFile(it->c_str()))
            {
                *resolvedFullPath = *it;
                return true;
            }
        }

        return false;
    }

    bool TryGetEnvironmentVariableValue(const char* variableName, std::string* value)
    {
        if (variableName == nullptr || value == nullptr)
        {
            return false;
        }

#ifdef _WIN32
        char* envValue = nullptr;
        size_t envValueLength = 0;
        const errno_t result = _dupenv_s(&envValue, &envValueLength, variableName);
        if (result != 0 || envValue == nullptr || envValue[0] == '\0')
        {
            if (envValue != nullptr)
            {
                free(envValue);
            }

            return false;
        }

        value->assign(envValue);
        free(envValue);
        return true;
#else
        const char* envValue = getenv(variableName);
        if (envValue == nullptr || envValue[0] == '\0')
        {
            return false;
        }

        value->assign(envValue);
        return true;
#endif
    }

    bool GetDirectoryFromFullPath(const std::string& fullPath, std::string* directory)
    {
        if (directory == nullptr || fullPath.empty())
        {
            return false;
        }

        const size_t separatorIndex = fullPath.find_last_of("/\\");
        if (separatorIndex == std::string::npos)
        {
            return false;
        }

        *directory = fullPath.substr(0, separatorIndex);
        return true;
    }

    bool GetExecutableDirectory(std::string* directory)
    {
        if (directory == nullptr)
        {
            return false;
        }

        char binaryLocation[2048];
        if (!PlatformAgnostic::SystemInfo::GetBinaryLocation(binaryLocation, static_cast<unsigned>(sizeof(binaryLocation))))
        {
            return false;
        }

        return GetDirectoryFromFullPath(binaryLocation, directory);
    }

    bool GetCurrentModuleDirectory(std::string* directory)
    {
        if (directory == nullptr)
        {
            return false;
        }

#ifdef _WIN32
        HMODULE moduleHandle = nullptr;
        if (::GetModuleHandleExA(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCSTR>(&GetCurrentModuleDirectory),
                &moduleHandle) == 0)
        {
            return false;
        }

        char modulePath[MAX_PATH];
        const DWORD modulePathLength = ::GetModuleFileNameA(moduleHandle, modulePath, MAX_PATH);
        if (modulePathLength == 0)
        {
            return false;
        }

        return GetDirectoryFromFullPath(modulePath, directory);
#else
        Dl_info moduleInfo;
        if (dladdr(reinterpret_cast<void*>(&GetCurrentModuleDirectory), &moduleInfo) == 0 ||
            moduleInfo.dli_fname == nullptr ||
            moduleInfo.dli_fname[0] == '\0')
        {
            return false;
        }

        return GetDirectoryFromFullPath(moduleInfo.dli_fname, directory);
#endif
    }

    std::vector<std::string> BuildLibraryCandidates()
    {
        std::vector<std::string> candidates;

        std::string envPath;
        if (TryGetEnvironmentVariableValue("CHAKRA_RUST_PACKAGES_PATH", &envPath))
        {
            candidates.push_back(envPath);
            candidates.push_back(JoinPath(envPath, kRustPackageLibraryName));
        }

        std::string moduleDirectory;
        if (GetCurrentModuleDirectory(&moduleDirectory))
        {
            candidates.push_back(JoinPath(moduleDirectory, kRustPackageLibraryName));
            candidates.push_back(JoinPath(moduleDirectory, std::string("../../../../rust/chakra_packages/target/release/") + kRustPackageLibraryName));
            candidates.push_back(JoinPath(moduleDirectory, std::string("../../../../bin/ch/rust/chakra_packages/target/release/") + kRustPackageLibraryName));
        }

        std::string executableDirectory;
        if (GetExecutableDirectory(&executableDirectory))
        {
            candidates.push_back(JoinPath(executableDirectory, kRustPackageLibraryName));
            candidates.push_back(JoinPath(executableDirectory, std::string("../../../../rust/chakra_packages/target/release/") + kRustPackageLibraryName));
            candidates.push_back(JoinPath(executableDirectory, std::string("../../../../bin/ch/rust/chakra_packages/target/release/") + kRustPackageLibraryName));
        }

        candidates.push_back(kRustPackageLibraryName);
        return candidates;
    }

    std::vector<std::string> BuildNamedLibraryCandidates(const char* environmentVariableName, const char* libraryName, const char* repoRelativeDirectory)
    {
        std::vector<std::string> candidates;

        if (environmentVariableName != nullptr && libraryName != nullptr)
        {
            std::string envPath;
            if (TryGetEnvironmentVariableValue(environmentVariableName, &envPath))
            {
                candidates.push_back(envPath);
                candidates.push_back(JoinPath(envPath, libraryName));
            }
        }

        if (libraryName == nullptr)
        {
            return candidates;
        }

        std::string moduleDirectory;
        if (GetCurrentModuleDirectory(&moduleDirectory))
        {
            candidates.push_back(JoinPath(moduleDirectory, libraryName));
            if (repoRelativeDirectory != nullptr)
            {
                candidates.push_back(JoinPath(moduleDirectory, std::string("../../../../") + repoRelativeDirectory + libraryName));
            }
        }

        std::string executableDirectory;
        if (GetExecutableDirectory(&executableDirectory))
        {
            candidates.push_back(JoinPath(executableDirectory, libraryName));
            if (repoRelativeDirectory != nullptr)
            {
                candidates.push_back(JoinPath(executableDirectory, std::string("../../../../") + repoRelativeDirectory + libraryName));
            }
        }

        candidates.push_back(libraryName);
        return candidates;
    }

    RustLibraryHandle TryLoadLibrary(const std::string& libraryPath)
    {
        if (libraryPath.empty())
        {
            return nullptr;
        }

#ifdef _WIN32
        return ::LoadLibraryA(libraryPath.c_str());
#else
        return ::dlopen(libraryPath.c_str(), RTLD_LOCAL | RTLD_NOW);
#endif
    }

    void* TryResolveSymbol(RustLibraryHandle library, const char* symbolName)
    {
        if (library == nullptr || symbolName == nullptr)
        {
            return nullptr;
        }

#ifdef _WIN32
        return reinterpret_cast<void*>(::GetProcAddress(library, symbolName));
#else
        return dlsym(library, symbolName);
#endif
    }

    void InitializeRustPackageApi()
    {
        if (g_rustPackageApi.initialized)
        {
            return;
        }

        g_rustPackageApi.initialized = true;

        const std::vector<std::string> candidates = BuildLibraryCandidates();
        for (std::vector<std::string>::const_iterator candidate = candidates.begin();
            candidate != candidates.end();
            ++candidate)
        {
            g_rustPackageApi.library = TryLoadLibrary(*candidate);
            if (g_rustPackageApi.library != nullptr)
            {
                break;
            }
        }

        if (g_rustPackageApi.library == nullptr)
        {
            return;
        }

        g_rustPackageApi.infoVersion = reinterpret_cast<ChakraInfoVersionFn>(
            TryResolveSymbol(g_rustPackageApi.library, "chakra_info_version"));
        g_rustPackageApi.lastError = reinterpret_cast<ChakraLastErrorFn>(
            TryResolveSymbol(g_rustPackageApi.library, "chakra_last_error_message"));
        g_rustPackageApi.stringFree = reinterpret_cast<ChakraStringFreeFn>(
            TryResolveSymbol(g_rustPackageApi.library, "chakra_string_free"));
        g_rustPackageApi.fsReadFileUtf8 = reinterpret_cast<ChakraFsReadFileUtf8Fn>(
            TryResolveSymbol(g_rustPackageApi.library, "chakra_fs_read_file_utf8"));
        g_rustPackageApi.fsWriteFileUtf8 = reinterpret_cast<ChakraFsWriteFileUtf8Fn>(
            TryResolveSymbol(g_rustPackageApi.library, "chakra_fs_write_file_utf8"));
        g_rustPackageApi.fsExists = reinterpret_cast<ChakraFsExistsFn>(
            TryResolveSymbol(g_rustPackageApi.library, "chakra_fs_exists"));
        g_rustPackageApi.reqwestGetText = reinterpret_cast<ChakraReqwestGetTextFn>(
            TryResolveSymbol(g_rustPackageApi.library, "chakra_reqwest_get_text"));
        g_rustPackageApi.reqwestPostText = reinterpret_cast<ChakraReqwestPostTextFn>(
            TryResolveSymbol(g_rustPackageApi.library, "chakra_reqwest_post_text"));
        g_rustPackageApi.reqwestFetchText = reinterpret_cast<ChakraReqwestFetchTextFn>(
            TryResolveSymbol(g_rustPackageApi.library, "chakra_reqwest_fetch_text"));
        g_rustPackageApi.reqwestDownloadFetchParallel = reinterpret_cast<ChakraReqwestDownloadFetchParallelFn>(
            TryResolveSymbol(g_rustPackageApi.library, "chakra_reqwest_download_fetch_parallel"));
        g_rustPackageApi.es2020Analyze = reinterpret_cast<ChakraEsAnalyzeFn>(
            TryResolveSymbol(g_rustPackageApi.library, "chakra_es2020_analyze"));
        g_rustPackageApi.es2021Analyze = reinterpret_cast<ChakraEsAnalyzeFn>(
            TryResolveSymbol(g_rustPackageApi.library, "chakra_es2021_analyze"));
        g_rustPackageApi.es2021Transform = reinterpret_cast<ChakraEsAnalyzeFn>(
            TryResolveSymbol(g_rustPackageApi.library, "chakra_es2021_transform"));
    }

    void InitializeFfiApi()
    {
        if (g_ffiApi.initialized)
        {
            return;
        }

        g_ffiApi.initialized = true;

        // Prefer symbols from chakra_packages so existing deployments work without
        // requiring a separate chakra_ffi shared library.
        if (g_rustPackageApi.library == nullptr)
        {
            std::call_once(g_rustPackageInitializeFlag, InitializeRustPackageApi);
        }

        if (g_rustPackageApi.library != nullptr)
        {
            g_ffiApi.library = g_rustPackageApi.library;
            g_ffiApi.dlopen = reinterpret_cast<ChakraFfiDlopenFn>(
                TryResolveSymbol(g_ffiApi.library, "chakra_ffi_dlopen"));
            g_ffiApi.dlsym = reinterpret_cast<ChakraFfiDlsymFn>(
                TryResolveSymbol(g_ffiApi.library, "chakra_ffi_dlsym"));
            g_ffiApi.dlclose = reinterpret_cast<ChakraFfiDlcloseFn>(
                TryResolveSymbol(g_ffiApi.library, "chakra_ffi_dlclose"));
            g_ffiApi.call = reinterpret_cast<ChakraFfiCallFn>(
                TryResolveSymbol(g_ffiApi.library, "chakra_ffi_call"));
            g_ffiApi.getLastError = reinterpret_cast<ChakraFfiGetLastErrorFn>(
                TryResolveSymbol(g_ffiApi.library, "chakra_ffi_get_last_error"));
            g_ffiApi.freeString = reinterpret_cast<ChakraFfiFreeStringFn>(
                TryResolveSymbol(g_ffiApi.library, "chakra_ffi_free_string"));

            if (g_ffiApi.dlopen != nullptr &&
                g_ffiApi.dlsym != nullptr &&
                g_ffiApi.dlclose != nullptr &&
                g_ffiApi.call != nullptr)
            {
                return;
            }

            // Reset and try dedicated library candidates.
            g_ffiApi.library = nullptr;
            g_ffiApi.dlopen = nullptr;
            g_ffiApi.dlsym = nullptr;
            g_ffiApi.dlclose = nullptr;
            g_ffiApi.call = nullptr;
            g_ffiApi.getLastError = nullptr;
            g_ffiApi.freeString = nullptr;
        }

        const std::vector<std::string> candidates = BuildNamedLibraryCandidates(
            "CHAKRA_RUST_FFI_PATH",
            kRustFfiLibraryName,
            "rust/ffiimpl/target/release/");
        for (std::vector<std::string>::const_iterator candidate = candidates.begin();
            candidate != candidates.end();
            ++candidate)
        {
            g_ffiApi.library = TryLoadLibrary(*candidate);
            if (g_ffiApi.library != nullptr)
            {
                break;
            }
        }

        if (g_ffiApi.library == nullptr)
        {
            return;
        }

        g_ffiApi.dlopen = reinterpret_cast<ChakraFfiDlopenFn>(
            TryResolveSymbol(g_ffiApi.library, "chakra_ffi_dlopen"));
        g_ffiApi.dlsym = reinterpret_cast<ChakraFfiDlsymFn>(
            TryResolveSymbol(g_ffiApi.library, "chakra_ffi_dlsym"));
        g_ffiApi.dlclose = reinterpret_cast<ChakraFfiDlcloseFn>(
            TryResolveSymbol(g_ffiApi.library, "chakra_ffi_dlclose"));
        g_ffiApi.call = reinterpret_cast<ChakraFfiCallFn>(
            TryResolveSymbol(g_ffiApi.library, "chakra_ffi_call"));
        g_ffiApi.getLastError = reinterpret_cast<ChakraFfiGetLastErrorFn>(
            TryResolveSymbol(g_ffiApi.library, "chakra_ffi_get_last_error"));
        g_ffiApi.freeString = reinterpret_cast<ChakraFfiFreeStringFn>(
            TryResolveSymbol(g_ffiApi.library, "chakra_ffi_free_string"));
    }

    bool EnsureFfiApiLoaded(std::string* errorMessage)
    {
        std::call_once(g_ffiInitializeFlag, InitializeFfiApi);

        if (g_ffiApi.library == nullptr)
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = "Unable to load chakra_ffi shared library. Set CHAKRA_RUST_FFI_PATH to the library path or containing directory.";
            }

            return false;
        }

        if (g_ffiApi.dlopen == nullptr || g_ffiApi.dlsym == nullptr || g_ffiApi.dlclose == nullptr || g_ffiApi.call == nullptr)
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = "chakra_ffi shared library is missing required exports.";
            }

            return false;
        }

        return true;
    }

    void SetFfiErrorMessage(std::string* errorMessage, const char* fallbackMessage)
    {
        if (errorMessage == nullptr)
        {
            return;
        }

        if (g_ffiApi.getLastError != nullptr)
        {
            uint8_t* lastError = g_ffiApi.getLastError();
            if (lastError != nullptr)
            {
                if (lastError[0] != '\0')
                {
                    *errorMessage = reinterpret_cast<const char*>(lastError);
                    if (g_ffiApi.freeString != nullptr)
                    {
                        g_ffiApi.freeString(lastError);
                    }
                    return;
                }

                if (g_ffiApi.freeString != nullptr)
                {
                    g_ffiApi.freeString(lastError);
                }
            }
        }

        if (fallbackMessage != nullptr)
        {
            *errorMessage = fallbackMessage;
        }
        else
        {
            errorMessage->clear();
        }
    }

    void InitializeHttpServerApi()
    {
        if (g_httpServerApi.initialized)
        {
            return;
        }

        g_httpServerApi.initialized = true;

        // Prefer symbols from chakra_packages so existing deployments work without
        // requiring a separate chakra_httpserver shared library.
        if (g_rustPackageApi.library == nullptr)
        {
            std::call_once(g_rustPackageInitializeFlag, InitializeRustPackageApi);
        }

        if (g_rustPackageApi.library != nullptr)
        {
            g_httpServerApi.library = g_rustPackageApi.library;
            g_httpServerApi.serveSingle = reinterpret_cast<ChakraHttpServeSingleFn>(
                TryResolveSymbol(g_httpServerApi.library, "chakra_http_serve_single"));
            g_httpServerApi.serveMulti = reinterpret_cast<ChakraHttpServeMultiFn>(
                TryResolveSymbol(g_httpServerApi.library, "chakra_http_serve_multi"));
            g_httpServerApi.start = reinterpret_cast<ChakraHttpStartFn>(
                TryResolveSymbol(g_httpServerApi.library, "chakra_http_start"));
            g_httpServerApi.onRoute = reinterpret_cast<ChakraHttpOnRouteFn>(
                TryResolveSymbol(g_httpServerApi.library, "chakra_http_on_route"));
            g_httpServerApi.stop = reinterpret_cast<ChakraHttpStopFn>(
                TryResolveSymbol(g_httpServerApi.library, "chakra_http_stop"));
            g_httpServerApi.accept = reinterpret_cast<ChakraHttpAcceptFn>(
                TryResolveSymbol(g_httpServerApi.library, "chakra_http_accept"));
            g_httpServerApi.respond = reinterpret_cast<ChakraHttpRespondFn>(
                TryResolveSymbol(g_httpServerApi.library, "chakra_http_respond"));
            g_httpServerApi.freeString = reinterpret_cast<ChakraHttpFreeStringFn>(
                TryResolveSymbol(g_httpServerApi.library, "chakra_http_free_string"));

            if (g_httpServerApi.serveSingle != nullptr &&
                g_httpServerApi.serveMulti != nullptr &&
                g_httpServerApi.start != nullptr &&
                g_httpServerApi.stop != nullptr &&
                g_httpServerApi.accept != nullptr &&
                g_httpServerApi.respond != nullptr &&
                g_httpServerApi.freeString != nullptr)
            {
                return;
            }

            // Reset and try dedicated library candidates.
            g_httpServerApi.library = nullptr;
            g_httpServerApi.serveSingle = nullptr;
            g_httpServerApi.serveMulti = nullptr;
            g_httpServerApi.start = nullptr;
            g_httpServerApi.onRoute = nullptr;
            g_httpServerApi.stop = nullptr;
            g_httpServerApi.accept = nullptr;
            g_httpServerApi.respond = nullptr;
            g_httpServerApi.freeString = nullptr;
        }

        const std::vector<std::string> candidates = BuildNamedLibraryCandidates(
            "CHAKRA_RUST_HTTP_SERVER_PATH",
            kRustHttpServerLibraryName,
            "rust/httpserver/target/release/");
        for (std::vector<std::string>::const_iterator candidate = candidates.begin();
            candidate != candidates.end();
            ++candidate)
        {
            g_httpServerApi.library = TryLoadLibrary(*candidate);
            if (g_httpServerApi.library != nullptr)
            {
                break;
            }
        }

        if (g_httpServerApi.library == nullptr)
        {
            return;
        }

        g_httpServerApi.serveSingle = reinterpret_cast<ChakraHttpServeSingleFn>(
            TryResolveSymbol(g_httpServerApi.library, "chakra_http_serve_single"));
        g_httpServerApi.serveMulti = reinterpret_cast<ChakraHttpServeMultiFn>(
            TryResolveSymbol(g_httpServerApi.library, "chakra_http_serve_multi"));
        g_httpServerApi.start = reinterpret_cast<ChakraHttpStartFn>(
            TryResolveSymbol(g_httpServerApi.library, "chakra_http_start"));
        g_httpServerApi.onRoute = reinterpret_cast<ChakraHttpOnRouteFn>(
            TryResolveSymbol(g_httpServerApi.library, "chakra_http_on_route"));
        g_httpServerApi.stop = reinterpret_cast<ChakraHttpStopFn>(
            TryResolveSymbol(g_httpServerApi.library, "chakra_http_stop"));
        g_httpServerApi.accept = reinterpret_cast<ChakraHttpAcceptFn>(
            TryResolveSymbol(g_httpServerApi.library, "chakra_http_accept"));
        g_httpServerApi.respond = reinterpret_cast<ChakraHttpRespondFn>(
            TryResolveSymbol(g_httpServerApi.library, "chakra_http_respond"));
        g_httpServerApi.freeString = reinterpret_cast<ChakraHttpFreeStringFn>(
            TryResolveSymbol(g_httpServerApi.library, "chakra_http_free_string"));
    }

    bool EnsureHttpServerApiLoaded(std::string* errorMessage)
    {
        std::call_once(g_httpServerInitializeFlag, InitializeHttpServerApi);

        if (g_httpServerApi.library == nullptr)
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = "Unable to load chakra_httpserver shared library. Set CHAKRA_RUST_HTTP_SERVER_PATH to the library path or containing directory.";
            }

            return false;
        }

        if (g_httpServerApi.serveSingle == nullptr || g_httpServerApi.serveMulti == nullptr || g_httpServerApi.start == nullptr || g_httpServerApi.stop == nullptr || g_httpServerApi.accept == nullptr || g_httpServerApi.respond == nullptr || g_httpServerApi.freeString == nullptr)
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = "chakra_httpserver shared library is missing required exports.";
            }

            return false;
        }

        return true;
    }

    bool EnsureRustPackageApiLoaded(std::string* errorMessage)
    {
        std::call_once(g_rustPackageInitializeFlag, InitializeRustPackageApi);

        if (g_rustPackageApi.library == nullptr)
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = "Unable to load chakra_packages shared library. Set CHAKRA_RUST_PACKAGES_PATH to the library path or containing directory.";
            }

            return false;
        }

        return true;
    }

    void SetRustErrorMessage(std::string* errorMessage, const char* fallbackMessage)
    {
        if (errorMessage == nullptr)
        {
            return;
        }

        if (g_rustPackageApi.lastError != nullptr)
        {
            const char* lastError = g_rustPackageApi.lastError();
            if (lastError != nullptr && lastError[0] != '\0')
            {
                *errorMessage = lastError;
                return;
            }
        }

        if (fallbackMessage != nullptr)
        {
            *errorMessage = fallbackMessage;
        }
        else
        {
            errorMessage->clear();
        }
    }

    bool CopyRustOwnedString(char* rustString, std::string* output)
    {
        if (rustString == nullptr || output == nullptr)
        {
            return false;
        }

        output->assign(rustString);

        if (g_rustPackageApi.stringFree != nullptr)
        {
            g_rustPackageApi.stringFree(rustString);
        }

        return true;
    }

    bool TryGetInfoVersion(std::string* version, std::string* errorMessage)
    {
        if (version == nullptr)
        {
            return false;
        }

        version->clear();

        if (!EnsureRustPackageApiLoaded(errorMessage))
        {
            return false;
        }

        if (g_rustPackageApi.infoVersion == nullptr)
        {
            SetRustErrorMessage(errorMessage, "Rust symbol chakra_info_version is unavailable.");
            return false;
        }

        const char* rawVersion = g_rustPackageApi.infoVersion();
        if (rawVersion == nullptr)
        {
            SetRustErrorMessage(errorMessage, "Failed to retrieve version from chakra:info.");
            return false;
        }

        version->assign(rawVersion);
        return true;
    }

    bool TryFsReadFileUtf8(const std::string& path, std::string* contents, std::string* errorMessage)
    {
        if (contents == nullptr)
        {
            return false;
        }

        contents->clear();

        if (!EnsureRustPackageApiLoaded(errorMessage))
        {
            return false;
        }

        if (g_rustPackageApi.fsReadFileUtf8 == nullptr)
        {
            SetRustErrorMessage(errorMessage, "Rust symbol chakra_fs_read_file_utf8 is unavailable.");
            return false;
        }

        char* value = g_rustPackageApi.fsReadFileUtf8(path.c_str());
        if (!CopyRustOwnedString(value, contents))
        {
            SetRustErrorMessage(errorMessage, "Failed to read file through chakra:fs.");
            return false;
        }

        return true;
    }

    bool TryFsWriteFileUtf8(const std::string& path, const std::string& content, std::string* errorMessage)
    {
        if (!EnsureRustPackageApiLoaded(errorMessage))
        {
            return false;
        }

        if (g_rustPackageApi.fsWriteFileUtf8 == nullptr)
        {
            SetRustErrorMessage(errorMessage, "Rust symbol chakra_fs_write_file_utf8 is unavailable.");
            return false;
        }

        if (g_rustPackageApi.fsWriteFileUtf8(path.c_str(), content.c_str()) == 0)
        {
            SetRustErrorMessage(errorMessage, "Failed to write file through chakra:fs.");
            return false;
        }

        return true;
    }

    bool TryFsExists(const std::string& path, bool* exists, std::string* errorMessage)
    {
        if (exists == nullptr)
        {
            return false;
        }

        *exists = false;

        if (!EnsureRustPackageApiLoaded(errorMessage))
        {
            return false;
        }

        if (g_rustPackageApi.fsExists == nullptr)
        {
            SetRustErrorMessage(errorMessage, "Rust symbol chakra_fs_exists is unavailable.");
            return false;
        }

        const int existsResult = g_rustPackageApi.fsExists(path.c_str());
        if (existsResult < 0)
        {
            SetRustErrorMessage(errorMessage, "Failed to check file existence through chakra:fs.");
            return false;
        }

        *exists = existsResult != 0;
        return true;
    }

    bool TryReqwestGetText(const std::string& url, std::string* responseText, std::string* errorMessage)
    {
        if (responseText == nullptr)
        {
            return false;
        }

        responseText->clear();

        if (!EnsureRustPackageApiLoaded(errorMessage))
        {
            return false;
        }

        if (g_rustPackageApi.reqwestGetText == nullptr)
        {
            SetRustErrorMessage(errorMessage, "Rust symbol chakra_reqwest_get_text is unavailable.");
            return false;
        }

        char* value = g_rustPackageApi.reqwestGetText(url.c_str());
        if (!CopyRustOwnedString(value, responseText))
        {
            SetRustErrorMessage(errorMessage, "Failed to execute GET through chakra:reqwest.");
            return false;
        }

        return true;
    }

    bool TryReqwestPostText(const std::string& url, const std::string& body, std::string* responseText, std::string* errorMessage)
    {
        if (responseText == nullptr)
        {
            return false;
        }

        responseText->clear();

        if (!EnsureRustPackageApiLoaded(errorMessage))
        {
            return false;
        }

        if (g_rustPackageApi.reqwestPostText == nullptr)
        {
            SetRustErrorMessage(errorMessage, "Rust symbol chakra_reqwest_post_text is unavailable.");
            return false;
        }

        char* value = g_rustPackageApi.reqwestPostText(url.c_str(), body.c_str());
        if (!CopyRustOwnedString(value, responseText))
        {
            SetRustErrorMessage(errorMessage, "Failed to execute POST through chakra:reqwest.");
            return false;
        }

        return true;
    }

    bool TryReqwestFetchText(const std::string& method, const std::string& url, const std::string* body, std::string* responseText, std::string* errorMessage)
    {
        if (responseText == nullptr)
        {
            return false;
        }

        responseText->clear();

        if (!EnsureRustPackageApiLoaded(errorMessage))
        {
            return false;
        }

        if (g_rustPackageApi.reqwestFetchText == nullptr)
        {
            SetRustErrorMessage(errorMessage, "Rust symbol chakra_reqwest_fetch_text is unavailable.");
            return false;
        }

        const char* requestBody = body == nullptr ? nullptr : body->c_str();
        char* value = g_rustPackageApi.reqwestFetchText(method.c_str(), url.c_str(), requestBody);
        if (!CopyRustOwnedString(value, responseText))
        {
            SetRustErrorMessage(errorMessage, "Failed to execute fetch through chakra:reqwest.");
            return false;
        }

        return true;
    }

    bool TryReqwestDownloadFetchParallel(const std::string& url, const std::string& outputPath, int parallelPartCount, std::string* errorMessage)
    {
        if (!EnsureRustPackageApiLoaded(errorMessage))
        {
            return false;
        }

        if (g_rustPackageApi.reqwestDownloadFetchParallel == nullptr)
        {
            SetRustErrorMessage(errorMessage, "Rust symbol chakra_reqwest_download_fetch_parallel is unavailable.");
            return false;
        }

        if (g_rustPackageApi.reqwestDownloadFetchParallel(url.c_str(), outputPath.c_str(), parallelPartCount) == 0)
        {
            SetRustErrorMessage(errorMessage, "Failed to execute downloadFetch through chakra:reqwest.");
            return false;
        }

        return true;
    }

    bool TryEs2020Analyze(const std::string& source, std::string* analysisJson, std::string* errorMessage)
    {
        if (analysisJson == nullptr)
        {
            return false;
        }

        analysisJson->clear();

        if (!EnsureRustPackageApiLoaded(errorMessage))
        {
            return false;
        }

        if (g_rustPackageApi.es2020Analyze == nullptr)
        {
            SetRustErrorMessage(errorMessage, "Rust symbol chakra_es2020_analyze is unavailable.");
            return false;
        }

        char* value = g_rustPackageApi.es2020Analyze(source.c_str());
        if (!CopyRustOwnedString(value, analysisJson))
        {
            SetRustErrorMessage(errorMessage, "Failed to analyze source through chakra:es2020.");
            return false;
        }

        return true;
    }

    bool TryEs2021Analyze(const std::string& source, std::string* analysisJson, std::string* errorMessage)
    {
        if (analysisJson == nullptr)
        {
            return false;
        }

        analysisJson->clear();

        if (!EnsureRustPackageApiLoaded(errorMessage))
        {
            return false;
        }

        if (g_rustPackageApi.es2021Analyze == nullptr)
        {
            SetRustErrorMessage(errorMessage, "Rust symbol chakra_es2021_analyze is unavailable.");
            return false;
        }

        char* value = g_rustPackageApi.es2021Analyze(source.c_str());
        if (!CopyRustOwnedString(value, analysisJson))
        {
            SetRustErrorMessage(errorMessage, "Failed to analyze source through chakra:es2021.");
            return false;
        }

        return true;
    }

    bool TryEs2021Transform(const std::string& source, std::string* transformedSource, std::string* errorMessage)
    {
        if (transformedSource == nullptr)
        {
            return false;
        }

        transformedSource->clear();

        if (!EnsureRustPackageApiLoaded(errorMessage))
        {
            return false;
        }

        if (g_rustPackageApi.es2021Transform == nullptr)
        {
            SetRustErrorMessage(errorMessage, "Rust symbol chakra_es2021_transform is unavailable.");
            return false;
        }

        char* value = g_rustPackageApi.es2021Transform(source.c_str());
        if (!CopyRustOwnedString(value, transformedSource))
        {
            SetRustErrorMessage(errorMessage, "Failed to transform source through chakra:es2021.");
            return false;
        }

        return true;
    }

    JsErrorCode SetExceptionFromUtf8(const std::string& message)
    {
        JsValueRef messageValue = JS_INVALID_REFERENCE;
        JsErrorCode errorCode = JsCreateString(message.c_str(), message.length(), &messageValue);
        if (errorCode != JsNoError)
        {
            return errorCode;
        }

        JsValueRef errorValue = JS_INVALID_REFERENCE;
        errorCode = JsCreateError(messageValue, &errorValue);
        if (errorCode != JsNoError)
        {
            return errorCode;
        }

        return JsSetException(errorValue);
    }

    JsValueRef SetExceptionAndReturnInvalidReference(const std::string& message)
    {
        SetExceptionFromUtf8(message);
        return JS_INVALID_REFERENCE;
    }

    JsErrorCode ConvertValueToUtf8String(JsValueRef value, std::string* output)
    {
        if (output == nullptr)
        {
            return JsErrorInvalidArgument;
        }

        output->clear();

        JsValueRef stringValue = JS_INVALID_REFERENCE;
        JsErrorCode errorCode = JsConvertValueToString(value, &stringValue);
        if (errorCode != JsNoError)
        {
            return errorCode;
        }

        size_t requiredLength = 0;
        errorCode = JsCopyString(stringValue, nullptr, 0, &requiredLength);
        if (errorCode != JsNoError)
        {
            return errorCode;
        }

        if (requiredLength == 0)
        {
            return JsNoError;
        }

        output->resize(requiredLength);
        size_t writtenLength = 0;
        errorCode = JsCopyString(stringValue, &(*output)[0], requiredLength, &writtenLength);
        if (errorCode != JsNoError)
        {
            return errorCode;
        }

        output->resize(writtenLength);
        return JsNoError;
    }

    JsErrorCode CreateUtf8StringValue(const std::string& text, JsValueRef* result)
    {
        if (result == nullptr)
        {
            return JsErrorInvalidArgument;
        }

        return JsCreateString(text.c_str(), text.length(), result);
    }

    JsErrorCode GetPropertyByName(JsValueRef object, const char* propertyName, JsValueRef* propertyValue)
    {
        if (propertyName == nullptr || propertyValue == nullptr)
        {
            return JsErrorInvalidArgument;
        }

        JsPropertyIdRef propertyId = JS_INVALID_REFERENCE;
        JsErrorCode errorCode = JsCreatePropertyId(propertyName, strlen(propertyName), &propertyId);
        if (errorCode != JsNoError)
        {
            return errorCode;
        }

        return JsGetProperty(object, propertyId, propertyValue);
    }

    JsErrorCode SetPropertyByName(JsValueRef object, const char* propertyName, JsValueRef propertyValue)
    {
        if (propertyName == nullptr)
        {
            return JsErrorInvalidArgument;
        }

        JsPropertyIdRef propertyId = JS_INVALID_REFERENCE;
        JsErrorCode errorCode = JsCreatePropertyId(propertyName, strlen(propertyName), &propertyId);
        if (errorCode != JsNoError)
        {
            return errorCode;
        }

        return JsSetProperty(object, propertyId, propertyValue, true);
    }

    JsErrorCode InstallMethod(JsValueRef object, const char* propertyName, JsNativeFunction callback)
    {
        JsValueRef functionValue = JS_INVALID_REFERENCE;
        JsErrorCode errorCode = JsCreateFunction(callback, nullptr, &functionValue);
        if (errorCode != JsNoError)
        {
            return errorCode;
        }

        return SetPropertyByName(object, propertyName, functionValue);
    }

    JsErrorCode ParseJsonText(const std::string& jsonText, JsValueRef* result)
    {
        if (result == nullptr)
        {
            return JsErrorInvalidArgument;
        }

        JsValueRef jsonTextValue = JS_INVALID_REFERENCE;
        JsErrorCode errorCode = JsCreateString(jsonText.c_str(), jsonText.length(), &jsonTextValue);
        if (errorCode != JsNoError)
        {
            return errorCode;
        }

        JsValueRef globalObject = JS_INVALID_REFERENCE;
        errorCode = JsGetGlobalObject(&globalObject);
        if (errorCode != JsNoError)
        {
            return errorCode;
        }

        JsValueRef jsonObject = JS_INVALID_REFERENCE;
        errorCode = GetPropertyByName(globalObject, "JSON", &jsonObject);
        if (errorCode != JsNoError)
        {
            return errorCode;
        }

        JsValueRef parseFunction = JS_INVALID_REFERENCE;
        errorCode = GetPropertyByName(jsonObject, "parse", &parseFunction);
        if (errorCode != JsNoError)
        {
            return errorCode;
        }

        JsValueRef arguments[2] = { jsonObject, jsonTextValue };
        return JsCallFunction(parseFunction, arguments, 2, result);
    }

    JsErrorCode GetSourceArgument(JsValueRef* arguments, unsigned short argumentCount, const char* functionName, std::string* sourceText)
    {
        if (argumentCount < 2)
        {
            SetExceptionFromUtf8(std::string(functionName) + " expects a source argument.");
            return JsErrorInvalidArgument;
        }

        return ConvertValueToUtf8String(arguments[1], sourceText);
    }

    JsErrorCode EvaluateJavascriptModule(const std::string& modulePath, const std::string& moduleSource, JsValueRef* moduleExports)
    {
        if (moduleExports == nullptr)
        {
            return JsErrorInvalidArgument;
        }

        std::string moduleDirectory;
        if (!GetDirectoryFromFullPath(modulePath, &moduleDirectory))
        {
            moduleDirectory = GetCurrentWorkingDirectoryString();
        }

        std::string wrappedSource =
            "(function(){"
            "var __filename='" + EscapeForSingleQuotedJsString(modulePath) + "';"
            "var __dirname='" + EscapeForSingleQuotedJsString(moduleDirectory) + "';"
            "var module={exports:{}};"
            "var exports=module.exports;"
            "var requireLocal=function(p){return require(p,__dirname);};"
            "(function(exports,require,module,__filename,__dirname){\n" +
            moduleSource +
            "\n})(exports,requireLocal,module,__filename,__dirname);"
            "return module.exports;"
            "})()";

        JsValueRef scriptValue = JS_INVALID_REFERENCE;
        JsErrorCode errorCode = JsCreateString(wrappedSource.c_str(), wrappedSource.length(), &scriptValue);
        if (errorCode != JsNoError)
        {
            return errorCode;
        }

        JsValueRef sourceNameValue = JS_INVALID_REFERENCE;
        errorCode = JsCreateString(modulePath.c_str(), modulePath.length(), &sourceNameValue);
        if (errorCode != JsNoError)
        {
            return errorCode;
        }

        return JsRun(scriptValue, JS_SOURCE_CONTEXT_NONE, sourceNameValue, JsParseScriptAttributeNone, moduleExports);
    }

    JsErrorCode LoadFileModule(const std::string& rawSpecifier, const std::string& baseDirectory, JsValueRef* moduleValue)
    {
        if (moduleValue == nullptr)
        {
            return JsErrorInvalidArgument;
        }

        *moduleValue = JS_INVALID_REFERENCE;

        std::string resolvedPath;
        if (!TryResolveRequirePath(rawSpecifier, baseDirectory, &resolvedPath))
        {
            return SetExceptionFromUtf8("Cannot find module '" + rawSpecifier + "'.");
        }

        std::map<std::string, JsValueRef>::const_iterator cacheEntry = g_requireFileModuleCache.find(resolvedPath);
        if (cacheEntry != g_requireFileModuleCache.end())
        {
            *moduleValue = cacheEntry->second;
            return JsNoError;
        }

        std::string sourceText;
        if (!TryReadUtf8File(resolvedPath, &sourceText))
        {
            return SetExceptionFromUtf8("Failed to read module file '" + resolvedPath + "'.");
        }

        JsErrorCode errorCode = JsNoError;
        if (EndsWithCaseInsensitive(resolvedPath, ".json"))
        {
            errorCode = ParseJsonText(sourceText, moduleValue);
        }
        else
        {
            errorCode = EvaluateJavascriptModule(resolvedPath, sourceText, moduleValue);
        }

        if (errorCode != JsNoError)
        {
            return errorCode;
        }

        JsAddRef(*moduleValue, nullptr);
        g_requireFileModuleCache[resolvedPath] = *moduleValue;
        return JsNoError;
    }

    JsErrorCode CreateSystemPackageObject(const std::string& moduleName, JsValueRef* packageObject);

    JsValueRef CHAKRA_CALLBACK InfoVersionCallback(
        JsValueRef callee,
        bool isConstructCall,
        JsValueRef* arguments,
        unsigned short argumentCount,
        void* callbackState)
    {
        UNREFERENCED_PARAMETER(callee);
        UNREFERENCED_PARAMETER(isConstructCall);
        UNREFERENCED_PARAMETER(arguments);
        UNREFERENCED_PARAMETER(argumentCount);
        UNREFERENCED_PARAMETER(callbackState);

        std::string version;
        std::string rustError;
        if (!TryGetInfoVersion(&version, &rustError))
        {
            return SetExceptionAndReturnInvalidReference(rustError.empty() ? "chakra:info.version failed." : rustError);
        }

        JsValueRef returnValue = JS_INVALID_REFERENCE;
        if (CreateUtf8StringValue(version, &returnValue) != JsNoError)
        {
            return JS_INVALID_REFERENCE;
        }

        return returnValue;
    }

    JsValueRef CHAKRA_CALLBACK FsReadFileSyncCallback(
        JsValueRef callee,
        bool isConstructCall,
        JsValueRef* arguments,
        unsigned short argumentCount,
        void* callbackState)
    {
        UNREFERENCED_PARAMETER(callee);
        UNREFERENCED_PARAMETER(isConstructCall);
        UNREFERENCED_PARAMETER(callbackState);

        if (argumentCount < 2)
        {
            return SetExceptionAndReturnInvalidReference("chakra:fs.readFileSync expects a path argument.");
        }

        std::string path;
        JsErrorCode errorCode = ConvertValueToUtf8String(arguments[1], &path);
        if (errorCode != JsNoError)
        {
            return JS_INVALID_REFERENCE;
        }

        std::string contents;
        std::string rustError;
        if (!TryFsReadFileUtf8(path, &contents, &rustError))
        {
            return SetExceptionAndReturnInvalidReference(rustError.empty() ? "chakra:fs.readFileSync failed." : rustError);
        }

        JsValueRef returnValue = JS_INVALID_REFERENCE;
        if (CreateUtf8StringValue(contents, &returnValue) != JsNoError)
        {
            return JS_INVALID_REFERENCE;
        }

        return returnValue;
    }

    JsValueRef CHAKRA_CALLBACK FsWriteFileSyncCallback(
        JsValueRef callee,
        bool isConstructCall,
        JsValueRef* arguments,
        unsigned short argumentCount,
        void* callbackState)
    {
        UNREFERENCED_PARAMETER(callee);
        UNREFERENCED_PARAMETER(isConstructCall);
        UNREFERENCED_PARAMETER(callbackState);

        if (argumentCount < 3)
        {
            return SetExceptionAndReturnInvalidReference("chakra:fs.writeFileSync expects path and content arguments.");
        }

        std::string path;
        JsErrorCode errorCode = ConvertValueToUtf8String(arguments[1], &path);
        if (errorCode != JsNoError)
        {
            return JS_INVALID_REFERENCE;
        }

        std::string content;
        errorCode = ConvertValueToUtf8String(arguments[2], &content);
        if (errorCode != JsNoError)
        {
            return JS_INVALID_REFERENCE;
        }

        std::string rustError;
        if (!TryFsWriteFileUtf8(path, content, &rustError))
        {
            return SetExceptionAndReturnInvalidReference(rustError.empty() ? "chakra:fs.writeFileSync failed." : rustError);
        }

        JsValueRef returnValue = JS_INVALID_REFERENCE;
        if (JsGetUndefinedValue(&returnValue) != JsNoError)
        {
            return JS_INVALID_REFERENCE;
        }

        return returnValue;
    }

    JsValueRef CHAKRA_CALLBACK FsExistsSyncCallback(
        JsValueRef callee,
        bool isConstructCall,
        JsValueRef* arguments,
        unsigned short argumentCount,
        void* callbackState)
    {
        UNREFERENCED_PARAMETER(callee);
        UNREFERENCED_PARAMETER(isConstructCall);
        UNREFERENCED_PARAMETER(callbackState);

        if (argumentCount < 2)
        {
            return SetExceptionAndReturnInvalidReference("chakra:fs.existsSync expects a path argument.");
        }

        std::string path;
        JsErrorCode errorCode = ConvertValueToUtf8String(arguments[1], &path);
        if (errorCode != JsNoError)
        {
            return JS_INVALID_REFERENCE;
        }

        bool exists = false;
        std::string rustError;
        if (!TryFsExists(path, &exists, &rustError))
        {
            return SetExceptionAndReturnInvalidReference(rustError.empty() ? "chakra:fs.existsSync failed." : rustError);
        }

        JsValueRef returnValue = JS_INVALID_REFERENCE;
        if (exists)
        {
            if (JsGetTrueValue(&returnValue) != JsNoError)
            {
                return JS_INVALID_REFERENCE;
            }
        }
        else
        {
            if (JsGetFalseValue(&returnValue) != JsNoError)
            {
                return JS_INVALID_REFERENCE;
            }
        }

        return returnValue;
    }

    JsValueRef CHAKRA_CALLBACK ReqwestGetCallback(
        JsValueRef callee,
        bool isConstructCall,
        JsValueRef* arguments,
        unsigned short argumentCount,
        void* callbackState)
    {
        UNREFERENCED_PARAMETER(callee);
        UNREFERENCED_PARAMETER(isConstructCall);
        UNREFERENCED_PARAMETER(callbackState);

        if (argumentCount < 2)
        {
            return SetExceptionAndReturnInvalidReference("chakra:reqwest.get expects a URL argument.");
        }

        std::string url;
        JsErrorCode errorCode = ConvertValueToUtf8String(arguments[1], &url);
        if (errorCode != JsNoError)
        {
            return JS_INVALID_REFERENCE;
        }

        std::string responseText;
        std::string rustError;
        if (!TryReqwestGetText(url, &responseText, &rustError))
        {
            return SetExceptionAndReturnInvalidReference(rustError.empty() ? "chakra:reqwest.get failed." : rustError);
        }

        JsValueRef returnValue = JS_INVALID_REFERENCE;
        if (CreateUtf8StringValue(responseText, &returnValue) != JsNoError)
        {
            return JS_INVALID_REFERENCE;
        }

        return returnValue;
    }

    JsValueRef CHAKRA_CALLBACK ReqwestPostCallback(
        JsValueRef callee,
        bool isConstructCall,
        JsValueRef* arguments,
        unsigned short argumentCount,
        void* callbackState)
    {
        UNREFERENCED_PARAMETER(callee);
        UNREFERENCED_PARAMETER(isConstructCall);
        UNREFERENCED_PARAMETER(callbackState);

        if (argumentCount < 3)
        {
            return SetExceptionAndReturnInvalidReference("chakra:reqwest.post expects URL and body arguments.");
        }

        std::string url;
        JsErrorCode errorCode = ConvertValueToUtf8String(arguments[1], &url);
        if (errorCode != JsNoError)
        {
            return JS_INVALID_REFERENCE;
        }

        std::string body;
        errorCode = ConvertValueToUtf8String(arguments[2], &body);
        if (errorCode != JsNoError)
        {
            return JS_INVALID_REFERENCE;
        }

        std::string responseText;
        std::string rustError;
        if (!TryReqwestPostText(url, body, &responseText, &rustError))
        {
            return SetExceptionAndReturnInvalidReference(rustError.empty() ? "chakra:reqwest.post failed." : rustError);
        }

        JsValueRef returnValue = JS_INVALID_REFERENCE;
        if (CreateUtf8StringValue(responseText, &returnValue) != JsNoError)
        {
            return JS_INVALID_REFERENCE;
        }

        return returnValue;
    }

    JsValueRef CHAKRA_CALLBACK ReqwestFetchCallback(
        JsValueRef callee,
        bool isConstructCall,
        JsValueRef* arguments,
        unsigned short argumentCount,
        void* callbackState)
    {
        UNREFERENCED_PARAMETER(callee);
        UNREFERENCED_PARAMETER(isConstructCall);
        UNREFERENCED_PARAMETER(callbackState);

        if (argumentCount < 3)
        {
            return SetExceptionAndReturnInvalidReference("chakra:reqwest.fetch expects method and URL arguments.");
        }

        std::string method;
        JsErrorCode errorCode = ConvertValueToUtf8String(arguments[1], &method);
        if (errorCode != JsNoError)
        {
            return JS_INVALID_REFERENCE;
        }

        std::string url;
        errorCode = ConvertValueToUtf8String(arguments[2], &url);
        if (errorCode != JsNoError)
        {
            return JS_INVALID_REFERENCE;
        }

        std::string bodyText;
        const std::string* bodyPointer = nullptr;
        if (argumentCount > 3)
        {
            JsValueType bodyType = JsUndefined;
            errorCode = JsGetValueType(arguments[3], &bodyType);
            if (errorCode != JsNoError)
            {
                return JS_INVALID_REFERENCE;
            }

            if (bodyType != JsUndefined)
            {
                errorCode = ConvertValueToUtf8String(arguments[3], &bodyText);
                if (errorCode != JsNoError)
                {
                    return JS_INVALID_REFERENCE;
                }

                bodyPointer = &bodyText;
            }
        }

        std::string responseText;
        std::string rustError;
        if (!TryReqwestFetchText(method, url, bodyPointer, &responseText, &rustError))
        {
            return SetExceptionAndReturnInvalidReference(rustError.empty() ? "chakra:reqwest.fetch failed." : rustError);
        }

        JsValueRef returnValue = JS_INVALID_REFERENCE;
        if (CreateUtf8StringValue(responseText, &returnValue) != JsNoError)
        {
            return JS_INVALID_REFERENCE;
        }

        return returnValue;
    }

    JsValueRef CHAKRA_CALLBACK ReqwestDownloadFetchCallback(
        JsValueRef callee,
        bool isConstructCall,
        JsValueRef* arguments,
        unsigned short argumentCount,
        void* callbackState)
    {
        UNREFERENCED_PARAMETER(callee);
        UNREFERENCED_PARAMETER(isConstructCall);
        UNREFERENCED_PARAMETER(callbackState);

        if (argumentCount < 3)
        {
            return SetExceptionAndReturnInvalidReference("chakra:reqwest.downloadFetch expects URL and outputPath arguments.");
        }

        std::string url;
        JsErrorCode errorCode = ConvertValueToUtf8String(arguments[1], &url);
        if (errorCode != JsNoError)
        {
            return JS_INVALID_REFERENCE;
        }

        std::string outputPath;
        errorCode = ConvertValueToUtf8String(arguments[2], &outputPath);
        if (errorCode != JsNoError)
        {
            return JS_INVALID_REFERENCE;
        }

        int parallelPartCount = 4;
        if (argumentCount > 3)
        {
            errorCode = JsNumberToInt(arguments[3], &parallelPartCount);
            if (errorCode != JsNoError)
            {
                return JS_INVALID_REFERENCE;
            }
        }

        if (parallelPartCount <= 0)
        {
            parallelPartCount = 4;
        }

        std::string rustError;
        if (!TryReqwestDownloadFetchParallel(url, outputPath, parallelPartCount, &rustError))
        {
            return SetExceptionAndReturnInvalidReference(rustError.empty() ? "chakra:reqwest.downloadFetch failed." : rustError);
        }

        JsValueRef returnValue = JS_INVALID_REFERENCE;
        if (JsGetTrueValue(&returnValue) != JsNoError)
        {
            return JS_INVALID_REFERENCE;
        }

        return returnValue;
    }

    JsValueRef CHAKRA_CALLBACK Es2020AnalyzeCallback(
        JsValueRef callee,
        bool isConstructCall,
        JsValueRef* arguments,
        unsigned short argumentCount,
        void* callbackState)
    {
        UNREFERENCED_PARAMETER(callee);
        UNREFERENCED_PARAMETER(isConstructCall);
        UNREFERENCED_PARAMETER(callbackState);

        std::string sourceText;
        if (GetSourceArgument(arguments, argumentCount, "chakra:es2020.analyze", &sourceText) != JsNoError)
        {
            return JS_INVALID_REFERENCE;
        }

        std::string analysisJson;
        std::string rustError;
        if (!TryEs2020Analyze(sourceText, &analysisJson, &rustError))
        {
            return SetExceptionAndReturnInvalidReference(rustError.empty() ? "chakra:es2020.analyze failed." : rustError);
        }

        JsValueRef returnValue = JS_INVALID_REFERENCE;
        if (ParseJsonText(analysisJson, &returnValue) != JsNoError)
        {
            return JS_INVALID_REFERENCE;
        }

        return returnValue;
    }

    JsValueRef CHAKRA_CALLBACK Es2021AnalyzeCallback(
        JsValueRef callee,
        bool isConstructCall,
        JsValueRef* arguments,
        unsigned short argumentCount,
        void* callbackState)
    {
        UNREFERENCED_PARAMETER(callee);
        UNREFERENCED_PARAMETER(isConstructCall);
        UNREFERENCED_PARAMETER(callbackState);

        std::string sourceText;
        if (GetSourceArgument(arguments, argumentCount, "chakra:es2021.analyze", &sourceText) != JsNoError)
        {
            return JS_INVALID_REFERENCE;
        }

        std::string analysisJson;
        std::string rustError;
        if (!TryEs2021Analyze(sourceText, &analysisJson, &rustError))
        {
            return SetExceptionAndReturnInvalidReference(rustError.empty() ? "chakra:es2021.analyze failed." : rustError);
        }

        JsValueRef returnValue = JS_INVALID_REFERENCE;
        if (ParseJsonText(analysisJson, &returnValue) != JsNoError)
        {
            return JS_INVALID_REFERENCE;
        }

        return returnValue;
    }

    JsValueRef CHAKRA_CALLBACK Es2021TransformCallback(
        JsValueRef callee,
        bool isConstructCall,
        JsValueRef* arguments,
        unsigned short argumentCount,
        void* callbackState)
    {
        UNREFERENCED_PARAMETER(callee);
        UNREFERENCED_PARAMETER(isConstructCall);
        UNREFERENCED_PARAMETER(callbackState);

        std::string sourceText;
        if (GetSourceArgument(arguments, argumentCount, "chakra:es2021.transform", &sourceText) != JsNoError)
        {
            return JS_INVALID_REFERENCE;
        }

        std::string transformedSource;
        std::string rustError;
        if (!TryEs2021Transform(sourceText, &transformedSource, &rustError))
        {
            return SetExceptionAndReturnInvalidReference(rustError.empty() ? "chakra:es2021.transform failed." : rustError);
        }

        JsValueRef returnValue = JS_INVALID_REFERENCE;
        if (CreateUtf8StringValue(transformedSource, &returnValue) != JsNoError)
        {
            return JS_INVALID_REFERENCE;
        }

        return returnValue;
    }

    JsErrorCode CreateInfoPackageObject(JsValueRef* packageObject)
    {
        JsErrorCode errorCode = JsCreateObject(packageObject);
        if (errorCode != JsNoError)
        {
            return errorCode;
        }

        return InstallMethod(*packageObject, "version", InfoVersionCallback);
    }

    JsErrorCode CreateFsPackageObject(JsValueRef* packageObject)
    {
        JsErrorCode errorCode = JsCreateObject(packageObject);
        if (errorCode != JsNoError)
        {
            return errorCode;
        }

        errorCode = InstallMethod(*packageObject, "readFileSync", FsReadFileSyncCallback);
        if (errorCode != JsNoError)
        {
            return errorCode;
        }

        errorCode = InstallMethod(*packageObject, "writeFileSync", FsWriteFileSyncCallback);
        if (errorCode != JsNoError)
        {
            return errorCode;
        }

        return InstallMethod(*packageObject, "existsSync", FsExistsSyncCallback);
    }

    JsErrorCode CreateReqwestPackageObject(JsValueRef* packageObject)
    {
        JsErrorCode errorCode = JsCreateObject(packageObject);
        if (errorCode != JsNoError)
        {
            return errorCode;
        }

        errorCode = InstallMethod(*packageObject, "get", ReqwestGetCallback);
        if (errorCode != JsNoError)
        {
            return errorCode;
        }

        errorCode = InstallMethod(*packageObject, "post", ReqwestPostCallback);
        if (errorCode != JsNoError)
        {
            return errorCode;
        }

        errorCode = InstallMethod(*packageObject, "fetch", ReqwestFetchCallback);
        if (errorCode != JsNoError)
        {
            return errorCode;
        }

        return InstallMethod(*packageObject, "downloadFetch", ReqwestDownloadFetchCallback);
    }

    JsErrorCode CreateEs2020PackageObject(JsValueRef* packageObject)
    {
        JsErrorCode errorCode = JsCreateObject(packageObject);
        if (errorCode != JsNoError)
        {
            return errorCode;
        }

        return InstallMethod(*packageObject, "analyze", Es2020AnalyzeCallback);
    }

    JsErrorCode CreateEs2021PackageObject(JsValueRef* packageObject)
    {
        JsErrorCode errorCode = JsCreateObject(packageObject);
        if (errorCode != JsNoError)
        {
            return errorCode;
        }

        errorCode = InstallMethod(*packageObject, "analyze", Es2021AnalyzeCallback);
        if (errorCode != JsNoError)
        {
            return errorCode;
        }

        return InstallMethod(*packageObject, "transform", Es2021TransformCallback);
    }

    JsErrorCode CreateSystemPackageObject(const std::string& moduleName, JsValueRef* packageObject)
    {
        if (packageObject == nullptr)
        {
            return JsErrorInvalidArgument;
        }

        *packageObject = JS_INVALID_REFERENCE;

        if (moduleName == "chakra:info")
        {
            return CreateInfoPackageObject(packageObject);
        }

        if (moduleName == "chakra:fs")
        {
            return CreateFsPackageObject(packageObject);
        }

        if (moduleName == "chakra:reqwest")
        {
            return CreateReqwestPackageObject(packageObject);
        }

        if (moduleName == "chakra:es2020")
        {
            return CreateEs2020PackageObject(packageObject);
        }

        if (moduleName == "chakra:es2021")
        {
            return CreateEs2021PackageObject(packageObject);
        }

        return JsErrorInvalidArgument;
    }

    JsValueRef CHAKRA_CALLBACK ChakraSystemRequireCallback(
        JsValueRef callee,
        bool isConstructCall,
        JsValueRef* arguments,
        unsigned short argumentCount,
        void* callbackState)
    {
        UNREFERENCED_PARAMETER(callee);
        UNREFERENCED_PARAMETER(isConstructCall);
        UNREFERENCED_PARAMETER(callbackState);

        if (argumentCount < 2)
        {
            return SetExceptionAndReturnInvalidReference("require expects a module specifier, for example require('chakra:info').");
        }

        std::string moduleName;
        const JsErrorCode toStringError = ConvertValueToUtf8String(arguments[1], &moduleName);
        if (toStringError != JsNoError)
        {
            return JS_INVALID_REFERENCE;
        }

        if (moduleName.length() >= 7 && moduleName.compare(0, 7, "chakra:") == 0)
        {
            JsValueRef packageObject = JS_INVALID_REFERENCE;
            const JsErrorCode createError = CreateSystemPackageObject(moduleName, &packageObject);
            if (createError != JsNoError)
            {
                return SetExceptionAndReturnInvalidReference(kUnknownPackageMessage);
            }

            return packageObject;
        }

        std::string baseDirectory;
        if (argumentCount > 2)
        {
            JsValueType baseType = JsUndefined;
            if (JsGetValueType(arguments[2], &baseType) == JsNoError && baseType != JsUndefined && baseType != JsNull)
            {
                const JsErrorCode baseError = ConvertValueToUtf8String(arguments[2], &baseDirectory);
                if (baseError != JsNoError)
                {
                    return JS_INVALID_REFERENCE;
                }
            }
        }

        JsValueRef moduleValue = JS_INVALID_REFERENCE;
        const JsErrorCode loadError = LoadFileModule(moduleName, baseDirectory, &moduleValue);
        if (loadError != JsNoError)
        {
            return JS_INVALID_REFERENCE;
        }

        return moduleValue;
    }

    // ── FFI Support ───────────────────────────────────────────────────────────

    enum class FfiPrimitiveType
    {
        I32,
        U32,
        I64,
        U64,
        Pointer,
        CString
    };

    struct FfiTypeDescriptor
    {
        bool isStruct;
        bool isUnion;
        FfiPrimitiveType primitive;
        std::vector<std::pair<std::string, FfiTypeDescriptor>> fields;

        FfiTypeDescriptor()
            : isStruct(false), isUnion(false), primitive(FfiPrimitiveType::U64)
        {
        }
    };

    struct FfiBoundFunctionState
    {
        void* functionPointer;
        bool hasSignature;
        std::vector<FfiTypeDescriptor> argumentTypes;
        bool hasReturnType;
        FfiTypeDescriptor returnType;

        FfiBoundFunctionState()
            : functionPointer(nullptr), hasSignature(false), hasReturnType(false)
        {
        }
    };

    std::vector<std::unique_ptr<FfiBoundFunctionState>> g_ffiBoundFunctionStates;

    bool TryParsePrimitiveTypeName(const std::string& typeName, FfiPrimitiveType* primitiveType)
    {
        if (primitiveType == nullptr)
        {
            return false;
        }

        std::string lowerName = typeName;
        for (size_t i = 0; i < lowerName.length(); ++i)
        {
            lowerName[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(lowerName[i])));
        }

        if (lowerName == "i32")
        {
            *primitiveType = FfiPrimitiveType::I32;
            return true;
        }

        if (lowerName == "u32")
        {
            *primitiveType = FfiPrimitiveType::U32;
            return true;
        }

        if (lowerName == "i64" || lowerName == "isize")
        {
            *primitiveType = FfiPrimitiveType::I64;
            return true;
        }

        if (lowerName == "u64" || lowerName == "usize")
        {
            *primitiveType = FfiPrimitiveType::U64;
            return true;
        }

        if (lowerName == "ptr" || lowerName == "pointer" || lowerName == "void*" || lowerName == "i32*" || lowerName == "u32*" || lowerName == "i64*" || lowerName == "u64*")
        {
            *primitiveType = FfiPrimitiveType::Pointer;
            return true;
        }

        if (lowerName == "string" || lowerName == "cstring" || lowerName == "char*" || lowerName == "const char*")
        {
            *primitiveType = FfiPrimitiveType::CString;
            return true;
        }

        return false;
    }

    JsErrorCode GetObjectKeysArray(JsValueRef object, JsValueRef* keysArray)
    {
        if (keysArray == nullptr)
        {
            return JsErrorInvalidArgument;
        }

        JsValueRef globalObject = JS_INVALID_REFERENCE;
        JsErrorCode errorCode = JsGetGlobalObject(&globalObject);
        if (errorCode != JsNoError)
        {
            return errorCode;
        }

        JsValueRef objectConstructor = JS_INVALID_REFERENCE;
        errorCode = GetPropertyByName(globalObject, "Object", &objectConstructor);
        if (errorCode != JsNoError)
        {
            return errorCode;
        }

        JsValueRef keysFunction = JS_INVALID_REFERENCE;
        errorCode = GetPropertyByName(objectConstructor, "keys", &keysFunction);
        if (errorCode != JsNoError)
        {
            return errorCode;
        }

        JsValueRef callArguments[2] = { objectConstructor, object };
        return JsCallFunction(keysFunction, callArguments, 2, keysArray);
    }

    bool IsStringConstructor(JsValueRef value)
    {
        JsValueRef globalObject = JS_INVALID_REFERENCE;
        if (JsGetGlobalObject(&globalObject) != JsNoError)
        {
            return false;
        }

        JsValueRef stringConstructor = JS_INVALID_REFERENCE;
        if (GetPropertyByName(globalObject, "String", &stringConstructor) != JsNoError)
        {
            return false;
        }

        return stringConstructor == value;
    }

    bool ParseFfiTypeDescriptor(
        JsValueRef value,
        FfiTypeDescriptor* descriptor,
        std::string* errorMessage)
    {
        if (descriptor == nullptr)
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = "Internal type parser error";
            }

            return false;
        }

        JsValueType valueType = JsUndefined;
        if (JsGetValueType(value, &valueType) != JsNoError)
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = "Unable to inspect FFI type descriptor";
            }

            return false;
        }

        if (valueType == JsString)
        {
            std::string typeName;
            if (ConvertValueToUtf8String(value, &typeName) != JsNoError)
            {
                if (errorMessage != nullptr)
                {
                    *errorMessage = "Invalid FFI type string";
                }

                return false;
            }

            FfiPrimitiveType primitiveType;
            if (!TryParsePrimitiveTypeName(typeName, &primitiveType))
            {
                if (errorMessage != nullptr)
                {
                    *errorMessage = "Unsupported FFI type: " + typeName;
                }

                return false;
            }

            descriptor->isStruct = false;
            descriptor->isUnion = false;
            descriptor->primitive = primitiveType;
            descriptor->fields.clear();
            return true;
        }

        if (valueType == JsFunction && IsStringConstructor(value))
        {
            descriptor->isStruct = false;
            descriptor->isUnion = false;
            descriptor->primitive = FfiPrimitiveType::CString;
            descriptor->fields.clear();
            return true;
        }

        if (valueType != JsObject)
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = "FFI type must be a string, String constructor, or object";
            }

            return false;
        }

        bool treatAsUnion = false;
        JsValueRef kindValue = JS_INVALID_REFERENCE;
        if (GetPropertyByName(value, "__ffiTypeKind", &kindValue) == JsNoError)
        {
            JsValueType kindType = JsUndefined;
            if (JsGetValueType(kindValue, &kindType) == JsNoError && kindType == JsString)
            {
                std::string kindText;
                if (ConvertValueToUtf8String(kindValue, &kindText) == JsNoError)
                {
                    if (kindText == "primitive")
                    {
                        JsValueRef nameValue = JS_INVALID_REFERENCE;
                        if (GetPropertyByName(value, "name", &nameValue) != JsNoError)
                        {
                            if (errorMessage != nullptr)
                            {
                                *errorMessage = "Invalid primitive type descriptor";
                            }

                            return false;
                        }

                        return ParseFfiTypeDescriptor(nameValue, descriptor, errorMessage);
                    }

                    if (kindText == "struct")
                    {
                        JsValueRef fieldsValue = JS_INVALID_REFERENCE;
                        if (GetPropertyByName(value, "fields", &fieldsValue) != JsNoError)
                        {
                            if (errorMessage != nullptr)
                            {
                                *errorMessage = "Invalid struct type descriptor";
                            }

                            return false;
                        }

                        value = fieldsValue;
                    }

                    if (kindText == "union")
                    {
                        JsValueRef fieldsValue = JS_INVALID_REFERENCE;
                        if (GetPropertyByName(value, "fields", &fieldsValue) != JsNoError)
                        {
                            if (errorMessage != nullptr)
                            {
                                *errorMessage = "Invalid union type descriptor";
                            }

                            return false;
                        }

                        value = fieldsValue;
                        treatAsUnion = true;
                    }
                }
            }
        }

        JsValueRef keysArray = JS_INVALID_REFERENCE;
        if (GetObjectKeysArray(value, &keysArray) != JsNoError)
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = "Unable to read struct field names";
            }

            return false;
        }

        JsValueRef lengthValue = JS_INVALID_REFERENCE;
        if (GetPropertyByName(keysArray, "length", &lengthValue) != JsNoError)
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = "Unable to read struct field count";
            }

            return false;
        }

        double keyCountNumber = 0.0;
        if (JsNumberToDouble(lengthValue, &keyCountNumber) != JsNoError)
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = "Invalid struct field count";
            }

            return false;
        }

        descriptor->isStruct = true;
        descriptor->isUnion = treatAsUnion;
        descriptor->fields.clear();

        const uint32_t keyCount = static_cast<uint32_t>(keyCountNumber);
        for (uint32_t index = 0; index < keyCount; ++index)
        {
            JsValueRef keyValue = JS_INVALID_REFERENCE;
            const std::string indexText = std::to_string(index);
            if (GetPropertyByName(keysArray, indexText.c_str(), &keyValue) != JsNoError)
            {
                if (errorMessage != nullptr)
                {
                    *errorMessage = "Unable to read struct field name";
                }

                return false;
            }

            std::string fieldName;
            if (ConvertValueToUtf8String(keyValue, &fieldName) != JsNoError)
            {
                if (errorMessage != nullptr)
                {
                    *errorMessage = "Struct field name must be a string";
                }

                return false;
            }

            JsValueRef fieldTypeValue = JS_INVALID_REFERENCE;
            if (GetPropertyByName(value, fieldName.c_str(), &fieldTypeValue) != JsNoError)
            {
                if (errorMessage != nullptr)
                {
                    *errorMessage = "Unable to read struct field type for '" + fieldName + "'";
                }

                return false;
            }

            FfiTypeDescriptor fieldDescriptor;
            if (!ParseFfiTypeDescriptor(fieldTypeValue, &fieldDescriptor, errorMessage))
            {
                return false;
            }

            descriptor->fields.push_back(std::make_pair(fieldName, fieldDescriptor));
        }

        return true;
    }

    bool MarshalValueToFfiArgs(
        JsValueRef value,
        const FfiTypeDescriptor& descriptor,
        std::vector<std::string>* stringStorage,
        std::vector<uint64_t>* args,
        std::string* errorMessage)
    {
        if (args == nullptr || stringStorage == nullptr)
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = "Internal marshal error";
            }

            return false;
        }

        if (descriptor.isStruct)
        {
            JsValueType valueType = JsUndefined;
            if (JsGetValueType(value, &valueType) != JsNoError || valueType != JsObject)
            {
                if (errorMessage != nullptr)
                {
                    *errorMessage = "Struct argument must be an object";
                }

                return false;
            }

            if (descriptor.isUnion)
            {
                for (size_t fieldIndex = 0; fieldIndex < descriptor.fields.size(); ++fieldIndex)
                {
                    const std::string& fieldName = descriptor.fields[fieldIndex].first;
                    const FfiTypeDescriptor& fieldType = descriptor.fields[fieldIndex].second;

                    JsValueRef fieldValue = JS_INVALID_REFERENCE;
                    if (GetPropertyByName(value, fieldName.c_str(), &fieldValue) != JsNoError)
                    {
                        continue;
                    }

                    JsValueType fieldValueType = JsUndefined;
                    if (JsGetValueType(fieldValue, &fieldValueType) == JsNoError && fieldValueType != JsUndefined)
                    {
                        return MarshalValueToFfiArgs(fieldValue, fieldType, stringStorage, args, errorMessage);
                    }
                }

                if (errorMessage != nullptr)
                {
                    *errorMessage = "Union argument must provide at least one defined field";
                }

                return false;
            }

            for (size_t fieldIndex = 0; fieldIndex < descriptor.fields.size(); ++fieldIndex)
            {
                const std::string& fieldName = descriptor.fields[fieldIndex].first;
                const FfiTypeDescriptor& fieldType = descriptor.fields[fieldIndex].second;

                JsValueRef fieldValue = JS_INVALID_REFERENCE;
                if (GetPropertyByName(value, fieldName.c_str(), &fieldValue) != JsNoError)
                {
                    if (errorMessage != nullptr)
                    {
                        *errorMessage = "Missing struct field: " + fieldName;
                    }

                    return false;
                }

                if (!MarshalValueToFfiArgs(fieldValue, fieldType, stringStorage, args, errorMessage))
                {
                    return false;
                }
            }

            return true;
        }

        if (descriptor.primitive == FfiPrimitiveType::CString)
        {
            JsValueType valueType = JsUndefined;
            if (JsGetValueType(value, &valueType) == JsNoError && (valueType == JsUndefined || valueType == JsNull))
            {
                args->push_back(0);
                return true;
            }

            std::string text;
            if (ConvertValueToUtf8String(value, &text) != JsNoError)
            {
                if (errorMessage != nullptr)
                {
                    *errorMessage = "CString argument must be a string";
                }

                return false;
            }

            stringStorage->push_back(text);
            args->push_back(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(stringStorage->back().c_str())));
            return true;
        }

        double numberValue = 0.0;
        if (JsNumberToDouble(value, &numberValue) != JsNoError)
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = "Numeric FFI argument expected";
            }

            return false;
        }

        switch (descriptor.primitive)
        {
        case FfiPrimitiveType::I32:
            args->push_back(static_cast<uint64_t>(static_cast<int32_t>(numberValue)));
            return true;
        case FfiPrimitiveType::U32:
            args->push_back(static_cast<uint64_t>(static_cast<uint32_t>(numberValue)));
            return true;
        case FfiPrimitiveType::I64:
            args->push_back(static_cast<uint64_t>(static_cast<int64_t>(numberValue)));
            return true;
        case FfiPrimitiveType::U64:
            args->push_back(static_cast<uint64_t>(numberValue));
            return true;
        case FfiPrimitiveType::Pointer:
            args->push_back(static_cast<uint64_t>(static_cast<uintptr_t>(numberValue)));
            return true;
        default:
            if (errorMessage != nullptr)
            {
                *errorMessage = "Unsupported FFI primitive type";
            }

            return false;
        }
    }

    JsErrorCode ConvertFfiResultToJsValue(uint64_t rawResult, const FfiTypeDescriptor& descriptor, JsValueRef* result)
    {
        if (result == nullptr)
        {
            return JsErrorInvalidArgument;
        }

        if (descriptor.isStruct)
        {
            return JsDoubleToNumber(static_cast<double>(rawResult), result);
        }

        switch (descriptor.primitive)
        {
        case FfiPrimitiveType::I32:
            return JsDoubleToNumber(static_cast<double>(static_cast<int32_t>(rawResult)), result);
        case FfiPrimitiveType::U32:
            return JsDoubleToNumber(static_cast<double>(static_cast<uint32_t>(rawResult)), result);
        case FfiPrimitiveType::I64:
            return JsDoubleToNumber(static_cast<double>(static_cast<int64_t>(rawResult)), result);
        case FfiPrimitiveType::U64:
            return JsDoubleToNumber(static_cast<double>(rawResult), result);
        case FfiPrimitiveType::Pointer:
            return JsDoubleToNumber(static_cast<double>(static_cast<uintptr_t>(rawResult)), result);
        case FfiPrimitiveType::CString:
        {
            const char* text = reinterpret_cast<const char*>(static_cast<uintptr_t>(rawResult));
            if (text == nullptr)
            {
                return JsGetUndefinedValue(result);
            }

            return JsCreateString(text, strlen(text), result);
        }
        default:
            return JsDoubleToNumber(static_cast<double>(rawResult), result);
        }
    }

    JsValueRef CHAKRA_CALLBACK FfiTypeCallback(
        _In_ JsValueRef callee,
        _In_ bool isConstructCall,
        _In_ JsValueRef *arguments,
        _In_ unsigned short argumentCount,
        _In_opt_ void *callbackState)
    {
        UNREFERENCED_PARAMETER(callee);
        UNREFERENCED_PARAMETER(isConstructCall);
        UNREFERENCED_PARAMETER(callbackState);

        if (argumentCount < 2)
        {
            return SetExceptionAndReturnInvalidReference("ffi.type requires a type descriptor");
        }

        JsValueRef input = arguments[1];
        JsValueType inputType = JsUndefined;
        if (JsGetValueType(input, &inputType) != JsNoError)
        {
            return SetExceptionAndReturnInvalidReference("ffi.type: invalid descriptor");
        }

        if (inputType == JsString || (inputType == JsFunction && IsStringConstructor(input)))
        {
            std::string primitiveName = "string";
            if (inputType == JsString)
            {
                primitiveName.clear();
                if (ConvertValueToUtf8String(input, &primitiveName) != JsNoError)
                {
                    return SetExceptionAndReturnInvalidReference("ffi.type: invalid primitive type");
                }
            }

            JsValueRef descriptor = JS_INVALID_REFERENCE;
            JsCreateObject(&descriptor);

            JsValueRef kindValue = JS_INVALID_REFERENCE;
            JsCreateString("primitive", 9, &kindValue);
            SetPropertyByName(descriptor, "__ffiTypeKind", kindValue);

            JsValueRef nameValue = JS_INVALID_REFERENCE;
            JsCreateString(primitiveName.c_str(), primitiveName.length(), &nameValue);
            SetPropertyByName(descriptor, "name", nameValue);
            return descriptor;
        }

        if (inputType == JsObject)
        {
            JsValueRef descriptor = JS_INVALID_REFERENCE;
            JsCreateObject(&descriptor);

            JsValueRef kindValue = JS_INVALID_REFERENCE;
            JsCreateString("struct", 6, &kindValue);
            SetPropertyByName(descriptor, "__ffiTypeKind", kindValue);
            SetPropertyByName(descriptor, "fields", input);
            return descriptor;
        }

        return SetExceptionAndReturnInvalidReference("ffi.type expects a string, String, or object");
    }

    JsErrorCode CreateCompositeTypeDescriptorValue(const char* kindName, JsValueRef fieldsValue, JsValueRef* descriptorValue)
    {
        if (kindName == nullptr || descriptorValue == nullptr)
        {
            return JsErrorInvalidArgument;
        }

        *descriptorValue = JS_INVALID_REFERENCE;

        JsValueRef descriptor = JS_INVALID_REFERENCE;
        JsErrorCode errorCode = JsCreateObject(&descriptor);
        if (errorCode != JsNoError)
        {
            return errorCode;
        }

        JsValueRef kindValue = JS_INVALID_REFERENCE;
        errorCode = JsCreateString(kindName, strlen(kindName), &kindValue);
        if (errorCode != JsNoError)
        {
            return errorCode;
        }

        errorCode = SetPropertyByName(descriptor, "__ffiTypeKind", kindValue);
        if (errorCode != JsNoError)
        {
            return errorCode;
        }

        errorCode = SetPropertyByName(descriptor, "fields", fieldsValue);
        if (errorCode != JsNoError)
        {
            return errorCode;
        }

        *descriptorValue = descriptor;
        return JsNoError;
    }

    JsValueRef CHAKRA_CALLBACK FfiStructCallback(
        _In_ JsValueRef callee,
        _In_ bool isConstructCall,
        _In_ JsValueRef *arguments,
        _In_ unsigned short argumentCount,
        _In_opt_ void *callbackState)
    {
        UNREFERENCED_PARAMETER(callee);
        UNREFERENCED_PARAMETER(isConstructCall);
        UNREFERENCED_PARAMETER(callbackState);

        if (argumentCount < 2)
        {
            return SetExceptionAndReturnInvalidReference("ffi.struct requires a fields object");
        }

        JsValueType fieldsType = JsUndefined;
        if (JsGetValueType(arguments[1], &fieldsType) != JsNoError || fieldsType != JsObject)
        {
            return SetExceptionAndReturnInvalidReference("ffi.struct: fields must be an object");
        }

        JsValueRef descriptor = JS_INVALID_REFERENCE;
        if (CreateCompositeTypeDescriptorValue("struct", arguments[1], &descriptor) != JsNoError)
        {
            return SetExceptionAndReturnInvalidReference("ffi.struct: failed to create type descriptor");
        }

        return descriptor;
    }

    JsValueRef CHAKRA_CALLBACK FfiUnionCallback(
        _In_ JsValueRef callee,
        _In_ bool isConstructCall,
        _In_ JsValueRef *arguments,
        _In_ unsigned short argumentCount,
        _In_opt_ void *callbackState)
    {
        UNREFERENCED_PARAMETER(callee);
        UNREFERENCED_PARAMETER(isConstructCall);
        UNREFERENCED_PARAMETER(callbackState);

        if (argumentCount < 2)
        {
            return SetExceptionAndReturnInvalidReference("ffi.union requires a fields object");
        }

        JsValueType fieldsType = JsUndefined;
        if (JsGetValueType(arguments[1], &fieldsType) != JsNoError || fieldsType != JsObject)
        {
            return SetExceptionAndReturnInvalidReference("ffi.union: fields must be an object");
        }

        JsValueRef descriptor = JS_INVALID_REFERENCE;
        if (CreateCompositeTypeDescriptorValue("union", arguments[1], &descriptor) != JsNoError)
        {
            return SetExceptionAndReturnInvalidReference("ffi.union: failed to create type descriptor");
        }

        return descriptor;
    }

    JsErrorCode CreatePrimitiveTypeDescriptorValue(const char* primitiveName, JsValueRef* descriptorValue)
    {
        if (primitiveName == nullptr || descriptorValue == nullptr)
        {
            return JsErrorInvalidArgument;
        }

        *descriptorValue = JS_INVALID_REFERENCE;

        JsValueRef descriptor = JS_INVALID_REFERENCE;
        JsErrorCode errorCode = JsCreateObject(&descriptor);
        if (errorCode != JsNoError)
        {
            return errorCode;
        }

        JsValueRef kindValue = JS_INVALID_REFERENCE;
        errorCode = JsCreateString("primitive", 9, &kindValue);
        if (errorCode != JsNoError)
        {
            return errorCode;
        }

        errorCode = SetPropertyByName(descriptor, "__ffiTypeKind", kindValue);
        if (errorCode != JsNoError)
        {
            return errorCode;
        }

        JsValueRef nameValue = JS_INVALID_REFERENCE;
        errorCode = JsCreateString(primitiveName, strlen(primitiveName), &nameValue);
        if (errorCode != JsNoError)
        {
            return errorCode;
        }

        errorCode = SetPropertyByName(descriptor, "name", nameValue);
        if (errorCode != JsNoError)
        {
            return errorCode;
        }

        *descriptorValue = descriptor;
        return JsNoError;
    }

    JsValueRef CHAKRA_CALLBACK FfiPtrCallback(
        _In_ JsValueRef callee,
        _In_ bool isConstructCall,
        _In_ JsValueRef *arguments,
        _In_ unsigned short argumentCount,
        _In_opt_ void *callbackState)
    {
        UNREFERENCED_PARAMETER(callee);
        UNREFERENCED_PARAMETER(isConstructCall);
        UNREFERENCED_PARAMETER(callbackState);

        JsValueRef descriptor = JS_INVALID_REFERENCE;
        if (CreatePrimitiveTypeDescriptorValue("ptr", &descriptor) != JsNoError)
        {
            return SetExceptionAndReturnInvalidReference("ffi.ptr: failed to create pointer type descriptor");
        }

        if (argumentCount > 1)
        {
            JsValueType targetType = JsUndefined;
            if (JsGetValueType(arguments[1], &targetType) == JsNoError && targetType != JsUndefined && targetType != JsNull)
            {
                SetPropertyByName(descriptor, "to", arguments[1]);
            }
        }

        return descriptor;
    }

    JsErrorCode InstallFfiTypesObject(JsValueRef ffiObj)
    {
        JsValueRef typesObject = JS_INVALID_REFERENCE;
        JsErrorCode errorCode = JsCreateObject(&typesObject);
        if (errorCode != JsNoError)
        {
            return errorCode;
        }

        struct FfiTypeEntry
        {
            const char* propertyName;
            const char* primitiveName;
        };

        static const FfiTypeEntry entries[] =
        {
            { "i32", "i32" },
            { "u32", "u32" },
            { "i64", "i64" },
            { "u64", "u64" },
            { "isize", "isize" },
            { "usize", "usize" },
            { "ptr", "ptr" },
            { "pointer", "pointer" },
            { "void", "void" },
            { "i32Ptr", "i32*" },
            { "u32Ptr", "u32*" },
            { "i64Ptr", "i64*" },
            { "u64Ptr", "u64*" },
            { "string", "string" },
            { "cstring", "cstring" },
            { "charPtr", "char*" },
            { "constCharPtr", "const char*" }
        };

        for (size_t index = 0; index < sizeof(entries) / sizeof(entries[0]); ++index)
        {
            JsValueRef descriptor = JS_INVALID_REFERENCE;
            errorCode = CreatePrimitiveTypeDescriptorValue(entries[index].primitiveName, &descriptor);
            if (errorCode != JsNoError)
            {
                return errorCode;
            }

            errorCode = SetPropertyByName(typesObject, entries[index].propertyName, descriptor);
            if (errorCode != JsNoError)
            {
                return errorCode;
            }
        }

        return SetPropertyByName(ffiObj, "types", typesObject);
    }

    JsValueRef CHAKRA_CALLBACK FfiDlopenCallback(
        _In_ JsValueRef callee,
        _In_ bool isConstructCall,
        _In_ JsValueRef *arguments,
        _In_ unsigned short argumentCount,
        _In_opt_ void *callbackState)
    {
        UNREFERENCED_PARAMETER(callee);
        UNREFERENCED_PARAMETER(isConstructCall);
        UNREFERENCED_PARAMETER(callbackState);

        if (argumentCount < 2)
        {
            return SetExceptionAndReturnInvalidReference("ffi.dlopen requires a path string");
        }

        std::string pathStr;
        if (ConvertValueToUtf8String(arguments[1], &pathStr) != JsNoError)
        {
            return SetExceptionAndReturnInvalidReference("ffi.dlopen: path must be a string");
        }

        std::string errorMessage;
        if (!EnsureFfiApiLoaded(&errorMessage))
        {
            return SetExceptionAndReturnInvalidReference(errorMessage.c_str());
        }

        ChakraFfiHandle handle = g_ffiApi.dlopen(
            reinterpret_cast<const uint8_t*>(pathStr.c_str()),
            pathStr.length()
        );

        if (chakra_ffi_handle_is_null(handle))
        {
            SetFfiErrorMessage(&errorMessage, "ffi.dlopen: failed to load library");
            return SetExceptionAndReturnInvalidReference(errorMessage.c_str());
        }

        JsValueRef handleObj = JS_INVALID_REFERENCE;
        JsCreateObject(&handleObj);
        
        JsValueRef handleValue = JS_INVALID_REFERENCE;
        JsDoubleToNumber(static_cast<double>(handle.handle), &handleValue);
        SetPropertyByName(handleObj, "handle", handleValue);

        return handleObj;
    }

    JsValueRef CHAKRA_CALLBACK FfiDlsymCallback(
        _In_ JsValueRef callee,
        _In_ bool isConstructCall,
        _In_ JsValueRef *arguments,
        _In_ unsigned short argumentCount,
        _In_opt_ void *callbackState)
    {
        UNREFERENCED_PARAMETER(callee);
        UNREFERENCED_PARAMETER(isConstructCall);
        UNREFERENCED_PARAMETER(callbackState);

        if (argumentCount < 3)
        {
            return SetExceptionAndReturnInvalidReference("ffi.dlsym requires (handle, symbol)");
        }

        double handleNum = 0.0;
        if (JsNumberToDouble(arguments[1], &handleNum) != JsNoError)
        {
            return SetExceptionAndReturnInvalidReference("ffi.dlsym: handle must be a number");
        }

        std::string symbolStr;
        if (ConvertValueToUtf8String(arguments[2], &symbolStr) != JsNoError)
        {
            return SetExceptionAndReturnInvalidReference("ffi.dlsym: symbol must be a string");
        }

        ChakraFfiHandle handle;
        handle.handle = static_cast<uint64_t>(handleNum);

        std::string errorMessage;
        if (!EnsureFfiApiLoaded(&errorMessage))
        {
            return SetExceptionAndReturnInvalidReference(errorMessage.c_str());
        }

        void* funcPtr = g_ffiApi.dlsym(
            handle,
            reinterpret_cast<const uint8_t*>(symbolStr.c_str()),
            symbolStr.length() + 1
        );

        if (funcPtr == nullptr)
        {
            SetFfiErrorMessage(&errorMessage, "ffi.dlsym: symbol not found");
            return SetExceptionAndReturnInvalidReference(errorMessage.c_str());
        }

        JsValueRef ptrObj = JS_INVALID_REFERENCE;
        JsCreateObject(&ptrObj);

        JsValueRef ptrValue = JS_INVALID_REFERENCE;
        JsDoubleToNumber(static_cast<double>(reinterpret_cast<uintptr_t>(funcPtr)), &ptrValue);
        SetPropertyByName(ptrObj, "ptr", ptrValue);

        return ptrObj;
    }

    JsValueRef CHAKRA_CALLBACK FfiCallCallback(
        _In_ JsValueRef callee,
        _In_ bool isConstructCall,
        _In_ JsValueRef *arguments,
        _In_ unsigned short argumentCount,
        _In_opt_ void *callbackState)
    {
        UNREFERENCED_PARAMETER(callee);
        UNREFERENCED_PARAMETER(isConstructCall);
        UNREFERENCED_PARAMETER(callbackState);

        if (argumentCount < 2)
        {
            return SetExceptionAndReturnInvalidReference("ffi.call requires (funcPtr, args)");
        }

        double ptrNum = 0.0;
        if (JsNumberToDouble(arguments[1], &ptrNum) != JsNoError)
        {
            return SetExceptionAndReturnInvalidReference("ffi.call: funcPtr must be a number");
        }

        void* funcPtr = reinterpret_cast<void*>(static_cast<uintptr_t>(ptrNum));

        // Collect arguments array
        uint64_t callArgs[8] = {0};
        uint32_t argc = 0;

        if (argumentCount > 2)
        {
            JsValueType argsType = JsUndefined;
            if (JsGetValueType(arguments[2], &argsType) == JsNoError && argsType == JsArray)
            {
                JsValueRef lengthProp = JS_INVALID_REFERENCE;
                if (GetPropertyByName(arguments[2], "length", &lengthProp) == JsNoError)
                {
                    double len = 0.0;
                    if (JsNumberToDouble(lengthProp, &len) == JsNoError)
                    {
                        argc = static_cast<uint32_t>(len > 8 ? 8 : len);
                        for (uint32_t i = 0; i < argc; ++i)
                        {
                            JsValueRef elem = JS_INVALID_REFERENCE;
                            std::string indexStr = std::to_string(i);
                            if (GetPropertyByName(arguments[2], indexStr.c_str(), &elem) == JsNoError)
                            {
                                double val = 0.0;
                                JsNumberToDouble(elem, &val);
                                callArgs[i] = static_cast<uint64_t>(val);
                            }
                        }
                    }
                }
            }
        }

        std::string errorMessage;
        if (!EnsureFfiApiLoaded(&errorMessage))
        {
            return SetExceptionAndReturnInvalidReference(errorMessage.c_str());
        }

        uint64_t result = g_ffiApi.call(funcPtr, argc, callArgs);

        JsValueRef resultValue = JS_INVALID_REFERENCE;
        JsDoubleToNumber(static_cast<double>(result), &resultValue);
        return resultValue;
    }

    JsValueRef CHAKRA_CALLBACK FfiBoundFunctionCallback(
        _In_ JsValueRef callee,
        _In_ bool isConstructCall,
        _In_ JsValueRef *arguments,
        _In_ unsigned short argumentCount,
        _In_opt_ void *callbackState)
    {
        UNREFERENCED_PARAMETER(callee);
        UNREFERENCED_PARAMETER(isConstructCall);

        if (callbackState == nullptr)
        {
            return SetExceptionAndReturnInvalidReference("ffi.func: invalid bound function state");
        }

        FfiBoundFunctionState* state = reinterpret_cast<FfiBoundFunctionState*>(callbackState);
        if (state == nullptr || state->functionPointer == nullptr)
        {
            return SetExceptionAndReturnInvalidReference("ffi.func: invalid bound function state");
        }

        void* funcPtr = state->functionPointer;

        uint64_t callArgs[8] = { 0 };
        uint32_t argc = 0;
        if (!state->hasSignature)
        {
            std::vector<std::string> untypedStringStorage;
            if (argumentCount > 1)
            {
                const uint32_t providedArgCount = static_cast<uint32_t>(argumentCount - 1);
                argc = providedArgCount > 8 ? 8 : providedArgCount;
                for (uint32_t i = 0; i < argc; ++i)
                {
                    JsValueType argType = JsUndefined;
                    if (JsGetValueType(arguments[i + 1], &argType) != JsNoError)
                    {
                        return SetExceptionAndReturnInvalidReference("ffi.func: unable to inspect argument type");
                    }

                    if (argType == JsUndefined || argType == JsNull)
                    {
                        callArgs[i] = 0;
                        continue;
                    }

                    if (argType == JsString)
                    {
                        std::string text;
                        if (ConvertValueToUtf8String(arguments[i + 1], &text) != JsNoError)
                        {
                            return SetExceptionAndReturnInvalidReference("ffi.func: failed to convert string argument");
                        }

                        untypedStringStorage.push_back(text);
                        callArgs[i] = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(untypedStringStorage.back().c_str()));
                        continue;
                    }

                    if (argType == JsBoolean)
                    {
                        bool boolValue = false;
                        if (JsBooleanToBool(arguments[i + 1], &boolValue) != JsNoError)
                        {
                            return SetExceptionAndReturnInvalidReference("ffi.func: failed to convert boolean argument");
                        }

                        callArgs[i] = boolValue ? 1 : 0;
                        continue;
                    }

                    double value = 0.0;
                    if (JsNumberToDouble(arguments[i + 1], &value) != JsNoError)
                    {
                        return SetExceptionAndReturnInvalidReference("ffi.func: untyped mode accepts number/string/bool/null; use signature for structs/typed pointers");
                    }

                    callArgs[i] = static_cast<uint64_t>(value);
                }
            }
        }
        else
        {
            const uint32_t providedArgCount = argumentCount > 0 ? static_cast<uint32_t>(argumentCount - 1) : 0;
            if (providedArgCount != state->argumentTypes.size())
            {
                return SetExceptionAndReturnInvalidReference("ffi.func: argument count does not match signature");
            }

            std::vector<uint64_t> marshaledArgs;
            std::vector<std::string> stringStorage;
            std::string marshalError;

            for (uint32_t i = 0; i < providedArgCount; ++i)
            {
                if (!MarshalValueToFfiArgs(arguments[i + 1], state->argumentTypes[i], &stringStorage, &marshaledArgs, &marshalError))
                {
                    return SetExceptionAndReturnInvalidReference("ffi.func: " + marshalError);
                }
            }

            if (marshaledArgs.size() > 8)
            {
                return SetExceptionAndReturnInvalidReference("ffi.func: marshaled argument count exceeds ABI limit (8)");
            }

            argc = static_cast<uint32_t>(marshaledArgs.size());
            for (uint32_t index = 0; index < argc; ++index)
            {
                callArgs[index] = marshaledArgs[index];
            }
        }

        std::string errorMessage;
        if (!EnsureFfiApiLoaded(&errorMessage))
        {
            return SetExceptionAndReturnInvalidReference(errorMessage.c_str());
        }

        const uint64_t result = g_ffiApi.call(funcPtr, argc, callArgs);

        JsValueRef resultValue = JS_INVALID_REFERENCE;
        if (state->hasReturnType)
        {
            if (ConvertFfiResultToJsValue(result, state->returnType, &resultValue) != JsNoError)
            {
                return SetExceptionAndReturnInvalidReference("ffi.func: failed to convert typed return value");
            }
        }
        else
        {
            JsDoubleToNumber(static_cast<double>(result), &resultValue);
        }

        return resultValue;
    }

    JsValueRef CHAKRA_CALLBACK FfiFuncCallback(
        _In_ JsValueRef callee,
        _In_ bool isConstructCall,
        _In_ JsValueRef *arguments,
        _In_ unsigned short argumentCount,
        _In_opt_ void *callbackState)
    {
        UNREFERENCED_PARAMETER(callee);
        UNREFERENCED_PARAMETER(isConstructCall);
        UNREFERENCED_PARAMETER(callbackState);

        if (argumentCount < 3)
        {
            return SetExceptionAndReturnInvalidReference("ffi.func requires (handle, symbol)");
        }

        double handleNum = 0.0;
        if (JsNumberToDouble(arguments[1], &handleNum) != JsNoError)
        {
            return SetExceptionAndReturnInvalidReference("ffi.func: handle must be a number");
        }

        std::string symbolStr;
        if (ConvertValueToUtf8String(arguments[2], &symbolStr) != JsNoError)
        {
            return SetExceptionAndReturnInvalidReference("ffi.func: symbol must be a string");
        }

        ChakraFfiHandle handle;
        handle.handle = static_cast<uint64_t>(handleNum);

        std::string errorMessage;
        if (!EnsureFfiApiLoaded(&errorMessage))
        {
            return SetExceptionAndReturnInvalidReference(errorMessage.c_str());
        }

        void* funcPtr = g_ffiApi.dlsym(
            handle,
            reinterpret_cast<const uint8_t*>(symbolStr.c_str()),
            symbolStr.length() + 1);

        if (funcPtr == nullptr)
        {
            SetFfiErrorMessage(&errorMessage, "ffi.func: symbol not found");
            return SetExceptionAndReturnInvalidReference(errorMessage.c_str());
        }

        std::unique_ptr<FfiBoundFunctionState> state(new FfiBoundFunctionState());
        state->functionPointer = funcPtr;

        if (argumentCount > 3)
        {
            JsValueType signatureType = JsUndefined;
            if (JsGetValueType(arguments[3], &signatureType) == JsNoError && signatureType != JsUndefined && signatureType != JsNull)
            {
                if (signatureType != JsObject)
                {
                    return SetExceptionAndReturnInvalidReference("ffi.func: signature must be an object");
                }

                JsValueRef argsTypeValue = JS_INVALID_REFERENCE;
                if (GetPropertyByName(arguments[3], "args", &argsTypeValue) == JsNoError)
                {
                    JsValueType argsType = JsUndefined;
                    if (JsGetValueType(argsTypeValue, &argsType) == JsNoError && argsType != JsUndefined && argsType != JsNull)
                    {
                        if (argsType != JsArray)
                        {
                            return SetExceptionAndReturnInvalidReference("ffi.func: signature.args must be an array");
                        }

                        JsValueRef argCountValue = JS_INVALID_REFERENCE;
                        if (GetPropertyByName(argsTypeValue, "length", &argCountValue) != JsNoError)
                        {
                            return SetExceptionAndReturnInvalidReference("ffi.func: failed to read signature.args length");
                        }

                        double argCountNumber = 0.0;
                        if (JsNumberToDouble(argCountValue, &argCountNumber) != JsNoError)
                        {
                            return SetExceptionAndReturnInvalidReference("ffi.func: invalid signature.args length");
                        }

                        const uint32_t typeCount = static_cast<uint32_t>(argCountNumber);
                        state->argumentTypes.clear();
                        state->argumentTypes.reserve(typeCount);
                        for (uint32_t index = 0; index < typeCount; ++index)
                        {
                            JsValueRef typeValue = JS_INVALID_REFERENCE;
                            const std::string indexText = std::to_string(index);
                            if (GetPropertyByName(argsTypeValue, indexText.c_str(), &typeValue) != JsNoError)
                            {
                                return SetExceptionAndReturnInvalidReference("ffi.func: failed to read signature argument type");
                            }

                            FfiTypeDescriptor typeDescriptor;
                            std::string typeError;
                            if (!ParseFfiTypeDescriptor(typeValue, &typeDescriptor, &typeError))
                            {
                                return SetExceptionAndReturnInvalidReference("ffi.func: " + typeError);
                            }

                            state->argumentTypes.push_back(typeDescriptor);
                        }

                        state->hasSignature = true;
                    }
                }

                JsValueRef returnTypeValue = JS_INVALID_REFERENCE;
                if (GetPropertyByName(arguments[3], "returns", &returnTypeValue) == JsNoError)
                {
                    JsValueType returnType = JsUndefined;
                    if (JsGetValueType(returnTypeValue, &returnType) == JsNoError && returnType != JsUndefined && returnType != JsNull)
                    {
                        std::string returnError;
                        if (!ParseFfiTypeDescriptor(returnTypeValue, &state->returnType, &returnError))
                        {
                            return SetExceptionAndReturnInvalidReference("ffi.func: " + returnError);
                        }

                        state->hasReturnType = true;
                        state->hasSignature = true;
                    }
                }
            }
        }

        JsValueRef functionValue = JS_INVALID_REFERENCE;
        if (JsCreateFunction(FfiBoundFunctionCallback, state.get(), &functionValue) != JsNoError)
        {
            return SetExceptionAndReturnInvalidReference("ffi.func: failed to create bound function");
        }

        g_ffiBoundFunctionStates.push_back(std::move(state));

        return functionValue;
    }

    JsValueRef CHAKRA_CALLBACK FfiCloseCallback(
        _In_ JsValueRef callee,
        _In_ bool isConstructCall,
        _In_ JsValueRef *arguments,
        _In_ unsigned short argumentCount,
        _In_opt_ void *callbackState)
    {
        UNREFERENCED_PARAMETER(callee);
        UNREFERENCED_PARAMETER(isConstructCall);
        UNREFERENCED_PARAMETER(callbackState);

        if (argumentCount < 2)
        {
            return SetExceptionAndReturnInvalidReference("ffi.close requires a handle");
        }

        double handleNum = 0.0;
        if (JsNumberToDouble(arguments[1], &handleNum) != JsNoError)
        {
            return SetExceptionAndReturnInvalidReference("ffi.close: handle must be a number");
        }

        ChakraFfiHandle handle;
        handle.handle = static_cast<uint64_t>(handleNum);

        std::string errorMessage;
        if (!EnsureFfiApiLoaded(&errorMessage))
        {
            return SetExceptionAndReturnInvalidReference(errorMessage.c_str());
        }

        int result = g_ffiApi.dlclose(handle);

        JsValueRef resultValue = JS_INVALID_REFERENCE;
        JsDoubleToNumber(static_cast<double>(result), &resultValue);
        return resultValue;
    }
}

CHAKRA_API JsInstallChakraSystemRequire(_Out_opt_ JsValueRef* requireFunction);

JsErrorCode JsEnsureChakraSystemRequireIfMissing()
{
    JsValueRef globalObject = JS_INVALID_REFERENCE;
    JsErrorCode errorCode = JsGetGlobalObject(&globalObject);
    if (errorCode != JsNoError)
    {
        return errorCode;
    }

    JsValueRef existingRequire = JS_INVALID_REFERENCE;
    errorCode = GetPropertyByName(globalObject, "require", &existingRequire);
    if (errorCode != JsNoError)
    {
        return errorCode;
    }

    JsValueType requireType = JsUndefined;
    errorCode = JsGetValueType(existingRequire, &requireType);
    if (errorCode != JsNoError)
    {
        return errorCode;
    }

    if (requireType != JsUndefined)
    {
        return JsNoError;
    }

    return JsInstallChakraSystemRequire(nullptr);
}

CHAKRA_API JsInstallChakraSystemRequire(_Out_opt_ JsValueRef* requireFunction)
{
    JsValueRef functionValue = JS_INVALID_REFERENCE;
    JsErrorCode errorCode = JsCreateFunction(ChakraSystemRequireCallback, nullptr, &functionValue);
    if (errorCode != JsNoError)
    {
        return errorCode;
    }

    JsValueRef globalObject = JS_INVALID_REFERENCE;
    errorCode = JsGetGlobalObject(&globalObject);
    if (errorCode != JsNoError)
    {
        return errorCode;
    }

    errorCode = SetPropertyByName(globalObject, "require", functionValue);
    if (errorCode != JsNoError)
    {
        return errorCode;
    }

    if (requireFunction != nullptr)
    {
        *requireFunction = functionValue;
    }

    return JsNoError;
}

JsErrorCode JsEnsureFfiIfMissing()
{
    JsValueRef globalObject = JS_INVALID_REFERENCE;
    JsErrorCode errorCode = JsGetGlobalObject(&globalObject);
    if (errorCode != JsNoError)
    {
        return errorCode;
    }

    JsValueRef existingFfi = JS_INVALID_REFERENCE;
    errorCode = GetPropertyByName(globalObject, "ffi", &existingFfi);
    if (errorCode != JsNoError)
    {
        return errorCode;
    }

    JsValueType ffiType = JsUndefined;
    errorCode = JsGetValueType(existingFfi, &ffiType);
    if (errorCode != JsNoError)
    {
        return errorCode;
    }

    if (ffiType != JsUndefined)
    {
        return JsNoError;
    }

    return JsInstallFfi(nullptr);
}

JsErrorCode JsEnsureHttpServerIfMissing()
{
    JsValueRef globalObject = JS_INVALID_REFERENCE;
    JsErrorCode errorCode = JsGetGlobalObject(&globalObject);
    if (errorCode != JsNoError)
    {
        return errorCode;
    }

    // Check if cHttp exists
    JsValueRef existingCHttp = JS_INVALID_REFERENCE;
    errorCode = GetPropertyByName(globalObject, "cHttp", &existingCHttp);
    if (errorCode != JsNoError)
    {
        return errorCode;
    }

    JsValueType cHttpType = JsUndefined;
    errorCode = JsGetValueType(existingCHttp, &cHttpType);
    if (errorCode != JsNoError)
    {
        return errorCode;
    }

    if (cHttpType == JsUndefined)
    {
        errorCode = JsInstallHttpServer(nullptr);
        if (errorCode != JsNoError)
        {
            return errorCode;
        }
    }

    // Check if cHttpK exists
    JsValueRef existingCHttpK = JS_INVALID_REFERENCE;
    errorCode = GetPropertyByName(globalObject, "cHttpK", &existingCHttpK);
    if (errorCode != JsNoError)
    {
        return errorCode;
    }

    JsValueType cHttpKType = JsUndefined;
    errorCode = JsGetValueType(existingCHttpK, &cHttpKType);
    if (errorCode != JsNoError)
    {
        return errorCode;
    }

    if (cHttpKType == JsUndefined)
    {
        errorCode = JsInstallHttpServerMulti(nullptr);
        if (errorCode != JsNoError)
        {
            return errorCode;
        }
    }

    return JsNoError;
}

// ── HTTP Server Support ───────────────────────────────────────────────────────

namespace
{
    std::string NormalizeHttpRouteKey(const std::string& method, const std::string& path)
    {
        std::string normalizedMethod = method;
        for (size_t index = 0; index < normalizedMethod.length(); ++index)
        {
            normalizedMethod[index] = static_cast<char>(std::toupper(static_cast<unsigned char>(normalizedMethod[index])));
        }

        const size_t querySeparator = path.find('?');
        const std::string normalizedPath = querySeparator == std::string::npos ? path : path.substr(0, querySeparator);
        return normalizedMethod + ":" + normalizedPath;
    }

    JsErrorCode StringifyJsonValue(JsValueRef value, std::string* output)
    {
        if (output == nullptr)
        {
            return JsErrorInvalidArgument;
        }

        JsValueRef globalObject = JS_INVALID_REFERENCE;
        JsErrorCode errorCode = JsGetGlobalObject(&globalObject);
        if (errorCode != JsNoError)
        {
            return errorCode;
        }

        JsValueRef jsonObject = JS_INVALID_REFERENCE;
        errorCode = GetPropertyByName(globalObject, "JSON", &jsonObject);
        if (errorCode != JsNoError)
        {
            return errorCode;
        }

        JsValueRef stringifyFunction = JS_INVALID_REFERENCE;
        errorCode = GetPropertyByName(jsonObject, "stringify", &stringifyFunction);
        if (errorCode != JsNoError)
        {
            return errorCode;
        }

        JsValueRef callArguments[2] = { jsonObject, value };
        JsValueRef stringifiedValue = JS_INVALID_REFERENCE;
        errorCode = JsCallFunction(stringifyFunction, callArguments, 2, &stringifiedValue);
        if (errorCode != JsNoError)
        {
            return errorCode;
        }

        return ConvertValueToUtf8String(stringifiedValue, output);
    }

    // Store routes and event handlers per server instance
    struct HttpServerState
    {
        uint64_t serverId;
        std::map<std::string, JsValueRef> routeHandlers; // "GET:/" -> handler function
        std::map<std::string, JsValueRef> eventHandlers; // "get", "post", etc.
    };

    std::map<uint64_t, HttpServerState> g_httpServers;
    uint64_t g_nextHttpServerId = 1;

    JsValueRef CHAKRA_CALLBACK HttpServerOnCallback(
        _In_ JsValueRef callee,
        _In_ bool isConstructCall,
        _In_ JsValueRef *arguments,
        _In_ unsigned short argumentCount,
        _In_opt_ void *callbackState);

    JsValueRef CHAKRA_CALLBACK HttpServerEndCallback(
        _In_ JsValueRef callee,
        _In_ bool isConstructCall,
        _In_ JsValueRef *arguments,
        _In_ unsigned short argumentCount,
        _In_opt_ void *callbackState);

    JsValueRef CHAKRA_CALLBACK HttpServerListenCallback(
        _In_ JsValueRef callee,
        _In_ bool isConstructCall,
        _In_ JsValueRef *arguments,
        _In_ unsigned short argumentCount,
        _In_opt_ void *callbackState);

    JsValueRef CHAKRA_CALLBACK HttpResponseSendCallback(
        _In_ JsValueRef callee,
        _In_ bool isConstructCall,
        _In_ JsValueRef *arguments,
        _In_ unsigned short argumentCount,
        _In_opt_ void *callbackState);

    JsValueRef CHAKRA_CALLBACK HttpResponseJsonCallback(
        _In_ JsValueRef callee,
        _In_ bool isConstructCall,
        _In_ JsValueRef *arguments,
        _In_ unsigned short argumentCount,
        _In_opt_ void *callbackState);

    JsValueRef CHAKRA_CALLBACK HttpResponseEndCallback(
        _In_ JsValueRef callee,
        _In_ bool isConstructCall,
        _In_ JsValueRef *arguments,
        _In_ unsigned short argumentCount,
        _In_opt_ void *callbackState);

    JsValueRef CHAKRA_CALLBACK HttpServerServeCallback(
        _In_ JsValueRef callee,
        _In_ bool isConstructCall,
        _In_ JsValueRef *arguments,
        _In_ unsigned short argumentCount,
        _In_opt_ void *callbackState)
{
    UNREFERENCED_PARAMETER(callee);
    UNREFERENCED_PARAMETER(isConstructCall);
    UNREFERENCED_PARAMETER(callbackState);

    if (argumentCount < 3)
    {
        return SetExceptionAndReturnInvalidReference("serve requires (port, host, [threads])");
    }

    double port = 0.0;
    if (JsNumberToDouble(arguments[1], &port) != JsNoError)
    {
        return SetExceptionAndReturnInvalidReference("serve: port must be a number");
    }

    std::string host;
    if (ConvertValueToUtf8String(arguments[2], &host) != JsNoError)
    {
        return SetExceptionAndReturnInvalidReference("serve: host must be a string");
    }

    uint32_t threadCount = 1;
    if (argumentCount > 3)
    {
        double threads = 0.0;
        if (JsNumberToDouble(arguments[3], &threads) == JsNoError && threads > 0)
        {
            threadCount = static_cast<uint32_t>(threads);
        }
    }

    std::string errorMessage;
    if (!EnsureHttpServerApiLoaded(&errorMessage))
    {
        return SetExceptionAndReturnInvalidReference(errorMessage.c_str());
    }

    ChakraHttpServerHandle serverHandle = (argumentCount > 3 && threadCount > 1)
        ? g_httpServerApi.serveMulti(
            static_cast<uint16_t>(port),
            reinterpret_cast<const uint8_t*>(host.c_str()),
            host.length(),
            threadCount)
        : g_httpServerApi.serveSingle(
            static_cast<uint16_t>(port),
            reinterpret_cast<const uint8_t*>(host.c_str()),
            host.length());

    if (chakra_http_server_handle_is_null(serverHandle))
    {
        return SetExceptionAndReturnInvalidReference("Failed to create HTTP server");
    }

    if (g_httpServerApi.start(serverHandle) == 0)
    {
        return SetExceptionAndReturnInvalidReference("Failed to start HTTP server");
    }

    JsValueRef serverObj = JS_INVALID_REFERENCE;
    JsCreateObject(&serverObj);

    // Store server handle
    JsValueRef handleValue = JS_INVALID_REFERENCE;
    JsDoubleToNumber(static_cast<double>(serverHandle.id), &handleValue);
    SetPropertyByName(serverObj, "_handle", handleValue);

    // Store route handlers map
    JsValueRef routesObj = JS_INVALID_REFERENCE;
    JsCreateObject(&routesObj);
    SetPropertyByName(serverObj, "_routes", routesObj);

    // Install on method
    JsValueRef onFunc = JS_INVALID_REFERENCE;
    JsCreateFunction(HttpServerOnCallback, serverObj, &onFunc);
    SetPropertyByName(serverObj, "on", onFunc);

    // Install end method
    JsValueRef endFunc = JS_INVALID_REFERENCE;
    JsCreateFunction(HttpServerEndCallback, serverObj, &endFunc);
    SetPropertyByName(serverObj, "end", endFunc);

    // Install listen method (blocking request loop)
    JsValueRef listenFunc = JS_INVALID_REFERENCE;
    JsCreateFunction(HttpServerListenCallback, serverObj, &listenFunc);
    SetPropertyByName(serverObj, "listen", listenFunc);

    return serverObj;
}

JsValueRef CHAKRA_CALLBACK HttpServerOnCallback(
    _In_ JsValueRef callee,
    _In_ bool isConstructCall,
    _In_ JsValueRef *arguments,
    _In_ unsigned short argumentCount,
    _In_opt_ void *callbackState)
{
    UNREFERENCED_PARAMETER(callee);
    UNREFERENCED_PARAMETER(isConstructCall);
    UNREFERENCED_PARAMETER(callbackState);

    if (argumentCount < 4)
    {
        return SetExceptionAndReturnInvalidReference("on requires (method, path, handler)");
    }

    std::string method;
    if (ConvertValueToUtf8String(arguments[1], &method) != JsNoError)
    {
        return SetExceptionAndReturnInvalidReference("on: method must be a string");
    }

    std::string path;
    if (ConvertValueToUtf8String(arguments[2], &path) != JsNoError)
    {
        return SetExceptionAndReturnInvalidReference("on: path must be a string");
    }

    std::string errorMessage;
    if (!EnsureHttpServerApiLoaded(&errorMessage))
    {
        return SetExceptionAndReturnInvalidReference(errorMessage.c_str());
    }

    JsValueType handlerType = JsUndefined;
    if (JsGetValueType(arguments[3], &handlerType) != JsNoError || handlerType != JsFunction)
    {
        return SetExceptionAndReturnInvalidReference("on: handler must be a function");
    }

    // Store route handler for later use
    std::string routeKey = NormalizeHttpRouteKey(method, path);

    JsValueRef handleValue = JS_INVALID_REFERENCE;
    if (GetPropertyByName(arguments[0], "_handle", &handleValue) != JsNoError)
    {
        return SetExceptionAndReturnInvalidReference("Invalid server object");
    }

    double handleNum = 0.0;
    if (JsNumberToDouble(handleValue, &handleNum) != JsNoError)
    {
        return SetExceptionAndReturnInvalidReference("Invalid server handle");
    }

    ChakraHttpServerHandle handle;
    handle.id = static_cast<uint64_t>(handleNum);

    if (g_httpServerApi.onRoute != nullptr &&
        g_httpServerApi.onRoute(
            handle,
            reinterpret_cast<const uint8_t*>(routeKey.c_str()),
            routeKey.length()) == 0)
    {
        return SetExceptionAndReturnInvalidReference("on: failed to register route");
    }
    
    std::map<uint64_t, HttpServerState>::iterator serverIterator = g_httpServers.find(handle.id);
    if (serverIterator == g_httpServers.end())
    {
        HttpServerState state;
        state.serverId = handle.id;
        g_httpServers[handle.id] = state;
        serverIterator = g_httpServers.find(handle.id);
    }

    std::map<std::string, JsValueRef>::iterator existingHandler = serverIterator->second.routeHandlers.find(routeKey);
    if (existingHandler != serverIterator->second.routeHandlers.end())
    {
        JsRelease(existingHandler->second, nullptr);
    }

    JsAddRef(arguments[3], nullptr);
    serverIterator->second.routeHandlers[routeKey] = arguments[3];
    
    JsValueRef undef = JS_INVALID_REFERENCE;
    JsGetUndefinedValue(&undef);
    return undef; // Return undefined on success
    }

    JsValueRef CHAKRA_CALLBACK HttpServerListenCallback(
        _In_ JsValueRef callee,
        _In_ bool isConstructCall,
        _In_ JsValueRef *arguments,
        _In_ unsigned short argumentCount,
        _In_opt_ void *callbackState)
    {
        UNREFERENCED_PARAMETER(callee);
        UNREFERENCED_PARAMETER(isConstructCall);
        UNREFERENCED_PARAMETER(callbackState);

        if (argumentCount < 1)
        {
            return SetExceptionAndReturnInvalidReference("listen requires server context");
        }

        JsValueRef serverObj = arguments[0];
        JsValueRef handleValue = JS_INVALID_REFERENCE;
        if (GetPropertyByName(serverObj, "_handle", &handleValue) != JsNoError)
        {
            return SetExceptionAndReturnInvalidReference("Invalid server object");
        }

        double handleNum = 0.0;
        if (JsNumberToDouble(handleValue, &handleNum) != JsNoError)
        {
            return SetExceptionAndReturnInvalidReference("Invalid server handle");
        }

        ChakraHttpServerHandle handle;
        handle.id = static_cast<uint64_t>(handleNum);

        std::string errorMessage;
        if (!EnsureHttpServerApiLoaded(&errorMessage))
        {
            return SetExceptionAndReturnInvalidReference(errorMessage.c_str());
        }

        while (true)
        {
            uint8_t* requestJson = g_httpServerApi.accept(handle);
            if (requestJson == nullptr)
            {
                break;
            }

            std::string requestJsonText(reinterpret_cast<const char*>(requestJson));
            g_httpServerApi.freeString(requestJson);

            JsValueRef requestObject = JS_INVALID_REFERENCE;
            if (ParseJsonText(requestJsonText, &requestObject) != JsNoError)
            {
                continue;
            }

            JsValueRef methodValue = JS_INVALID_REFERENCE;
            JsValueRef pathValue = JS_INVALID_REFERENCE;
            JsValueRef requestIdValue = JS_INVALID_REFERENCE;
            if (GetPropertyByName(requestObject, "method", &methodValue) != JsNoError ||
                GetPropertyByName(requestObject, "path", &pathValue) != JsNoError ||
                GetPropertyByName(requestObject, "id", &requestIdValue) != JsNoError)
            {
                continue;
            }

            std::string requestMethod;
            std::string requestPath;
            double requestIdNumber = 0.0;
            if (ConvertValueToUtf8String(methodValue, &requestMethod) != JsNoError ||
                ConvertValueToUtf8String(pathValue, &requestPath) != JsNoError ||
                JsNumberToDouble(requestIdValue, &requestIdNumber) != JsNoError)
            {
                continue;
            }

            const uint64_t requestId = static_cast<uint64_t>(requestIdNumber);
            const std::string routeKey = NormalizeHttpRouteKey(requestMethod, requestPath);

            std::map<uint64_t, HttpServerState>::iterator serverIterator = g_httpServers.find(handle.id);
            if (serverIterator == g_httpServers.end())
            {
                continue;
            }

            std::map<std::string, JsValueRef>::iterator handlerIterator = serverIterator->second.routeHandlers.find(routeKey);
            if (handlerIterator == serverIterator->second.routeHandlers.end())
            {
                const char* body = "Route handler not registered";
                const char* contentType = "text/plain";
                g_httpServerApi.respond(handle, requestId, 404,
                    reinterpret_cast<const uint8_t*>(contentType), strlen(contentType),
                    reinterpret_cast<const uint8_t*>(body), strlen(body));
                continue;
            }

            JsValueRef responseObject = JS_INVALID_REFERENCE;
            JsCreateObject(&responseObject);

            JsValueRef requestIdJs = JS_INVALID_REFERENCE;
            JsDoubleToNumber(static_cast<double>(requestId), &requestIdJs);
            SetPropertyByName(responseObject, "_reqId", requestIdJs);

            JsValueRef serverHandleJs = JS_INVALID_REFERENCE;
            JsDoubleToNumber(static_cast<double>(handle.id), &serverHandleJs);
            SetPropertyByName(responseObject, "_serverHandle", serverHandleJs);

            JsValueRef sentState = JS_INVALID_REFERENCE;
            JsDoubleToNumber(0.0, &sentState);
            SetPropertyByName(responseObject, "_sent", sentState);

            JsValueRef sendFunc = JS_INVALID_REFERENCE;
            JsCreateFunction(HttpResponseSendCallback, nullptr, &sendFunc);
            SetPropertyByName(responseObject, "send", sendFunc);

            JsValueRef jsonFunc = JS_INVALID_REFERENCE;
            JsCreateFunction(HttpResponseJsonCallback, nullptr, &jsonFunc);
            SetPropertyByName(responseObject, "json", jsonFunc);

            JsValueRef endFunc = JS_INVALID_REFERENCE;
            JsCreateFunction(HttpResponseEndCallback, nullptr, &endFunc);
            SetPropertyByName(responseObject, "end", endFunc);

            JsValueRef callArguments[3] = { serverObj, requestObject, responseObject };
            JsValueRef callbackResult = JS_INVALID_REFERENCE;
            JsCallFunction(handlerIterator->second, callArguments, 3, &callbackResult);

            JsValueRef sentValue = JS_INVALID_REFERENCE;
            double sentNumber = 0.0;
            if (GetPropertyByName(responseObject, "_sent", &sentValue) == JsNoError &&
                JsNumberToDouble(sentValue, &sentNumber) == JsNoError &&
                sentNumber < 0.5)
            {
                const char* contentType = "text/plain";
                g_httpServerApi.respond(handle, requestId, 200,
                    reinterpret_cast<const uint8_t*>(contentType), strlen(contentType),
                    nullptr, 0);
            }
        }

        JsValueRef undef = JS_INVALID_REFERENCE;
        JsGetUndefinedValue(&undef);
        return undef;
    }

    JsValueRef CHAKRA_CALLBACK HttpResponseSendCallback(
        _In_ JsValueRef callee,
        _In_ bool isConstructCall,
        _In_ JsValueRef *arguments,
        _In_ unsigned short argumentCount,
        _In_opt_ void *callbackState)
    {
        UNREFERENCED_PARAMETER(callee);
        UNREFERENCED_PARAMETER(isConstructCall);
        UNREFERENCED_PARAMETER(callbackState);

        if (argumentCount < 1)
        {
            return SetExceptionAndReturnInvalidReference("res.send requires response context");
        }

        JsValueRef responseObject = arguments[0];
        JsValueRef requestIdValue = JS_INVALID_REFERENCE;
        JsValueRef handleValue = JS_INVALID_REFERENCE;
        if (GetPropertyByName(responseObject, "_reqId", &requestIdValue) != JsNoError ||
            GetPropertyByName(responseObject, "_serverHandle", &handleValue) != JsNoError)
        {
            return SetExceptionAndReturnInvalidReference("Invalid response context");
        }

        double requestIdNumber = 0.0;
        double handleNumber = 0.0;
        if (JsNumberToDouble(requestIdValue, &requestIdNumber) != JsNoError ||
            JsNumberToDouble(handleValue, &handleNumber) != JsNoError)
        {
            return SetExceptionAndReturnInvalidReference("Invalid response identifiers");
        }

        std::string body;
        if (argumentCount > 1)
        {
            ConvertValueToUtf8String(arguments[1], &body);
        }

        const char* contentType = "text/plain";
        ChakraHttpServerHandle handle;
        handle.id = static_cast<uint64_t>(handleNumber);
        g_httpServerApi.respond(handle, static_cast<uint64_t>(requestIdNumber), 200,
            reinterpret_cast<const uint8_t*>(contentType), strlen(contentType),
            reinterpret_cast<const uint8_t*>(body.c_str()), body.length());

        JsValueRef sentState = JS_INVALID_REFERENCE;
        JsDoubleToNumber(1.0, &sentState);
        SetPropertyByName(responseObject, "_sent", sentState);

        JsValueRef undef = JS_INVALID_REFERENCE;
        JsGetUndefinedValue(&undef);
        return undef;
    }

    JsValueRef CHAKRA_CALLBACK HttpResponseJsonCallback(
        _In_ JsValueRef callee,
        _In_ bool isConstructCall,
        _In_ JsValueRef *arguments,
        _In_ unsigned short argumentCount,
        _In_opt_ void *callbackState)
    {
        UNREFERENCED_PARAMETER(callee);
        UNREFERENCED_PARAMETER(isConstructCall);
        UNREFERENCED_PARAMETER(callbackState);

        if (argumentCount < 1)
        {
            return SetExceptionAndReturnInvalidReference("res.json requires response context");
        }

        JsValueRef responseObject = arguments[0];
        JsValueRef requestIdValue = JS_INVALID_REFERENCE;
        JsValueRef handleValue = JS_INVALID_REFERENCE;
        if (GetPropertyByName(responseObject, "_reqId", &requestIdValue) != JsNoError ||
            GetPropertyByName(responseObject, "_serverHandle", &handleValue) != JsNoError)
        {
            return SetExceptionAndReturnInvalidReference("Invalid response context");
        }

        double requestIdNumber = 0.0;
        double handleNumber = 0.0;
        if (JsNumberToDouble(requestIdValue, &requestIdNumber) != JsNoError ||
            JsNumberToDouble(handleValue, &handleNumber) != JsNoError)
        {
            return SetExceptionAndReturnInvalidReference("Invalid response identifiers");
        }

        std::string body = "null";
        if (argumentCount > 1)
        {
            if (StringifyJsonValue(arguments[1], &body) != JsNoError)
            {
                return SetExceptionAndReturnInvalidReference("res.json: failed to stringify payload");
            }
        }

        const char* contentType = "application/json";
        ChakraHttpServerHandle handle;
        handle.id = static_cast<uint64_t>(handleNumber);
        g_httpServerApi.respond(handle, static_cast<uint64_t>(requestIdNumber), 200,
            reinterpret_cast<const uint8_t*>(contentType), strlen(contentType),
            reinterpret_cast<const uint8_t*>(body.c_str()), body.length());

        JsValueRef sentState = JS_INVALID_REFERENCE;
        JsDoubleToNumber(1.0, &sentState);
        SetPropertyByName(responseObject, "_sent", sentState);

        JsValueRef undef = JS_INVALID_REFERENCE;
        JsGetUndefinedValue(&undef);
        return undef;
    }

    JsValueRef CHAKRA_CALLBACK HttpResponseEndCallback(
        _In_ JsValueRef callee,
        _In_ bool isConstructCall,
        _In_ JsValueRef *arguments,
        _In_ unsigned short argumentCount,
        _In_opt_ void *callbackState)
    {
        UNREFERENCED_PARAMETER(callee);
        UNREFERENCED_PARAMETER(isConstructCall);
        UNREFERENCED_PARAMETER(callbackState);

        if (argumentCount < 1)
        {
            return SetExceptionAndReturnInvalidReference("res.end requires response context");
        }

        JsValueRef responseObject = arguments[0];
        JsValueRef requestIdValue = JS_INVALID_REFERENCE;
        JsValueRef handleValue = JS_INVALID_REFERENCE;
        if (GetPropertyByName(responseObject, "_reqId", &requestIdValue) != JsNoError ||
            GetPropertyByName(responseObject, "_serverHandle", &handleValue) != JsNoError)
        {
            return SetExceptionAndReturnInvalidReference("Invalid response context");
        }

        double requestIdNumber = 0.0;
        double handleNumber = 0.0;
        if (JsNumberToDouble(requestIdValue, &requestIdNumber) != JsNoError ||
            JsNumberToDouble(handleValue, &handleNumber) != JsNoError)
        {
            return SetExceptionAndReturnInvalidReference("Invalid response identifiers");
        }

        ChakraHttpServerHandle handle;
        handle.id = static_cast<uint64_t>(handleNumber);
        const char* contentType = "text/plain";
        g_httpServerApi.respond(handle, static_cast<uint64_t>(requestIdNumber), 200,
            reinterpret_cast<const uint8_t*>(contentType), strlen(contentType),
            nullptr, 0);

        JsValueRef sentState = JS_INVALID_REFERENCE;
        JsDoubleToNumber(1.0, &sentState);
        SetPropertyByName(responseObject, "_sent", sentState);

        JsValueRef undef = JS_INVALID_REFERENCE;
        JsGetUndefinedValue(&undef);
        return undef;
    }

    JsValueRef CHAKRA_CALLBACK HttpServerEndCallback(
        _In_ JsValueRef callee,
        _In_ bool isConstructCall,
        _In_ JsValueRef *arguments,
        _In_ unsigned short argumentCount,
        _In_opt_ void *callbackState)
{
    UNREFERENCED_PARAMETER(callee);
    UNREFERENCED_PARAMETER(isConstructCall);
    UNREFERENCED_PARAMETER(callbackState);

    if (argumentCount < 1)
    {
        return SetExceptionAndReturnInvalidReference("end requires context");
    }

    JsValueRef serverObj = arguments[0];
    JsValueRef handleValue = JS_INVALID_REFERENCE;
    if (GetPropertyByName(serverObj, "_handle", &handleValue) != JsNoError)
    {
        return SetExceptionAndReturnInvalidReference("Invalid server object");
    }

    double handleNum = 0.0;
    if (JsNumberToDouble(handleValue, &handleNum) != JsNoError)
    {
        return SetExceptionAndReturnInvalidReference("Invalid server handle");
    }

    ChakraHttpServerHandle handle;
    handle.id = static_cast<uint64_t>(handleNum);

    std::string errorMessage;
    if (!EnsureHttpServerApiLoaded(&errorMessage))
    {
        return SetExceptionAndReturnInvalidReference(errorMessage.c_str());
    }

    int result = g_httpServerApi.stop(handle);

    std::map<uint64_t, HttpServerState>::iterator serverIterator = g_httpServers.find(handle.id);
    if (serverIterator != g_httpServers.end())
    {
        for (std::map<std::string, JsValueRef>::iterator routeIterator = serverIterator->second.routeHandlers.begin();
            routeIterator != serverIterator->second.routeHandlers.end();
            ++routeIterator)
        {
            JsRelease(routeIterator->second, nullptr);
        }

        g_httpServers.erase(serverIterator);
    }
    
    JsValueRef resultValue = JS_INVALID_REFERENCE;
    JsDoubleToNumber(static_cast<double>(result), &resultValue);
    return resultValue;
    }
}

CHAKRA_API JsRequireChakraSystemPackage(_In_ JsValueRef moduleName, _Out_ JsValueRef* packageObject)
{
    if (packageObject == nullptr)
    {
        return JsErrorInvalidArgument;
    }

    *packageObject = JS_INVALID_REFERENCE;

    std::string moduleNameText;
    JsErrorCode errorCode = ConvertValueToUtf8String(moduleName, &moduleNameText);
    if (errorCode != JsNoError)
    {
        return errorCode;
    }

    errorCode = CreateSystemPackageObject(moduleNameText, packageObject);
    if (errorCode != JsNoError)
    {
        SetExceptionFromUtf8(kUnknownPackageMessage);
    }

    return errorCode;
}

CHAKRA_API JsInstallFfi(_Out_opt_ JsValueRef* ffiObject)
{
    JsValueRef ffiObj = JS_INVALID_REFERENCE;
    JsErrorCode errorCode = JsCreateObject(&ffiObj);
    if (errorCode != JsNoError)
    {
        return errorCode;
    }

    // Install ffi.dlopen
    JsValueRef dlopenFunc = JS_INVALID_REFERENCE;
    errorCode = JsCreateFunction(FfiDlopenCallback, nullptr, &dlopenFunc);
    if (errorCode != JsNoError)
    {
        return errorCode;
    }
    errorCode = SetPropertyByName(ffiObj, "dlopen", dlopenFunc);
    if (errorCode != JsNoError)
    {
        return errorCode;
    }

    // Install ffi.dlsym
    JsValueRef dlsymFunc = JS_INVALID_REFERENCE;
    errorCode = JsCreateFunction(FfiDlsymCallback, nullptr, &dlsymFunc);
    if (errorCode != JsNoError)
    {
        return errorCode;
    }
    errorCode = SetPropertyByName(ffiObj, "dlsym", dlsymFunc);
    if (errorCode != JsNoError)
    {
        return errorCode;
    }

    // Install ffi.call
    JsValueRef callFunc = JS_INVALID_REFERENCE;
    errorCode = JsCreateFunction(FfiCallCallback, nullptr, &callFunc);
    if (errorCode != JsNoError)
    {
        return errorCode;
    }
    errorCode = SetPropertyByName(ffiObj, "call", callFunc);
    if (errorCode != JsNoError)
    {
        return errorCode;
    }

    // Install ffi.func
    JsValueRef funcFunc = JS_INVALID_REFERENCE;
    errorCode = JsCreateFunction(FfiFuncCallback, nullptr, &funcFunc);
    if (errorCode != JsNoError)
    {
        return errorCode;
    }
    errorCode = SetPropertyByName(ffiObj, "func", funcFunc);
    if (errorCode != JsNoError)
    {
        return errorCode;
    }

    // Install ffi.type
    JsValueRef typeFunc = JS_INVALID_REFERENCE;
    errorCode = JsCreateFunction(FfiTypeCallback, nullptr, &typeFunc);
    if (errorCode != JsNoError)
    {
        return errorCode;
    }
    errorCode = SetPropertyByName(ffiObj, "type", typeFunc);
    if (errorCode != JsNoError)
    {
        return errorCode;
    }

    // Install ffi.struct
    JsValueRef structFunc = JS_INVALID_REFERENCE;
    errorCode = JsCreateFunction(FfiStructCallback, nullptr, &structFunc);
    if (errorCode != JsNoError)
    {
        return errorCode;
    }
    errorCode = SetPropertyByName(ffiObj, "struct", structFunc);
    if (errorCode != JsNoError)
    {
        return errorCode;
    }

    // Install ffi.union
    JsValueRef unionFunc = JS_INVALID_REFERENCE;
    errorCode = JsCreateFunction(FfiUnionCallback, nullptr, &unionFunc);
    if (errorCode != JsNoError)
    {
        return errorCode;
    }
    errorCode = SetPropertyByName(ffiObj, "union", unionFunc);
    if (errorCode != JsNoError)
    {
        return errorCode;
    }

    // Install ffi.ptr
    JsValueRef ptrFunc = JS_INVALID_REFERENCE;
    errorCode = JsCreateFunction(FfiPtrCallback, nullptr, &ptrFunc);
    if (errorCode != JsNoError)
    {
        return errorCode;
    }
    errorCode = SetPropertyByName(ffiObj, "ptr", ptrFunc);
    if (errorCode != JsNoError)
    {
        return errorCode;
    }

    // Install ffi.types
    errorCode = InstallFfiTypesObject(ffiObj);
    if (errorCode != JsNoError)
    {
        return errorCode;
    }

    // Install ffi.close
    JsValueRef closeFunc = JS_INVALID_REFERENCE;
    errorCode = JsCreateFunction(FfiCloseCallback, nullptr, &closeFunc);
    if (errorCode != JsNoError)
    {
        return errorCode;
    }
    errorCode = SetPropertyByName(ffiObj, "close", closeFunc);
    if (errorCode != JsNoError)
    {
        return errorCode;
    }

    // Install ffi object on global scope
    JsValueRef globalObject = JS_INVALID_REFERENCE;
    errorCode = JsGetGlobalObject(&globalObject);
    if (errorCode != JsNoError)
    {
        return errorCode;
    }

    errorCode = SetPropertyByName(globalObject, "ffi", ffiObj);
    if (errorCode != JsNoError)
    {
        return errorCode;
    }

    if (ffiObject != nullptr)
    {
        *ffiObject = ffiObj;
    }

    return JsNoError;
}

CHAKRA_API JsInstallHttpServer(_Out_opt_ JsValueRef* serverObject)
{
    JsValueRef serverObj = JS_INVALID_REFERENCE;
    JsErrorCode errorCode = JsCreateObject(&serverObj);
    if (errorCode != JsNoError)
    {
        return errorCode;
    }

    // Install serve method
    JsValueRef serveFunc = JS_INVALID_REFERENCE;
    errorCode = JsCreateFunction(HttpServerServeCallback, nullptr, &serveFunc);
    if (errorCode != JsNoError)
    {
        return errorCode;
    }
    errorCode = SetPropertyByName(serverObj, "serve", serveFunc);
    if (errorCode != JsNoError)
    {
        return errorCode;
    }

    // Install cHttp on global scope
    JsValueRef globalObject = JS_INVALID_REFERENCE;
    errorCode = JsGetGlobalObject(&globalObject);
    if (errorCode != JsNoError)
    {
        return errorCode;
    }

    errorCode = SetPropertyByName(globalObject, "cHttp", serverObj);
    if (errorCode != JsNoError)
    {
        return errorCode;
    }

    if (serverObject != nullptr)
    {
        *serverObject = serverObj;
    }

    return JsNoError;
}

CHAKRA_API JsInstallHttpServerMulti(_Out_opt_ JsValueRef* serverObject)
{
    JsValueRef serverObj = JS_INVALID_REFERENCE;
    JsErrorCode errorCode = JsCreateObject(&serverObj);
    if (errorCode != JsNoError)
    {
        return errorCode;
    }

    // Install serve method (multi-threaded version)
    JsValueRef serveFunc = JS_INVALID_REFERENCE;
    errorCode = JsCreateFunction(HttpServerServeCallback, nullptr, &serveFunc);
    if (errorCode != JsNoError)
    {
        return errorCode;
    }
    errorCode = SetPropertyByName(serverObj, "serve", serveFunc);
    if (errorCode != JsNoError)
    {
        return errorCode;
    }

    // Install cHttpK on global scope
    JsValueRef globalObject = JS_INVALID_REFERENCE;
    errorCode = JsGetGlobalObject(&globalObject);
    if (errorCode != JsNoError)
    {
        return errorCode;
    }

    errorCode = SetPropertyByName(globalObject, "cHttpK", serverObj);
    if (errorCode != JsNoError)
    {
        return errorCode;
    }

    if (serverObject != nullptr)
    {
        *serverObject = serverObj;
    }

    return JsNoError;
}

CHAKRA_API JsChakraEs2020Analyze(_In_ JsValueRef source, _Out_ JsValueRef* analysisResult)
{
    if (analysisResult == nullptr)
    {
        return JsErrorInvalidArgument;
    }

    *analysisResult = JS_INVALID_REFERENCE;

    std::string sourceText;
    JsErrorCode errorCode = ConvertValueToUtf8String(source, &sourceText);
    if (errorCode != JsNoError)
    {
        return errorCode;
    }

    std::string analysisJson;
    std::string rustError;
    if (!TryEs2020Analyze(sourceText, &analysisJson, &rustError))
    {
        SetExceptionFromUtf8(rustError.empty() ? "chakra:es2020.analyze failed." : rustError);
        return JsErrorFatal;
    }

    return ParseJsonText(analysisJson, analysisResult);
}

CHAKRA_API JsChakraEs2021Analyze(_In_ JsValueRef source, _Out_ JsValueRef* analysisResult)
{
    if (analysisResult == nullptr)
    {
        return JsErrorInvalidArgument;
    }

    *analysisResult = JS_INVALID_REFERENCE;

    std::string sourceText;
    JsErrorCode errorCode = ConvertValueToUtf8String(source, &sourceText);
    if (errorCode != JsNoError)
    {
        return errorCode;
    }

    std::string analysisJson;
    std::string rustError;
    if (!TryEs2021Analyze(sourceText, &analysisJson, &rustError))
    {
        SetExceptionFromUtf8(rustError.empty() ? "chakra:es2021.analyze failed." : rustError);
        return JsErrorFatal;
    }

    return ParseJsonText(analysisJson, analysisResult);
}

CHAKRA_API JsChakraEs2021Transform(_In_ JsValueRef source, _Out_ JsValueRef* transformedSource)
{
    if (transformedSource == nullptr)
    {
        return JsErrorInvalidArgument;
    }

    *transformedSource = JS_INVALID_REFERENCE;

    std::string sourceText;
    JsErrorCode errorCode = ConvertValueToUtf8String(source, &sourceText);
    if (errorCode != JsNoError)
    {
        return errorCode;
    }

    std::string transformedText;
    std::string rustError;
    if (!TryEs2021Transform(sourceText, &transformedText, &rustError))
    {
        SetExceptionFromUtf8(rustError.empty() ? "chakra:es2021.transform failed." : rustError);
        return JsErrorFatal;
    }

    return CreateUtf8StringValue(transformedText, transformedSource);
}
