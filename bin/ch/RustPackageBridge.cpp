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
#else
    typedef const char* (*ChakraInfoVersionFn)();
#endif

    struct RustPackageApi
    {
        bool isInitialized;
        RustLibraryHandle library;
        ChakraInfoVersionFn infoVersion;
    };

    RustPackageApi g_rustPackageApi = { false, nullptr, nullptr };

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
                std::string("../../../../bin/ch/rust/chakra_packages/target/release/") +
                kRustPackageLibraryName;
            candidates.push_back(JoinPath(executableDirectory, repoRelativeLibraryPath));
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
