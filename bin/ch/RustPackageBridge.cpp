//-------------------------------------------------------------------------------------------------------
// Copyright (C) ChakraCore Project Contributors. All rights reserved.
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------
#include "stdafx.h"
#include "RustPackageBridge.h"

#include <cstdlib>
#include <vector>

#ifndef _WIN32
#include <dlfcn.h>
#endif

namespace
{
#ifdef _WIN32
    typedef HMODULE RustLibraryHandle;
#else
    typedef void* RustLibraryHandle;
#endif

#ifdef _WIN32
    const char* const kRustPackageLibraryName = "chakra_packages.dll";
#elif defined(__APPLE__)
    const char* const kRustPackageLibraryName = "libchakra_packages.dylib";
#else
    const char* const kRustPackageLibraryName = "libchakra_packages.so";
#endif

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
#endif

    struct RustPackageApi
    {
        bool isInitialized;
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

    std::string JoinPath(const std::string& left, const std::string& right)
    {
        if (left.empty())
        {
            return right;
        }

        const char lastChar = left[left.length() - 1];
        if (lastChar == '/' || lastChar == '\\')
        {
            return left + right;
        }

        return left + "/" + right;
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

        std::string binaryPath(binaryLocation);
        const size_t separatorIndex = binaryPath.find_last_of("/\\");
        if (separatorIndex == std::string::npos)
        {
            return false;
        }

        *directory = binaryPath.substr(0, separatorIndex);
        return true;
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
        errno_t result = _dupenv_s(&envValue, &envValueLength, variableName);
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
        const char* envValue = std::getenv(variableName);
        if (envValue == nullptr || envValue[0] == '\0')
        {
            return false;
        }

        *value = envValue;
        return true;
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

        std::string executableDirectory;
        if (GetExecutableDirectory(&executableDirectory))
        {
            candidates.push_back(JoinPath(executableDirectory, kRustPackageLibraryName));

            const std::string repoRelativeLibraryPath =
                std::string("../../../../rust/chakra_packages/target/release/") +
                kRustPackageLibraryName;
            candidates.push_back(JoinPath(executableDirectory, repoRelativeLibraryPath));

            const std::string legacyRepoRelativeLibraryPath =
                std::string("../../../../bin/ch/rust/chakra_packages/target/release/") +
                kRustPackageLibraryName;
            candidates.push_back(JoinPath(executableDirectory, legacyRepoRelativeLibraryPath));
        }

        candidates.push_back(kRustPackageLibraryName);
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
        if (library == nullptr)
        {
            return nullptr;
        }

#ifdef _WIN32
        return reinterpret_cast<void*>(::GetProcAddress(library, symbolName));
#else
        return ::dlsym(library, symbolName);
#endif
    }

