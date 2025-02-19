#pragma once

#include "envoy/common/platform.h"
#include "envoy/http/codec.h"

#include "common/common/assert.h"
#include "common/http/header_map_impl.h"
#include "common/network/address_impl.h"
#include "common/network/listen_socket_impl.h"
#include "common/quic/quic_io_handle_wrapper.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "quiche/quic/core/quic_types.h"

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "quiche/quic/core/http/quic_header_list.h"
#include "quiche/quic/core/quic_error_codes.h"
#include "quiche/quic/platform/api/quic_ip_address.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "common/http/header_utility.h"

#include "openssl/ssl.h"

namespace Envoy {
namespace Quic {

// TODO(danzh): this is called on each write. Consider to return an address instance on the stack if
// the heap allocation is too expensive.
Network::Address::InstanceConstSharedPtr
quicAddressToEnvoyAddressInstance(const quic::QuicSocketAddress& quic_address);

quic::QuicSocketAddress envoyIpAddressToQuicSocketAddress(const Network::Address::Ip* envoy_ip);

class HeaderValidator {
public:
  virtual ~HeaderValidator() = default;
  virtual Http::HeaderUtility::HeaderValidationResult
  validateHeader(const std::string& header_name, absl::string_view header_value) = 0;
};

// The returned header map has all keys in lower case.
template <class T>
std::unique_ptr<T> quicHeadersToEnvoyHeaders(const quic::QuicHeaderList& header_list,
                                             HeaderValidator& validator) {
  auto headers = T::create();
  for (const auto& entry : header_list) {
    Http::HeaderUtility::HeaderValidationResult result =
        validator.validateHeader(entry.first, entry.second);
    switch (result) {
    case Http::HeaderUtility::HeaderValidationResult::REJECT:
      return nullptr;
    case Http::HeaderUtility::HeaderValidationResult::DROP:
      continue;
    case Http::HeaderUtility::HeaderValidationResult::ACCEPT:
      auto key = Http::LowerCaseString(entry.first);
      if (key != Http::Headers::get().Cookie) {
        // TODO(danzh): Avoid copy by referencing entry as header_list is already validated by QUIC.
        headers->addCopy(key, entry.second);
      } else {
        // QUICHE breaks "cookie" header into crumbs. Coalesce them by appending current one to
        // existing one if there is any.
        headers->appendCopy(key, entry.second);
      }
    }
  }
  return headers;
}

template <class T>
std::unique_ptr<T> spdyHeaderBlockToEnvoyHeaders(const spdy::SpdyHeaderBlock& header_block) {
  auto headers = T::create();
  for (auto entry : header_block) {
    // TODO(danzh): Avoid temporary strings and addCopy() with string_view.
    std::string key(entry.first);
    // QUICHE coalesces multiple trailer values with the same key with '\0'.
    std::vector<absl::string_view> values = absl::StrSplit(entry.second, '\0');
    for (const absl::string_view& value : values) {
      headers->addCopy(Http::LowerCaseString(key), value);
    }
  }
  return headers;
}

spdy::SpdyHeaderBlock envoyHeadersToSpdyHeaderBlock(const Http::HeaderMap& headers);

// Called when Envoy wants to reset the underlying QUIC stream.
quic::QuicRstStreamErrorCode envoyResetReasonToQuicRstError(Http::StreamResetReason reason);

// Called when a RST_STREAM frame is received.
Http::StreamResetReason quicRstErrorToEnvoyLocalResetReason(quic::QuicRstStreamErrorCode rst_err);

// Called when a QUIC stack reset the stream.
Http::StreamResetReason quicRstErrorToEnvoyRemoteResetReason(quic::QuicRstStreamErrorCode rst_err);

// Called when underlying QUIC connection is closed locally.
Http::StreamResetReason quicErrorCodeToEnvoyLocalResetReason(quic::QuicErrorCode error);

// Called when underlying QUIC connection is closed by peer.
Http::StreamResetReason quicErrorCodeToEnvoyRemoteResetReason(quic::QuicErrorCode error);

// Called when a GOAWAY frame is received.
ABSL_MUST_USE_RESULT
Http::GoAwayErrorCode quicErrorCodeToEnvoyErrorCode(quic::QuicErrorCode error) noexcept;

// Create a connection socket instance and apply given socket options to the
// socket. IP_PKTINFO and SO_RXQ_OVFL is always set if supported.
Network::ConnectionSocketPtr
createConnectionSocket(Network::Address::InstanceConstSharedPtr& peer_addr,
                       Network::Address::InstanceConstSharedPtr& local_addr,
                       const Network::ConnectionSocket::OptionsSharedPtr& options);

// Convert a cert in string form to X509 object.
// Return nullptr if the bytes passed cannot be passed.
bssl::UniquePtr<X509> parseDERCertificate(const std::string& der_bytes, std::string* error_details);

// Deduce the suitable signature algorithm according to the public key.
// Return the sign algorithm id works with the public key; If the public key is
// not supported, return 0 with error_details populated correspondingly.
int deduceSignatureAlgorithmFromPublicKey(const EVP_PKEY* public_key, std::string* error_details);

// Return a connection socket which read and write via io_handle, but doesn't close it when the
// socket gets closed nor set options on the socket.
Network::ConnectionSocketPtr
createServerConnectionSocket(Network::IoHandle& io_handle,
                             const quic::QuicSocketAddress& self_address,
                             const quic::QuicSocketAddress& peer_address,
                             const std::string& hostname, absl::string_view alpn);

} // namespace Quic
} // namespace Envoy
