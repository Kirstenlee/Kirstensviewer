/**
 * @file httpcommon.cpp
 * @brief
 *
 * $LicenseInfo:firstyear=2012&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2012-2014, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "linden_common.h"		// Modifies curl/curl.h interfaces
#include "httpcommon.h"
#include "llmutex.h"
#include "llthread.h"
#include <curl/curl.h>
#include <string>
#include <sstream>

namespace LLCore
{
	HttpStatus::type_enum_t EXT_CURL_EASY;
	HttpStatus::type_enum_t EXT_CURL_MULTI;
	HttpStatus::type_enum_t LLCORE;

	HttpStatus::operator U32() const
	{
		static constexpr int shift = sizeof(mDetails->mStatus) * 8;
		return (static_cast<U32>(mDetails->mType) << shift) | static_cast<U32>(mDetails->mStatus);
	}

	std::string HttpStatus::toHex() const
	{
		std::ostringstream result;
		result.width(8);
		result.fill('0');
		result << std::hex << operator U32();
		return result.str();
	}

	std::string HttpStatus::toString() const
	{
		static constexpr const char* llcore_errors[] =
		{
			"",
			"HTTP error reply status",
			"Services shutting down",
			"Operation canceled",
			"Invalid Content-Range header encountered",
			"Request handle not found",
			"Invalid datatype for argument or option",
			"Option has not been explicitly set",
			"Option is not dynamic and must be set early",
			"Invalid HTTP status code received from server",
			"Could not allocate required resource"
		};
		static constexpr int llcore_errors_count = static_cast<int>(std::size(llcore_errors));

		static constexpr struct HttpErrorEntry
		{
			type_enum_t mCode;
			const char* mText;
		}
		http_errors[] =
		{
			{ 100, "Continue" },
			{ 101, "Switching Protocols" },
			{ 200, "OK" },
			{ 201, "Created" },
			{ 202, "Accepted" },
			{ 203, "Non-Authoritative Information" },
			{ 204, "No Content" },
			{ 205, "Reset Content" },
			{ 206, "Partial Content" },
			{ 300, "Multiple Choices" },
			{ 301, "Moved Permanently" },
			{ 302, "Found" },
			{ 303, "See Other" },
			{ 304, "Not Modified" },
			{ 305, "Use Proxy" },
			{ 307, "Temporary Redirect" },
			{ 400, "Bad Request" },
			{ 401, "Unauthorized" },
			{ 402, "Payment Required" },
			{ 403, "Forbidden" },
			{ 404, "Not Found" },
			{ 405, "Method Not Allowed" },
			{ 406, "Not Acceptable" },
			{ 407, "Proxy Authentication Required" },
			{ 408, "Request Time-out" },
			{ 409, "Conflict" },
			{ 410, "Gone" },
			{ 411, "Length Required" },
			{ 412, "Precondition Failed" },
			{ 413, "Request Entity Too Large" },
			{ 414, "Request-URI Too Large" },
			{ 415, "Unsupported Media Type" },
			{ 416, "Requested range not satisfiable" },
			{ 417, "Expectation Failed" },
			{ 499, "Linden Catch-All" },
			{ 500, "Internal Server Error" },
			{ 501, "Not Implemented" },
			{ 502, "Bad Gateway" },
			{ 503, "Service Unavailable" },
			{ 504, "Gateway Time-out" },
			{ 505, "HTTP Version not supported" }
		};
		static constexpr int http_errors_count = static_cast<int>(std::size(http_errors));

		if (*this)
		{
			return {};
		}

		const auto type = getType();
		const auto status = getStatus();

		switch (type)
		{
		case EXT_CURL_EASY:
			return curl_easy_strerror(static_cast<CURLcode>(status));

		case EXT_CURL_MULTI:
			return curl_multi_strerror(static_cast<CURLMcode>(status));

		case LLCORE:
			if (status >= 0 && status < llcore_errors_count)
			{
				return llcore_errors[status];
			}
			break;

		default:
			if (isHttpStatus())
			{
				if ((type == 499) && (!getMessage().empty()))
					return getMessage();

				int low = 0;
				int high = http_errors_count - 1;
				while (low <= high)
				{
					const int mid = low + ((high - low) >> 1);
					const auto code = http_errors[mid].mCode;
					if (type == code)
					{
						return http_errors[mid].mText;
					}
					if (type < code)
					{
						high = mid - 1;
					}
					else
					{
						low = mid + 1;
					}
				}
			}
			break;
		}
		return "Unknown error";
	}

	std::string HttpStatus::toTerseString() const
	{
		std::ostringstream result;
		const auto type = getType();
		unsigned int error_value = static_cast<unsigned short>(getStatus());

		switch (type)
		{
		case EXT_CURL_EASY:
			result << "Easy_";
			break;

		case EXT_CURL_MULTI:
			result << "Multi_";
			break;

		case LLCORE:
			result << "Core_";
			break;

		default:
			if (isHttpStatus())
			{
				result << "Http_";
				error_value = type;
			}
			else
			{
				result << "Unknown_";
			}
			break;
		}

		result << error_value;
		return result.str();
	}

	bool HttpStatus::isRetryable() const
	{
		if (isHttpStatus())
		{
			const auto type = getType();
			if (type >= 499 && type <= 599)
				return true;
		}

		const auto type = getType();
		const auto status = getStatus();

		if (type == EXT_CURL_EASY)
		{
			switch (static_cast<CURLcode>(status))
			{
			case CURLE_COULDNT_CONNECT:
			case CURLE_COULDNT_RESOLVE_PROXY:
			case CURLE_COULDNT_RESOLVE_HOST:
			case CURLE_SEND_ERROR:
			case CURLE_RECV_ERROR:
			case CURLE_UPLOAD_FAILED:
			case CURLE_OPERATION_TIMEDOUT:
			case CURLE_HTTP_POST_ERROR:
			case CURLE_PARTIAL_FILE:
				return true;
			default:
				break;
			}
		}
		else if (type == LLCORE)
		{
			if (status == HE_INV_CONTENT_RANGE_HDR || status == HE_INVALID_HTTP_STATUS)
				return true;
		}

		return false;
	}

	namespace LLHttp
	{
		namespace
		{
			CURL* getCurlTemplateHandle()
			{
				static CURL* curlpTemplateHandle = nullptr;

				if (curlpTemplateHandle == nullptr)
				{
					curlpTemplateHandle = curl_easy_init();
					if (curlpTemplateHandle == nullptr)
					{
						LL_WARNS() << "curl error calling curl_easy_init()" << LL_ENDL;
					}
					else
					{
						const std::pair<CURLoption, long> options[] =
						{
							{ CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4 },
							{ CURLOPT_NOSIGNAL, 1L },
							{ CURLOPT_NOPROGRESS, 1L },
							{ CURLOPT_AUTOREFERER, 1L },
							{ CURLOPT_FOLLOWLOCATION, 1L },
							{ CURLOPT_SSL_VERIFYPEER, 1L },
							{ CURLOPT_SSL_VERIFYHOST, 0L },
							{ CURLOPT_DNS_CACHE_TIMEOUT, 15L }
						};

						for (const auto& [opt, val] : options)
						{
							check_curl_code(curl_easy_setopt(curlpTemplateHandle, opt, val), opt);
						}

						check_curl_code(curl_easy_setopt(curlpTemplateHandle, CURLOPT_ENCODING, ""), CURLOPT_ENCODING);
					}
				}

				return curlpTemplateHandle;
			}

			LLMutex* getCurlMutex()
			{
				static LLMutex* sHandleMutexp = nullptr;

				if (!sHandleMutexp)
				{
					sHandleMutexp = new LLMutex();
				}

				return sHandleMutexp;
			}

			void deallocateEasyCurl(CURL* curlp)
			{
				LLMutexLock lock(getCurlMutex());
				curl_easy_cleanup(curlp);
			}
		}

		void initialize()
		{
			CURLcode code = curl_global_init(CURL_GLOBAL_ALL);
			check_curl_code(code, CURL_GLOBAL_ALL);
		}

		void cleanup()
		{
			curl_global_cleanup();
		}

		CURL_ptr createEasyHandle()
		{
			LLMutexLock lock(getCurlMutex());
			CURL* handle = curl_easy_duphandle(getCurlTemplateHandle());
			return CURL_ptr(handle, &deallocateEasyCurl);
		}

		std::string getCURLVersion()
		{
			return curl_version();
		}

		void check_curl_code(CURLcode code, int curl_setopt_option)
		{
			if (CURLE_OK != code) [[unlikely]]
			{
				LL_WARNS() << "libcurl error detected:  " << curl_easy_strerror(code)
					<< ", curl_easy_setopt option:  " << curl_setopt_option
					<< LL_ENDL;
			}
		}
	}
} // end namespace LLCore