    void InitializeRustPackageApi()
    {
        if (g_rustPackageApi.isInitialized)
        {
            return;
        }

        g_rustPackageApi.isInitialized = true;

        const std::vector<std::string> candidates = BuildLibraryCandidates();
        for (const std::string& candidate : candidates)
        {
            g_rustPackageApi.library = TryLoadLibrary(candidate);
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

    bool SetErrorMessage(std::string* errorMessage, const char* fallbackMessage)
    {
        if (errorMessage == nullptr)
        {
            return false;
        }

        if (g_rustPackageApi.lastError != nullptr)
        {
            const char* lastErrorMessage = g_rustPackageApi.lastError();
            if (lastErrorMessage != nullptr && lastErrorMessage[0] != '\0')
            {
                *errorMessage = lastErrorMessage;
                return true;
            }
        }

        *errorMessage = fallbackMessage;
        return true;
    }

    bool CopyOwnedRustString(char* value, std::string* output)
    {
        if (value == nullptr || output == nullptr)
        {
            return false;
        }

        output->assign(value);

        if (g_rustPackageApi.stringFree != nullptr)
        {
            g_rustPackageApi.stringFree(value);
        }

        return true;
    }
}

bool RustPackageBridge::TryGetInfoVersion(std::string* version)
{
    if (version == nullptr)
    {
        return false;
    }

    InitializeRustPackageApi();

    if (g_rustPackageApi.infoVersion == nullptr)
    {
        return false;
    }

    const char* versionText = g_rustPackageApi.infoVersion();
    if (versionText == nullptr || versionText[0] == '\0')
    {
        return false;
    }

    *version = versionText;
    return true;
}

bool RustPackageBridge::TryFsReadFileUtf8(const std::string& path, std::string* contents, std::string* errorMessage)
{
    if (contents == nullptr)
    {
        return false;
    }

    InitializeRustPackageApi();

    if (g_rustPackageApi.fsReadFileUtf8 == nullptr)
    {
        SetErrorMessage(errorMessage, "Rust symbol chakra_fs_read_file_utf8 is unavailable.");
        return false;
    }

    if (g_rustPackageApi.stringFree == nullptr)
    {
        SetErrorMessage(errorMessage, "Rust symbol chakra_string_free is unavailable.");
        return false;
    }

    char* rawContents = g_rustPackageApi.fsReadFileUtf8(path.c_str());
    if (rawContents == nullptr)
    {
        SetErrorMessage(errorMessage, "Failed to read file through chakra:fs.");
        return false;
    }

    return CopyOwnedRustString(rawContents, contents);
}

bool RustPackageBridge::TryFsWriteFileUtf8(const std::string& path, const std::string& content, std::string* errorMessage)
{
    InitializeRustPackageApi();

    if (g_rustPackageApi.fsWriteFileUtf8 == nullptr)
    {
        SetErrorMessage(errorMessage, "Rust symbol chakra_fs_write_file_utf8 is unavailable.");
        return false;
    }

    const int result = g_rustPackageApi.fsWriteFileUtf8(path.c_str(), content.c_str());
    if (result == 1)
    {
        return true;
    }

    SetErrorMessage(errorMessage, "Failed to write file through chakra:fs.");
    return false;
}

bool RustPackageBridge::TryFsExists(const std::string& path, bool* exists, std::string* errorMessage)
{
    if (exists == nullptr)
    {
        return false;
    }

    InitializeRustPackageApi();

    if (g_rustPackageApi.fsExists == nullptr)
    {
        SetErrorMessage(errorMessage, "Rust symbol chakra_fs_exists is unavailable.");
        return false;
    }

    const int result = g_rustPackageApi.fsExists(path.c_str());
    if (result == 1)
    {
        *exists = true;
        return true;
    }

    if (result == 0)
    {
        *exists = false;
        return true;
    }

    SetErrorMessage(errorMessage, "Failed to test file existence through chakra:fs.");
    return false;
}

bool RustPackageBridge::TryReqwestGetText(const std::string& url, std::string* responseText, std::string* errorMessage)
{
    if (responseText == nullptr)
    {
        return false;
    }

    InitializeRustPackageApi();

    if (g_rustPackageApi.reqwestGetText == nullptr)
    {
        SetErrorMessage(errorMessage, "Rust symbol chakra_reqwest_get_text is unavailable.");
        return false;
    }

    if (g_rustPackageApi.stringFree == nullptr)
    {
        SetErrorMessage(errorMessage, "Rust symbol chakra_string_free is unavailable.");
        return false;
    }

    char* rawResponse = g_rustPackageApi.reqwestGetText(url.c_str());
    if (rawResponse == nullptr)
    {
        SetErrorMessage(errorMessage, "HTTP request failed through chakra:reqwest.");
        return false;
    }

    return CopyOwnedRustString(rawResponse, responseText);
}

bool RustPackageBridge::TryReqwestPostText(const std::string& url, const std::string& body, std::string* responseText, std::string* errorMessage)
{
    if (responseText == nullptr)
    {
        return false;
    }

    InitializeRustPackageApi();

    if (g_rustPackageApi.reqwestPostText == nullptr)
    {
        SetErrorMessage(errorMessage, "Rust symbol chakra_reqwest_post_text is unavailable.");
        return false;
    }

    if (g_rustPackageApi.stringFree == nullptr)
    {
        SetErrorMessage(errorMessage, "Rust symbol chakra_string_free is unavailable.");
        return false;
    }

    char* rawResponse = g_rustPackageApi.reqwestPostText(url.c_str(), body.c_str());
    if (rawResponse == nullptr)
    {
        SetErrorMessage(errorMessage, "HTTP POST request failed through chakra:reqwest.");
        return false;
    }

    return CopyOwnedRustString(rawResponse, responseText);
}

bool RustPackageBridge::TryReqwestFetchText(const std::string& method, const std::string& url, const std::string* body, std::string* responseText, std::string* errorMessage)
{
    if (responseText == nullptr)
    {
        return false;
    }

    InitializeRustPackageApi();

    if (g_rustPackageApi.reqwestFetchText == nullptr)
    {
        SetErrorMessage(errorMessage, "Rust symbol chakra_reqwest_fetch_text is unavailable.");
        return false;
    }

    if (g_rustPackageApi.stringFree == nullptr)
    {
        SetErrorMessage(errorMessage, "Rust symbol chakra_string_free is unavailable.");
        return false;
    }

    const char* bodyPointer = body == nullptr ? nullptr : body->c_str();
    char* rawResponse = g_rustPackageApi.reqwestFetchText(method.c_str(), url.c_str(), bodyPointer);
    if (rawResponse == nullptr)
    {
        SetErrorMessage(errorMessage, "HTTP fetch request failed through chakra:reqwest.");
        return false;
    }

    return CopyOwnedRustString(rawResponse, responseText);
}

bool RustPackageBridge::TryReqwestDownloadFetchParallel(const std::string& url, const std::string& outputPath, int parallelPartCount, std::string* errorMessage)
{
    InitializeRustPackageApi();

    if (g_rustPackageApi.reqwestDownloadFetchParallel == nullptr)
    {
        SetErrorMessage(errorMessage, "Rust symbol chakra_reqwest_download_fetch_parallel is unavailable.");
        return false;
    }

    const int result = g_rustPackageApi.reqwestDownloadFetchParallel(url.c_str(), outputPath.c_str(), parallelPartCount);
    if (result == 1)
    {
        return true;
    }

    SetErrorMessage(errorMessage, "HTTP download request failed through chakra:reqwest.downloadFetch.");
    return false;
}

bool RustPackageBridge::TryEs2020Analyze(const std::string& source, std::string* analysisJson, std::string* errorMessage)
{
    if (analysisJson == nullptr)
    {
        return false;
    }

    InitializeRustPackageApi();

    if (g_rustPackageApi.es2020Analyze == nullptr)
    {
        SetErrorMessage(errorMessage, "Rust symbol chakra_es2020_analyze is unavailable.");
        return false;
    }

    if (g_rustPackageApi.stringFree == nullptr)
    {
        SetErrorMessage(errorMessage, "Rust symbol chakra_string_free is unavailable.");
        return false;
    }

    char* rawAnalysis = g_rustPackageApi.es2020Analyze(source.c_str());
    if (rawAnalysis == nullptr)
    {
        SetErrorMessage(errorMessage, "Failed to analyze source through chakra:es2020.");
        return false;
    }

    return CopyOwnedRustString(rawAnalysis, analysisJson);
}

bool RustPackageBridge::TryEs2021Analyze(const std::string& source, std::string* analysisJson, std::string* errorMessage)
{
    if (analysisJson == nullptr)
    {
        return false;
    }

    InitializeRustPackageApi();

    if (g_rustPackageApi.es2021Analyze == nullptr)
    {
        SetErrorMessage(errorMessage, "Rust symbol chakra_es2021_analyze is unavailable.");
        return false;
    }

    if (g_rustPackageApi.stringFree == nullptr)
    {
        SetErrorMessage(errorMessage, "Rust symbol chakra_string_free is unavailable.");
        return false;
    }

    char* rawAnalysis = g_rustPackageApi.es2021Analyze(source.c_str());
    if (rawAnalysis == nullptr)
    {
        SetErrorMessage(errorMessage, "Failed to analyze source through chakra:es2021.");
        return false;
    }

    return CopyOwnedRustString(rawAnalysis, analysisJson);
}

bool RustPackageBridge::TryEs2021Transform(const std::string& source, std::string* transformedSource, std::string* errorMessage)
{
    if (transformedSource == nullptr)
    {
        return false;
    }

    InitializeRustPackageApi();

    if (g_rustPackageApi.es2021Transform == nullptr)
    {
        SetErrorMessage(errorMessage, "Rust symbol chakra_es2021_transform is unavailable.");
        return false;
    }

    if (g_rustPackageApi.stringFree == nullptr)
    {
        SetErrorMessage(errorMessage, "Rust symbol chakra_string_free is unavailable.");
        return false;
    }

    char* rawTransformedSource = g_rustPackageApi.es2021Transform(source.c_str());
    if (rawTransformedSource == nullptr)
    {
        SetErrorMessage(errorMessage, "Failed to transform source through chakra:es2021.");
        return false;
    }

    return CopyOwnedRustString(rawTransformedSource, transformedSource);
}
