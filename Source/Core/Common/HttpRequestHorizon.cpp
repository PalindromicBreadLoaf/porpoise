// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Common/HttpRequest.h"

// Stub out all required curl functions, leading to everything taking the same path as a
// failure on desktop.
namespace Common
{
class HttpRequest::Impl final
{
};

HttpRequest::HttpRequest(std::chrono::milliseconds timeout_ms, ProgressCallback callback)
{
}

HttpRequest::~HttpRequest() = default;

bool HttpRequest::IsValid() const
{
  return false;
}

void HttpRequest::SetCookies(const std::string& cookies)
{
}

void HttpRequest::UseIPv4()
{
}

void HttpRequest::FollowRedirects(long max)
{
}

s32 HttpRequest::GetLastResponseCode() const
{
  return 0;
}

std::string HttpRequest::EscapeComponent(const std::string& string)
{
  return {};
}

std::string HttpRequest::GetHeaderValue(std::string_view name) const
{
  return {};
}

HttpRequest::Response HttpRequest::Get(const std::string& url, const Headers& headers,
                                       AllowedReturnCodes codes)
{
  return {};
}

HttpRequest::Response HttpRequest::Post(const std::string& url, std::span<const u8> payload,
                                        const Headers& headers, AllowedReturnCodes codes)
{
  return {};
}

HttpRequest::Response HttpRequest::Post(const std::string& url, std::string_view payload,
                                        const Headers& headers, AllowedReturnCodes codes)
{
  return {};
}

HttpRequest::Response HttpRequest::PostMultiform(const std::string& url,
                                                 std::span<Multiform> multiform,
                                                 const Headers& headers, AllowedReturnCodes codes)
{
  return {};
}
}  // namespace Common
