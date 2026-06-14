include_guard()

include(Curl)
include(OpenSSL)
include(XmlRpcEpi)
include(ZLib)
include(Boost)

set(LLMESSAGE_INCLUDE_DIRS
    ${LIBS_OPEN_DIR}/llmessage
    ${CURL_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIRS}
    ${BOOST_INCLUDE_DIRS}
    )

set(LLMESSAGE_LIBRARIES llmessage)
