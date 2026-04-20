//-------------------------------------------------------------------------------------------------------
// Copyright (C) ChakraCore Project Contributors. All rights reserved.
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------
#pragma once

#include <string>

namespace RustPackageBridge
{
    bool TryGetInfoVersion(std::string* version);
    bool TryFsReadFileUtf8(const std::string& path, std::string* contents, std::string* errorMessage = nullptr);
    bool TryFsWriteFileUtf8(const std::string& path, const std::string& content, std::string* errorMessage = nullptr);
    bool TryFsExists(const std::string& path, bool* exists, std::string* errorMessage = nullptr);
    bool TryReqwestGetText(const std::string& url, std::string* responseText, std::string* errorMessage = nullptr);
    bool TryReqwestPostText(const std::string& url, const std::string& body, std::string* responseText, std::string* errorMessage = nullptr);
    bool TryReqwestFetchText(const std::string& method, const std::string& url, const std::string* body, std::string* responseText, std::string* errorMessage = nullptr);
    bool TryReqwestDownloadFetchParallel(const std::string& url, const std::string& outputPath, int parallelPartCount, std::string* errorMessage = nullptr);
}
