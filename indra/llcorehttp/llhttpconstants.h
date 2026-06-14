/** 
 * @file llhttpconstants.h
 * @brief Constants for HTTP requests and responses
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2001-2014, Linden Research, Inc.
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

#ifndef LL_HTTP_CONSTANTS_H
#define LL_HTTP_CONSTANTS_H

#include "stdtypes.h"
#include <string_view>  // Required for std::string_view

// =============================================================================
// HTTP STATUS CODES
// =============================================================================
// Using constexpr for compile-time evaluation - zero runtime overhead
// inline allows definition in header without ODR violations
// =============================================================================

// Standard errors from HTTP spec:
// http://www.w3.org/Protocols/rfc2616/rfc2616-sec6.html#sec6.1

// Informational 1xx
inline constexpr S32 HTTP_CONTINUE = 100;
inline constexpr S32 HTTP_SWITCHING_PROTOCOLS = 101;

// Success 2xx
inline constexpr S32 HTTP_OK = 200;
inline constexpr S32 HTTP_CREATED = 201;
inline constexpr S32 HTTP_ACCEPTED = 202;
inline constexpr S32 HTTP_NON_AUTHORITATIVE_INFORMATION = 203;
inline constexpr S32 HTTP_NO_CONTENT = 204;
inline constexpr S32 HTTP_RESET_CONTENT = 205;
inline constexpr S32 HTTP_PARTIAL_CONTENT = 206;

// Redirection 3xx
inline constexpr S32 HTTP_MULTIPLE_CHOICES = 300;
inline constexpr S32 HTTP_MOVED_PERMANENTLY = 301;
inline constexpr S32 HTTP_FOUND = 302;
inline constexpr S32 HTTP_SEE_OTHER = 303;
inline constexpr S32 HTTP_NOT_MODIFIED = 304;
inline constexpr S32 HTTP_USE_PROXY = 305;
inline constexpr S32 HTTP_TEMPORARY_REDIRECT = 307;

// Client Error 4xx
inline constexpr S32 HTTP_BAD_REQUEST = 400;
inline constexpr S32 HTTP_UNAUTHORIZED = 401;
inline constexpr S32 HTTP_PAYMENT_REQUIRED = 402;
inline constexpr S32 HTTP_FORBIDDEN = 403;
inline constexpr S32 HTTP_NOT_FOUND = 404;
inline constexpr S32 HTTP_METHOD_NOT_ALLOWED = 405;
inline constexpr S32 HTTP_NOT_ACCEPTABLE = 406;
inline constexpr S32 HTTP_PROXY_AUTHENTICATION_REQUIRED = 407;
inline constexpr S32 HTTP_REQUEST_TIME_OUT = 408;
inline constexpr S32 HTTP_CONFLICT = 409;
inline constexpr S32 HTTP_GONE = 410;
inline constexpr S32 HTTP_LENGTH_REQUIRED = 411;
inline constexpr S32 HTTP_PRECONDITION_FAILED = 412;
inline constexpr S32 HTTP_REQUEST_ENTITY_TOO_LARGE = 413;
inline constexpr S32 HTTP_REQUEST_URI_TOO_LARGE = 414;
inline constexpr S32 HTTP_UNSUPPORTED_MEDIA_TYPE = 415;
inline constexpr S32 HTTP_REQUESTED_RANGE_NOT_SATISFIABLE = 416;
inline constexpr S32 HTTP_EXPECTATION_FAILED = 417;

// Server Error 5xx
inline constexpr S32 HTTP_INTERNAL_SERVER_ERROR = 500;
inline constexpr S32 HTTP_NOT_IMPLEMENTED = 501;
inline constexpr S32 HTTP_BAD_GATEWAY = 502;
inline constexpr S32 HTTP_SERVICE_UNAVAILABLE = 503;
inline constexpr S32 HTTP_GATEWAY_TIME_OUT = 504;
inline constexpr S32 HTTP_VERSION_NOT_SUPPORTED = 505;

// Internal process errors (not sent over wire)
inline constexpr S32 HTTP_INTERNAL_CURL_ERROR = 498;
inline constexpr S32 HTTP_INTERNAL_ERROR = 499;


// =============================================================================
// HTTP METHODS/VERBS
// =============================================================================
// inline constexpr string_view: defined in header, no .cpp definition needed
// Benefits: zero heap allocation, compile-time constants, cache-friendly
// =============================================================================

inline constexpr std::string_view HTTP_VERB_INVALID{"(invalid)"};
inline constexpr std::string_view HTTP_VERB_HEAD{"HEAD"};
inline constexpr std::string_view HTTP_VERB_GET{"GET"};
inline constexpr std::string_view HTTP_VERB_PUT{"PUT"};
inline constexpr std::string_view HTTP_VERB_POST{"POST"};
inline constexpr std::string_view HTTP_VERB_DELETE{"DELETE"};
inline constexpr std::string_view HTTP_VERB_MOVE{"MOVE"};        // Caller will need to set 'Destination' header
inline constexpr std::string_view HTTP_VERB_OPTIONS{"OPTIONS"};
//inline constexpr std::string_view HTTP_VERB_PATCH{"PATCH"};
//inline constexpr std::string_view HTTP_VERB_COPY{"COPY"};

// HTTP method enumeration for type-safe method handling
enum EHTTPMethod
{
    HTTP_INVALID = 0,
    HTTP_HEAD,
    HTTP_GET,
    HTTP_PUT,
    HTTP_POST,
    HTTP_DELETE,
    HTTP_MOVE,
    HTTP_OPTIONS,
    HTTP_PATCH,
    HTTP_COPY,
    HTTP_METHOD_COUNT
};

// Parses 'Retry-After' header contents and returns seconds until retry should occur.
bool getSecondsUntilRetryAfter(const std::string& retry_after, F32& seconds_to_wait);


// =============================================================================
// OUTGOING HTTP HEADERS
// =============================================================================
// Do *not* use these to check incoming headers.
// For incoming headers, use the lower-case headers below.
// =============================================================================

inline constexpr std::string_view HTTP_OUT_HEADER_ACCEPT{"Accept"};
inline constexpr std::string_view HTTP_OUT_HEADER_ACCEPT_CHARSET{"Accept-Charset"};
inline constexpr std::string_view HTTP_OUT_HEADER_ACCEPT_ENCODING{"Accept-Encoding"};
inline constexpr std::string_view HTTP_OUT_HEADER_ACCEPT_LANGUAGE{"Accept-Language"};
inline constexpr std::string_view HTTP_OUT_HEADER_ACCEPT_RANGES{"Accept-Ranges"};
inline constexpr std::string_view HTTP_OUT_HEADER_AGE{"Age"};
inline constexpr std::string_view HTTP_OUT_HEADER_ALLOW{"Allow"};
inline constexpr std::string_view HTTP_OUT_HEADER_AUTHORIZATION{"Authorization"};
inline constexpr std::string_view HTTP_OUT_HEADER_CACHE_CONTROL{"Cache-Control"};
inline constexpr std::string_view HTTP_OUT_HEADER_CONNECTION{"Connection"};
inline constexpr std::string_view HTTP_OUT_HEADER_CONTENT_DESCRIPTION{"Content-Description"};
inline constexpr std::string_view HTTP_OUT_HEADER_CONTENT_ENCODING{"Content-Encoding"};
inline constexpr std::string_view HTTP_OUT_HEADER_CONTENT_ID{"Content-ID"};
inline constexpr std::string_view HTTP_OUT_HEADER_CONTENT_LANGUAGE{"Content-Language"};
inline constexpr std::string_view HTTP_OUT_HEADER_CONTENT_LENGTH{"Content-Length"};
inline constexpr std::string_view HTTP_OUT_HEADER_CONTENT_LOCATION{"Content-Location"};
inline constexpr std::string_view HTTP_OUT_HEADER_CONTENT_MD5{"Content-MD5"};
inline constexpr std::string_view HTTP_OUT_HEADER_CONTENT_RANGE{"Content-Range"};
inline constexpr std::string_view HTTP_OUT_HEADER_CONTENT_TRANSFER_ENCODING{"Content-Transfer-Encoding"};
inline constexpr std::string_view HTTP_OUT_HEADER_CONTENT_TYPE{"Content-Type"};
inline constexpr std::string_view HTTP_OUT_HEADER_COOKIE{"Cookie"};
inline constexpr std::string_view HTTP_OUT_HEADER_DATE{"Date"};
inline constexpr std::string_view HTTP_OUT_HEADER_DESTINATION{"Destination"};
inline constexpr std::string_view HTTP_OUT_HEADER_ETAG{"ETag"};
inline constexpr std::string_view HTTP_OUT_HEADER_EXPECT{"Expect"};
inline constexpr std::string_view HTTP_OUT_HEADER_EXPIRES{"Expires"};
inline constexpr std::string_view HTTP_OUT_HEADER_FROM{"From"};
inline constexpr std::string_view HTTP_OUT_HEADER_HOST{"Host"};
inline constexpr std::string_view HTTP_OUT_HEADER_IF_MATCH{"If-Match"};
inline constexpr std::string_view HTTP_OUT_HEADER_IF_MODIFIED_SINCE{"If-Modified-Since"};
inline constexpr std::string_view HTTP_OUT_HEADER_IF_NONE_MATCH{"If-None-Match"};
inline constexpr std::string_view HTTP_OUT_HEADER_IF_RANGE{"If-Range"};
inline constexpr std::string_view HTTP_OUT_HEADER_IF_UNMODIFIED_SINCE{"If-Unmodified-Since"};
inline constexpr std::string_view HTTP_OUT_HEADER_KEEP_ALIVE{"Keep-Alive"};
inline constexpr std::string_view HTTP_OUT_HEADER_LAST_MODIFIED{"Last-Modified"};
inline constexpr std::string_view HTTP_OUT_HEADER_LOCATION{"Location"};
inline constexpr std::string_view HTTP_OUT_HEADER_MAX_FORWARDS{"Max-Forwards"};
inline constexpr std::string_view HTTP_OUT_HEADER_MIME_VERSION{"MIME-Version"};
inline constexpr std::string_view HTTP_OUT_HEADER_PRAGMA{"Pragma"};
inline constexpr std::string_view HTTP_OUT_HEADER_PROXY_AUTHENTICATE{"Proxy-Authenticate"};
inline constexpr std::string_view HTTP_OUT_HEADER_PROXY_AUTHORIZATION{"Proxy-Authorization"};
inline constexpr std::string_view HTTP_OUT_HEADER_RANGE{"Range"};
inline constexpr std::string_view HTTP_OUT_HEADER_REFERER{"Referer"};
inline constexpr std::string_view HTTP_OUT_HEADER_RETRY_AFTER{"Retry-After"};
inline constexpr std::string_view HTTP_OUT_HEADER_SERVER{"Server"};
inline constexpr std::string_view HTTP_OUT_HEADER_SET_COOKIE{"Set-Cookie"};
inline constexpr std::string_view HTTP_OUT_HEADER_TE{"TE"};
inline constexpr std::string_view HTTP_OUT_HEADER_TRAILER{"Trailer"};
inline constexpr std::string_view HTTP_OUT_HEADER_TRANSFER_ENCODING{"Transfer-Encoding"};
inline constexpr std::string_view HTTP_OUT_HEADER_UPGRADE{"Upgrade"};
inline constexpr std::string_view HTTP_OUT_HEADER_USER_AGENT{"User-Agent"};
inline constexpr std::string_view HTTP_OUT_HEADER_VARY{"Vary"};
inline constexpr std::string_view HTTP_OUT_HEADER_VIA{"Via"};
inline constexpr std::string_view HTTP_OUT_HEADER_WARNING{"Warning"};
inline constexpr std::string_view HTTP_OUT_HEADER_WWW_AUTHENTICATE{"WWW-Authenticate"};


// =============================================================================
// INCOMING HTTP HEADERS (normalized to lower-case)
// =============================================================================

inline constexpr std::string_view HTTP_IN_HEADER_ACCEPT_LANGUAGE{"accept-language"};
inline constexpr std::string_view HTTP_IN_HEADER_CACHE_CONTROL{"cache-control"};
inline constexpr std::string_view HTTP_IN_HEADER_CONTENT_LENGTH{"content-length"};
inline constexpr std::string_view HTTP_IN_HEADER_CONTENT_LOCATION{"content-location"};
inline constexpr std::string_view HTTP_IN_HEADER_CONTENT_TYPE{"content-type"};
inline constexpr std::string_view HTTP_IN_HEADER_HOST{"host"};
inline constexpr std::string_view HTTP_IN_HEADER_LOCATION{"location"};
inline constexpr std::string_view HTTP_IN_HEADER_RETRY_AFTER{"retry-after"};
inline constexpr std::string_view HTTP_IN_HEADER_SET_COOKIE{"set-cookie"};
inline constexpr std::string_view HTTP_IN_HEADER_USER_AGENT{"user-agent"};
inline constexpr std::string_view HTTP_IN_HEADER_X_CONTENT_TYPE_OPTIONS{"x-content-type-options"};
inline constexpr std::string_view HTTP_IN_HEADER_X_FORWARDED_FOR{"x-forwarded-for"};


// =============================================================================
// HTTP CONTENT TYPES (MIME types)
// =============================================================================

inline constexpr std::string_view HTTP_CONTENT_LLSD_XML{"application/llsd+xml"};
inline constexpr std::string_view HTTP_CONTENT_OCTET_STREAM{"application/octet-stream"};
inline constexpr std::string_view HTTP_CONTENT_VND_LL_MESH{"application/vnd.ll.mesh"};
inline constexpr std::string_view HTTP_CONTENT_XML{"application/xml"};
inline constexpr std::string_view HTTP_CONTENT_JSON{"application/json"};
inline constexpr std::string_view HTTP_CONTENT_TEXT_HTML{"text/html"};
inline constexpr std::string_view HTTP_CONTENT_TEXT_HTML_UTF8{"text/html; charset=utf-8"};
inline constexpr std::string_view HTTP_CONTENT_TEXT_PLAIN_UTF8{"text/plain; charset=utf-8"};
inline constexpr std::string_view HTTP_CONTENT_TEXT_LLSD{"text/llsd"};
inline constexpr std::string_view HTTP_CONTENT_TEXT_XML{"text/xml"};
inline constexpr std::string_view HTTP_CONTENT_TEXT_LSL{"text/lsl"};
inline constexpr std::string_view HTTP_CONTENT_TEXT_PLAIN{"text/plain"};
inline constexpr std::string_view HTTP_CONTENT_IMAGE_X_J2C{"image/x-j2c"};
inline constexpr std::string_view HTTP_CONTENT_IMAGE_J2C{"image/j2c"};
inline constexpr std::string_view HTTP_CONTENT_IMAGE_JPEG{"image/jpeg"};
inline constexpr std::string_view HTTP_CONTENT_IMAGE_PNG{"image/png"};
inline constexpr std::string_view HTTP_CONTENT_IMAGE_BMP{"image/bmp"};


// =============================================================================
// HTTP CACHE CONTROL DIRECTIVES
// =============================================================================

inline constexpr std::string_view HTTP_NO_CACHE{"no-cache"};
inline constexpr std::string_view HTTP_NO_CACHE_CONTROL{"no-cache, max-age=0"};
inline constexpr std::string_view HTTP_NOSNIFF{"nosniff"};


#endif // LL_HTTP_CONSTANTS_H
