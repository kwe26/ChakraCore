//-------------------------------------------------------------------------------------------------------
// Copyright (C) ChakraCore Project Contributors. All rights reserved.
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------
#include "JsrtPch.h"
#include "ChakraCore.h"

#ifdef __valid
#undef __valid
#endif

#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
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

    const char* const kUnknownPackageMessage =
        "Unknown system package. Available modules: chakra:info, chakra:fs, chakra:reqwest, chakra:es2020, chakra:es2021.";

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

        JsValueRef packageObject = JS_INVALID_REFERENCE;
        const JsErrorCode createError = CreateSystemPackageObject(moduleName, &packageObject);
        if (createError != JsNoError)
        {
            return SetExceptionAndReturnInvalidReference(kUnknownPackageMessage);
        }

        return packageObject;
    }
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
