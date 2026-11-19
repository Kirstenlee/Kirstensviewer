include_guard()

include(CURL)
include(OpenSSL)
include(XmlRpcEpi)
include(ZLIB)
include(Boost)

set(LLMESSAGE_INCLUDE_DIRS
    ${LIBS_OPEN_DIR}/llmessage
    ${CURL_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIRS}
    ${BOOST_INCLUDE_DIRS}
    )

set(LLMESSAGE_LIBRARIES llmessage)
