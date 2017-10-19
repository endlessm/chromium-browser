/* Copyright (C) 1995-1998 Eric Young (eay@cryptsoft.com)
 * All rights reserved.
 *
 * This package is an SSL implementation written
 * by Eric Young (eay@cryptsoft.com).
 * The implementation was written so as to conform with Netscapes SSL.
 *
 * This library is free for commercial and non-commercial use as long as
 * the following conditions are aheared to.  The following conditions
 * apply to all code found in this distribution, be it the RC4, RSA,
 * lhash, DES, etc., code; not just the SSL code.  The SSL documentation
 * included with this distribution is covered by the same copyright terms
 * except that the holder is Tim Hudson (tjh@cryptsoft.com).
 *
 * Copyright remains Eric Young's, and as such any Copyright notices in
 * the code are not to be removed.
 * If this package is used in a product, Eric Young should be given attribution
 * as the author of the parts of the library used.
 * This can be in the form of a textual message at program startup or
 * in documentation (online or textual) provided with the package.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    "This product includes cryptographic software written by
 *     Eric Young (eay@cryptsoft.com)"
 *    The word 'cryptographic' can be left out if the rouines from the library
 *    being used are not cryptographic related :-).
 * 4. If you include any Windows specific code (or a derivative thereof) from
 *    the apps directory (application code) you must include an acknowledgement:
 *    "This product includes software written by Tim Hudson (tjh@cryptsoft.com)"
 *
 * THIS SOFTWARE IS PROVIDED BY ERIC YOUNG ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * The licence and distribution terms for any publically available version or
 * derivative of this code cannot be changed.  i.e. this code cannot simply be
 * copied and put under another distribution licence
 * [including the GNU Public Licence.]
 */
/* ====================================================================
 * Copyright (c) 1998-2002 The OpenSSL Project.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. All advertising materials mentioning features or use of this
 *    software must display the following acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit. (http://www.openssl.org/)"
 *
 * 4. The names "OpenSSL Toolkit" and "OpenSSL Project" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For written permission, please contact
 *    openssl-core@openssl.org.
 *
 * 5. Products derived from this software may not be called "OpenSSL"
 *    nor may "OpenSSL" appear in their names without prior written
 *    permission of the OpenSSL Project.
 *
 * 6. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit (http://www.openssl.org/)"
 *
 * THIS SOFTWARE IS PROVIDED BY THE OpenSSL PROJECT ``AS IS'' AND ANY
 * EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE OpenSSL PROJECT OR
 * ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 * ====================================================================
 *
 * This product includes cryptographic software written by Eric Young
 * (eay@cryptsoft.com).  This product includes software written by Tim
 * Hudson (tjh@cryptsoft.com). */
/* ====================================================================
 * Copyright 2002 Sun Microsystems, Inc. ALL RIGHTS RESERVED.
 * ECC cipher suite support in OpenSSL originally developed by
 * SUN MICROSYSTEMS, INC., and contributed to the OpenSSL project. */

#include <openssl/ssl.h>

#include <assert.h>
#include <limits.h>
#include <string.h>

#include <openssl/buf.h>
#include <openssl/bytestring.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/mem.h>
#include <openssl/md5.h>
#include <openssl/nid.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include "../crypto/internal.h"
#include "internal.h"


namespace bssl {

SSL_HANDSHAKE::SSL_HANDSHAKE(SSL *ssl_arg)
    : ssl(ssl_arg),
      scts_requested(0),
      needs_psk_binder(0),
      received_hello_retry_request(0),
      received_custom_extension(0),
      accept_psk_mode(0),
      cert_request(0),
      certificate_status_expected(0),
      ocsp_stapling_requested(0),
      should_ack_sni(0),
      in_false_start(0),
      in_early_data(0),
      early_data_offered(0),
      can_early_read(0),
      can_early_write(0),
      next_proto_neg_seen(0),
      ticket_expected(0),
      extended_master_secret(0),
      pending_private_key_op(0) {
}

SSL_HANDSHAKE::~SSL_HANDSHAKE() {
  OPENSSL_cleanse(secret, sizeof(secret));
  OPENSSL_cleanse(early_traffic_secret, sizeof(early_traffic_secret));
  OPENSSL_cleanse(client_handshake_secret, sizeof(client_handshake_secret));
  OPENSSL_cleanse(server_handshake_secret, sizeof(server_handshake_secret));
  OPENSSL_cleanse(client_traffic_secret_0, sizeof(client_traffic_secret_0));
  OPENSSL_cleanse(server_traffic_secret_0, sizeof(server_traffic_secret_0));
  OPENSSL_free(cookie);
  OPENSSL_free(key_share_bytes);
  OPENSSL_free(ecdh_public_key);
  OPENSSL_free(peer_sigalgs);
  OPENSSL_free(peer_supported_group_list);
  OPENSSL_free(peer_key);
  OPENSSL_free(server_params);
  ssl->ctx->x509_method->hs_flush_cached_ca_names(this);
  OPENSSL_free(certificate_types);

  if (key_block != NULL) {
    OPENSSL_cleanse(key_block, key_block_len);
    OPENSSL_free(key_block);
  }
}

SSL_HANDSHAKE *ssl_handshake_new(SSL *ssl) {
  UniquePtr<SSL_HANDSHAKE> hs = MakeUnique<SSL_HANDSHAKE>(ssl);
  if (!hs ||
      !hs->transcript.Init()) {
    return nullptr;
  }
  return hs.release();
}

void ssl_handshake_free(SSL_HANDSHAKE *hs) { Delete(hs); }

int ssl_check_message_type(SSL *ssl, const SSLMessage &msg, int type) {
  if (msg.type != type) {
    ssl3_send_alert(ssl, SSL3_AL_FATAL, SSL_AD_UNEXPECTED_MESSAGE);
    OPENSSL_PUT_ERROR(SSL, SSL_R_UNEXPECTED_MESSAGE);
    ERR_add_error_dataf("got type %d, wanted type %d", msg.type, type);
    return 0;
  }

  return 1;
}

static int add_record_to_flight(SSL *ssl, uint8_t type, const uint8_t *in,
                                size_t in_len) {
  /* We'll never add a flight while in the process of writing it out. */
  assert(ssl->s3->pending_flight_offset == 0);

  if (ssl->s3->pending_flight == NULL) {
    ssl->s3->pending_flight = BUF_MEM_new();
    if (ssl->s3->pending_flight == NULL) {
      return 0;
    }
  }

  size_t max_out = in_len + SSL_max_seal_overhead(ssl);
  size_t new_cap = ssl->s3->pending_flight->length + max_out;
  if (max_out < in_len || new_cap < max_out) {
    OPENSSL_PUT_ERROR(SSL, ERR_R_OVERFLOW);
    return 0;
  }

  size_t len;
  if (!BUF_MEM_reserve(ssl->s3->pending_flight, new_cap) ||
      !tls_seal_record(ssl, (uint8_t *)ssl->s3->pending_flight->data +
                                ssl->s3->pending_flight->length,
                       &len, max_out, type, in, in_len)) {
    return 0;
  }

  ssl->s3->pending_flight->length += len;
  return 1;
}

int ssl3_init_message(SSL *ssl, CBB *cbb, CBB *body, uint8_t type) {
  /* Pick a modest size hint to save most of the |realloc| calls. */
  if (!CBB_init(cbb, 64) ||
      !CBB_add_u8(cbb, type) ||
      !CBB_add_u24_length_prefixed(cbb, body)) {
    OPENSSL_PUT_ERROR(SSL, ERR_R_INTERNAL_ERROR);
    CBB_cleanup(cbb);
    return 0;
  }

  return 1;
}

int ssl3_finish_message(SSL *ssl, CBB *cbb, uint8_t **out_msg,
                        size_t *out_len) {
  if (!CBB_finish(cbb, out_msg, out_len)) {
    OPENSSL_PUT_ERROR(SSL, ERR_R_INTERNAL_ERROR);
    return 0;
  }

  return 1;
}

int ssl3_add_message(SSL *ssl, uint8_t *msg, size_t len) {
  /* Add the message to the current flight, splitting into several records if
   * needed. */
  int ret = 0;
  size_t added = 0;
  do {
    size_t todo = len - added;
    if (todo > ssl->max_send_fragment) {
      todo = ssl->max_send_fragment;
    }

    uint8_t type = SSL3_RT_HANDSHAKE;
    if (ssl->server &&
        ssl->s3->have_version &&
        ssl->version == TLS1_3_RECORD_TYPE_EXPERIMENT_VERSION &&
        ssl->s3->aead_write_ctx->is_null_cipher()) {
      type = SSL3_RT_PLAINTEXT_HANDSHAKE;
    }

    if (!add_record_to_flight(ssl, type, msg + added, todo)) {
      goto err;
    }
    added += todo;
  } while (added < len);

  ssl_do_msg_callback(ssl, 1 /* write */, SSL3_RT_HANDSHAKE, msg, len);
  /* TODO(svaldez): Move this up a layer to fix abstraction for SSLTranscript on
   * hs. */
  if (ssl->s3->hs != NULL &&
      !ssl->s3->hs->transcript.Update(msg, len)) {
    goto err;
  }
  ret = 1;

err:
  OPENSSL_free(msg);
  return ret;
}

int ssl3_add_change_cipher_spec(SSL *ssl) {
  static const uint8_t kChangeCipherSpec[1] = {SSL3_MT_CCS};

  if (!add_record_to_flight(ssl, SSL3_RT_CHANGE_CIPHER_SPEC, kChangeCipherSpec,
                            sizeof(kChangeCipherSpec))) {
    return 0;
  }

  ssl_do_msg_callback(ssl, 1 /* write */, SSL3_RT_CHANGE_CIPHER_SPEC,
                      kChangeCipherSpec, sizeof(kChangeCipherSpec));
  return 1;
}

int ssl3_add_alert(SSL *ssl, uint8_t level, uint8_t desc) {
  uint8_t alert[2] = {level, desc};
  if (!add_record_to_flight(ssl, SSL3_RT_ALERT, alert, sizeof(alert))) {
    return 0;
  }

  ssl_do_msg_callback(ssl, 1 /* write */, SSL3_RT_ALERT, alert, sizeof(alert));
  ssl_do_info_callback(ssl, SSL_CB_WRITE_ALERT, ((int)level << 8) | desc);
  return 1;
}

int ssl_add_message_cbb(SSL *ssl, CBB *cbb) {
  uint8_t *msg;
  size_t len;
  if (!ssl->method->finish_message(ssl, cbb, &msg, &len) ||
      !ssl->method->add_message(ssl, msg, len)) {
    return 0;
  }

  return 1;
}

int ssl3_flush_flight(SSL *ssl) {
  if (ssl->s3->pending_flight == NULL) {
    return 1;
  }

  if (ssl->s3->pending_flight->length > 0xffffffff ||
      ssl->s3->pending_flight->length > INT_MAX) {
    OPENSSL_PUT_ERROR(SSL, ERR_R_INTERNAL_ERROR);
    return -1;
  }

  /* If there is pending data in the write buffer, it must be flushed out before
   * any new data in pending_flight. */
  if (ssl_write_buffer_is_pending(ssl)) {
    int ret = ssl_write_buffer_flush(ssl);
    if (ret <= 0) {
      ssl->rwstate = SSL_WRITING;
      return ret;
    }
  }

  /* Write the pending flight. */
  while (ssl->s3->pending_flight_offset < ssl->s3->pending_flight->length) {
    int ret = BIO_write(
        ssl->wbio,
        ssl->s3->pending_flight->data + ssl->s3->pending_flight_offset,
        ssl->s3->pending_flight->length - ssl->s3->pending_flight_offset);
    if (ret <= 0) {
      ssl->rwstate = SSL_WRITING;
      return ret;
    }

    ssl->s3->pending_flight_offset += ret;
  }

  if (BIO_flush(ssl->wbio) <= 0) {
    ssl->rwstate = SSL_WRITING;
    return -1;
  }

  BUF_MEM_free(ssl->s3->pending_flight);
  ssl->s3->pending_flight = NULL;
  ssl->s3->pending_flight_offset = 0;
  return 1;
}

int ssl3_send_finished(SSL_HANDSHAKE *hs) {
  SSL *const ssl = hs->ssl;
  const SSL_SESSION *session = SSL_get_session(ssl);

  uint8_t finished[EVP_MAX_MD_SIZE];
  size_t finished_len;
  if (!hs->transcript.GetFinishedMAC(finished, &finished_len, session,
                                     ssl->server, ssl3_protocol_version(ssl))) {
    return 0;
  }

  /* Log the master secret, if logging is enabled. */
  if (!ssl_log_secret(ssl, "CLIENT_RANDOM",
                      session->master_key,
                      session->master_key_length)) {
    return 0;
  }

  /* Copy the Finished so we can use it for renegotiation checks. */
  if (ssl->version != SSL3_VERSION) {
    if (finished_len > sizeof(ssl->s3->previous_client_finished) ||
        finished_len > sizeof(ssl->s3->previous_server_finished)) {
      OPENSSL_PUT_ERROR(SSL, ERR_R_INTERNAL_ERROR);
      return -1;
    }

    if (ssl->server) {
      OPENSSL_memcpy(ssl->s3->previous_server_finished, finished, finished_len);
      ssl->s3->previous_server_finished_len = finished_len;
    } else {
      OPENSSL_memcpy(ssl->s3->previous_client_finished, finished, finished_len);
      ssl->s3->previous_client_finished_len = finished_len;
    }
  }

  ScopedCBB cbb;
  CBB body;
  if (!ssl->method->init_message(ssl, cbb.get(), &body, SSL3_MT_FINISHED) ||
      !CBB_add_bytes(&body, finished, finished_len) ||
      !ssl_add_message_cbb(ssl, cbb.get())) {
    OPENSSL_PUT_ERROR(SSL, ERR_R_INTERNAL_ERROR);
    return -1;
  }

  return 1;
}

int ssl3_get_finished(SSL_HANDSHAKE *hs) {
  SSL *const ssl = hs->ssl;
  SSLMessage msg;
  int ret = ssl_read_message(ssl, &msg);
  if (ret <= 0) {
    return ret;
  }

  if (!ssl_check_message_type(ssl, msg, SSL3_MT_FINISHED)) {
    return -1;
  }

  /* Snapshot the finished hash before incorporating the new message. */
  uint8_t finished[EVP_MAX_MD_SIZE];
  size_t finished_len;
  if (!hs->transcript.GetFinishedMAC(finished, &finished_len,
                                     SSL_get_session(ssl), !ssl->server,
                                     ssl3_protocol_version(ssl)) ||
      !ssl_hash_message(hs, msg)) {
    return -1;
  }

  int finished_ok = CBS_mem_equal(&msg.body, finished, finished_len);
#if defined(BORINGSSL_UNSAFE_FUZZER_MODE)
  finished_ok = 1;
#endif
  if (!finished_ok) {
    ssl3_send_alert(ssl, SSL3_AL_FATAL, SSL_AD_DECRYPT_ERROR);
    OPENSSL_PUT_ERROR(SSL, SSL_R_DIGEST_CHECK_FAILED);
    return -1;
  }

  /* Copy the Finished so we can use it for renegotiation checks. */
  if (ssl->version != SSL3_VERSION) {
    if (finished_len > sizeof(ssl->s3->previous_client_finished) ||
        finished_len > sizeof(ssl->s3->previous_server_finished)) {
      OPENSSL_PUT_ERROR(SSL, ERR_R_INTERNAL_ERROR);
      return -1;
    }

    if (ssl->server) {
      OPENSSL_memcpy(ssl->s3->previous_client_finished, finished, finished_len);
      ssl->s3->previous_client_finished_len = finished_len;
    } else {
      OPENSSL_memcpy(ssl->s3->previous_server_finished, finished, finished_len);
      ssl->s3->previous_server_finished_len = finished_len;
    }
  }

  ssl->method->next_message(ssl);
  return 1;
}

int ssl3_output_cert_chain(SSL *ssl) {
  ScopedCBB cbb;
  CBB body;
  if (!ssl->method->init_message(ssl, cbb.get(), &body, SSL3_MT_CERTIFICATE) ||
      !ssl_add_cert_chain(ssl, &body) ||
      !ssl_add_message_cbb(ssl, cbb.get())) {
    OPENSSL_PUT_ERROR(SSL, ERR_R_INTERNAL_ERROR);
    return 0;
  }

  return 1;
}

size_t ssl_max_handshake_message_len(const SSL *ssl) {
  /* kMaxMessageLen is the default maximum message size for handshakes which do
   * not accept peer certificate chains. */
  static const size_t kMaxMessageLen = 16384;

  if (SSL_in_init(ssl)) {
    if ((!ssl->server || (ssl->verify_mode & SSL_VERIFY_PEER)) &&
        kMaxMessageLen < ssl->max_cert_list) {
      return ssl->max_cert_list;
    }
    return kMaxMessageLen;
  }

  if (ssl3_protocol_version(ssl) < TLS1_3_VERSION) {
    /* In TLS 1.2 and below, the largest acceptable post-handshake message is
     * a HelloRequest. */
    return 0;
  }

  if (ssl->server) {
    /* The largest acceptable post-handshake message for a server is a
     * KeyUpdate. We will never initiate post-handshake auth. */
    return 1;
  }

  /* Clients must accept NewSessionTicket and CertificateRequest, so allow the
   * default size. */
  return kMaxMessageLen;
}

int ssl_read_message(SSL *ssl, SSLMessage *out) {
  while (!ssl->method->get_message(ssl, out)) {
    int ret = ssl->method->read_message(ssl);
    if (ret <= 0) {
      return ret;
    }
  }
  return 1;
}

static int extend_handshake_buffer(SSL *ssl, size_t length) {
  if (!BUF_MEM_reserve(ssl->init_buf, length)) {
    return -1;
  }
  while (ssl->init_buf->length < length) {
    int ret = ssl3_read_handshake_bytes(
        ssl, (uint8_t *)ssl->init_buf->data + ssl->init_buf->length,
        length - ssl->init_buf->length);
    if (ret <= 0) {
      return ret;
    }
    ssl->init_buf->length += (size_t)ret;
  }
  return 1;
}

static int read_v2_client_hello(SSL *ssl) {
  /* Read the first 5 bytes, the size of the TLS record header. This is
   * sufficient to detect a V2ClientHello and ensures that we never read beyond
   * the first record. */
  int ret = ssl_read_buffer_extend_to(ssl, SSL3_RT_HEADER_LENGTH);
  if (ret <= 0) {
    return ret;
  }
  const uint8_t *p = ssl_read_buffer(ssl);

  /* Some dedicated error codes for protocol mixups should the application wish
   * to interpret them differently. (These do not overlap with ClientHello or
   * V2ClientHello.) */
  if (strncmp("GET ", (const char *)p, 4) == 0 ||
      strncmp("POST ", (const char *)p, 5) == 0 ||
      strncmp("HEAD ", (const char *)p, 5) == 0 ||
      strncmp("PUT ", (const char *)p, 4) == 0) {
    OPENSSL_PUT_ERROR(SSL, SSL_R_HTTP_REQUEST);
    return -1;
  }
  if (strncmp("CONNE", (const char *)p, 5) == 0) {
    OPENSSL_PUT_ERROR(SSL, SSL_R_HTTPS_PROXY_REQUEST);
    return -1;
  }

  if ((p[0] & 0x80) == 0 || p[2] != SSL2_MT_CLIENT_HELLO ||
      p[3] != SSL3_VERSION_MAJOR) {
    /* Not a V2ClientHello. */
    return 1;
  }

  /* Determine the length of the V2ClientHello. */
  size_t msg_length = ((p[0] & 0x7f) << 8) | p[1];
  if (msg_length > (1024 * 4)) {
    OPENSSL_PUT_ERROR(SSL, SSL_R_RECORD_TOO_LARGE);
    return -1;
  }
  if (msg_length < SSL3_RT_HEADER_LENGTH - 2) {
    /* Reject lengths that are too short early. We have already read
     * |SSL3_RT_HEADER_LENGTH| bytes, so we should not attempt to process an
     * (invalid) V2ClientHello which would be shorter than that. */
    OPENSSL_PUT_ERROR(SSL, SSL_R_RECORD_LENGTH_MISMATCH);
    return -1;
  }

  /* Read the remainder of the V2ClientHello. */
  ret = ssl_read_buffer_extend_to(ssl, 2 + msg_length);
  if (ret <= 0) {
    return ret;
  }

  CBS v2_client_hello;
  CBS_init(&v2_client_hello, ssl_read_buffer(ssl) + 2, msg_length);

  /* The V2ClientHello without the length is incorporated into the handshake
   * hash. This is only ever called at the start of the handshake, so hs is
   * guaranteed to be non-NULL. */
  if (!ssl->s3->hs->transcript.Update(CBS_data(&v2_client_hello),
                                      CBS_len(&v2_client_hello))) {
    return -1;
  }

  ssl_do_msg_callback(ssl, 0 /* read */, 0 /* V2ClientHello */,
                      CBS_data(&v2_client_hello), CBS_len(&v2_client_hello));

  uint8_t msg_type;
  uint16_t version, cipher_spec_length, session_id_length, challenge_length;
  CBS cipher_specs, session_id, challenge;
  if (!CBS_get_u8(&v2_client_hello, &msg_type) ||
      !CBS_get_u16(&v2_client_hello, &version) ||
      !CBS_get_u16(&v2_client_hello, &cipher_spec_length) ||
      !CBS_get_u16(&v2_client_hello, &session_id_length) ||
      !CBS_get_u16(&v2_client_hello, &challenge_length) ||
      !CBS_get_bytes(&v2_client_hello, &cipher_specs, cipher_spec_length) ||
      !CBS_get_bytes(&v2_client_hello, &session_id, session_id_length) ||
      !CBS_get_bytes(&v2_client_hello, &challenge, challenge_length) ||
      CBS_len(&v2_client_hello) != 0) {
    OPENSSL_PUT_ERROR(SSL, SSL_R_DECODE_ERROR);
    return -1;
  }

  /* msg_type has already been checked. */
  assert(msg_type == SSL2_MT_CLIENT_HELLO);

  /* The client_random is the V2ClientHello challenge. Truncate or
   * left-pad with zeros as needed. */
  size_t rand_len = CBS_len(&challenge);
  if (rand_len > SSL3_RANDOM_SIZE) {
    rand_len = SSL3_RANDOM_SIZE;
  }
  uint8_t random[SSL3_RANDOM_SIZE];
  OPENSSL_memset(random, 0, SSL3_RANDOM_SIZE);
  OPENSSL_memcpy(random + (SSL3_RANDOM_SIZE - rand_len), CBS_data(&challenge),
                 rand_len);

  /* Write out an equivalent SSLv3 ClientHello. */
  size_t max_v3_client_hello = SSL3_HM_HEADER_LENGTH + 2 /* version */ +
                               SSL3_RANDOM_SIZE + 1 /* session ID length */ +
                               2 /* cipher list length */ +
                               CBS_len(&cipher_specs) / 3 * 2 +
                               1 /* compression length */ + 1 /* compression */;
  ScopedCBB client_hello;
  CBB hello_body, cipher_suites;
  if (!BUF_MEM_reserve(ssl->init_buf, max_v3_client_hello) ||
      !CBB_init_fixed(client_hello.get(), (uint8_t *)ssl->init_buf->data,
                      ssl->init_buf->max) ||
      !CBB_add_u8(client_hello.get(), SSL3_MT_CLIENT_HELLO) ||
      !CBB_add_u24_length_prefixed(client_hello.get(), &hello_body) ||
      !CBB_add_u16(&hello_body, version) ||
      !CBB_add_bytes(&hello_body, random, SSL3_RANDOM_SIZE) ||
      /* No session id. */
      !CBB_add_u8(&hello_body, 0) ||
      !CBB_add_u16_length_prefixed(&hello_body, &cipher_suites)) {
    OPENSSL_PUT_ERROR(SSL, ERR_R_MALLOC_FAILURE);
    return -1;
  }

  /* Copy the cipher suites. */
  while (CBS_len(&cipher_specs) > 0) {
    uint32_t cipher_spec;
    if (!CBS_get_u24(&cipher_specs, &cipher_spec)) {
      OPENSSL_PUT_ERROR(SSL, SSL_R_DECODE_ERROR);
      return -1;
    }

    /* Skip SSLv2 ciphers. */
    if ((cipher_spec & 0xff0000) != 0) {
      continue;
    }
    if (!CBB_add_u16(&cipher_suites, cipher_spec)) {
      OPENSSL_PUT_ERROR(SSL, ERR_R_INTERNAL_ERROR);
      return -1;
    }
  }

  /* Add the null compression scheme and finish. */
  if (!CBB_add_u8(&hello_body, 1) ||
      !CBB_add_u8(&hello_body, 0) ||
      !CBB_finish(client_hello.get(), NULL, &ssl->init_buf->length)) {
    OPENSSL_PUT_ERROR(SSL, ERR_R_INTERNAL_ERROR);
    return -1;
  }

  /* Consume and discard the V2ClientHello. */
  ssl_read_buffer_consume(ssl, 2 + msg_length);
  ssl_read_buffer_discard(ssl);

  ssl->s3->is_v2_hello = 1;
  return 1;
}

/* TODO(davidben): Remove |out_bytes_needed| and inline into |ssl3_get_message|
 * when the entire record is copied into |init_buf|. */
static bool parse_message(SSL *ssl, SSLMessage *out, size_t *out_bytes_needed) {
  if (ssl->init_buf == NULL) {
    *out_bytes_needed = 4;
    return false;
  }

  CBS cbs;
  uint32_t len;
  CBS_init(&cbs, reinterpret_cast<const uint8_t *>(ssl->init_buf->data),
           ssl->init_buf->length);
  if (!CBS_get_u8(&cbs, &out->type) ||
      !CBS_get_u24(&cbs, &len)) {
    *out_bytes_needed = 4;
    return false;
  }

  if (!CBS_get_bytes(&cbs, &out->body, len)) {
    *out_bytes_needed = 4 + len;
    return false;
  }

  CBS_init(&out->raw, reinterpret_cast<const uint8_t *>(ssl->init_buf->data),
           4 + len);
  out->is_v2_hello = ssl->s3->is_v2_hello;
  if (!ssl->s3->has_message) {
    if (!out->is_v2_hello) {
      ssl_do_msg_callback(ssl, 0 /* read */, SSL3_RT_HANDSHAKE,
                          CBS_data(&out->raw), CBS_len(&out->raw));
    }
    ssl->s3->has_message = 1;
  }
  return true;
}

bool ssl3_get_message(SSL *ssl, SSLMessage *out) {
  size_t unused;
  return parse_message(ssl, out, &unused);
}

int ssl3_read_message(SSL *ssl) {
  SSLMessage msg;
  size_t bytes_needed;
  if (parse_message(ssl, &msg, &bytes_needed)) {
    OPENSSL_PUT_ERROR(SSL, ERR_R_INTERNAL_ERROR);
    return -1;
  }

  /* Enforce the limit so the peer cannot force us to buffer 16MB. */
  if (bytes_needed > 4 + ssl_max_handshake_message_len(ssl)) {
    ssl3_send_alert(ssl, SSL3_AL_FATAL, SSL_AD_ILLEGAL_PARAMETER);
    OPENSSL_PUT_ERROR(SSL, SSL_R_EXCESSIVE_MESSAGE_SIZE);
    return -1;
  }

  /* Re-create the handshake buffer if needed. */
  if (ssl->init_buf == NULL) {
    ssl->init_buf = BUF_MEM_new();
    if (ssl->init_buf == NULL) {
      return -1;
    }
  }

  /* Bypass the record layer for the first message to handle V2ClientHello. */
  if (ssl->server && !ssl->s3->v2_hello_done) {
    int ret = read_v2_client_hello(ssl);
    if (ret > 0) {
      ssl->s3->v2_hello_done = 1;
    }
    return ret;
  }

  return extend_handshake_buffer(ssl, bytes_needed);
}

bool ssl_hash_message(SSL_HANDSHAKE *hs, const SSLMessage &msg) {
  /* V2ClientHello messages are pre-hashed. */
  if (msg.is_v2_hello) {
    return true;
  }

  return hs->transcript.Update(CBS_data(&msg.raw), CBS_len(&msg.raw));
}

void ssl3_next_message(SSL *ssl) {
  SSLMessage msg;
  if (!ssl3_get_message(ssl, &msg) ||
      ssl->init_buf == NULL ||
      ssl->init_buf->length < CBS_len(&msg.raw)) {
    assert(0);
    return;
  }

  OPENSSL_memmove(ssl->init_buf->data, ssl->init_buf->data + CBS_len(&msg.raw),
                  ssl->init_buf->length - CBS_len(&msg.raw));
  ssl->init_buf->length -= CBS_len(&msg.raw);
  ssl->s3->is_v2_hello = 0;
  ssl->s3->has_message = 0;

  /* Post-handshake messages are rare, so release the buffer after every
   * message. During the handshake, |on_handshake_complete| will release it. */
  if (!SSL_in_init(ssl) && ssl->init_buf->length == 0) {
    BUF_MEM_free(ssl->init_buf);
    ssl->init_buf = NULL;
  }
}

int ssl_parse_extensions(const CBS *cbs, uint8_t *out_alert,
                         const SSL_EXTENSION_TYPE *ext_types,
                         size_t num_ext_types, int ignore_unknown) {
  /* Reset everything. */
  for (size_t i = 0; i < num_ext_types; i++) {
    *ext_types[i].out_present = 0;
    CBS_init(ext_types[i].out_data, NULL, 0);
  }

  CBS copy = *cbs;
  while (CBS_len(&copy) != 0) {
    uint16_t type;
    CBS data;
    if (!CBS_get_u16(&copy, &type) ||
        !CBS_get_u16_length_prefixed(&copy, &data)) {
      OPENSSL_PUT_ERROR(SSL, SSL_R_PARSE_TLSEXT);
      *out_alert = SSL_AD_DECODE_ERROR;
      return 0;
    }

    const SSL_EXTENSION_TYPE *ext_type = NULL;
    for (size_t i = 0; i < num_ext_types; i++) {
      if (type == ext_types[i].type) {
        ext_type = &ext_types[i];
        break;
      }
    }

    if (ext_type == NULL) {
      if (ignore_unknown) {
        continue;
      }
      OPENSSL_PUT_ERROR(SSL, SSL_R_UNEXPECTED_EXTENSION);
      *out_alert = SSL_AD_UNSUPPORTED_EXTENSION;
      return 0;
    }

    /* Duplicate ext_types are forbidden. */
    if (*ext_type->out_present) {
      OPENSSL_PUT_ERROR(SSL, SSL_R_DUPLICATE_EXTENSION);
      *out_alert = SSL_AD_ILLEGAL_PARAMETER;
      return 0;
    }

    *ext_type->out_present = 1;
    *ext_type->out_data = data;
  }

  return 1;
}

static void set_crypto_buffer(CRYPTO_BUFFER **dest, CRYPTO_BUFFER *src) {
  /* TODO(davidben): Remove this helper once |SSL_SESSION| can use |UniquePtr|
   * and |UniquePtr| has up_ref helpers. */
  CRYPTO_BUFFER_free(*dest);
  *dest = src;
  if (src != nullptr) {
    CRYPTO_BUFFER_up_ref(src);
  }
}

enum ssl_verify_result_t ssl_verify_peer_cert(SSL_HANDSHAKE *hs) {
  SSL *const ssl = hs->ssl;
  const SSL_SESSION *prev_session = ssl->s3->established_session;
  if (prev_session != NULL) {
    /* If renegotiating, the server must not change the server certificate. See
     * https://mitls.org/pages/attacks/3SHAKE. We never resume on renegotiation,
     * so this check is sufficient to ensure the reported peer certificate never
     * changes on renegotiation. */
    assert(!ssl->server);
    if (sk_CRYPTO_BUFFER_num(prev_session->certs) !=
        sk_CRYPTO_BUFFER_num(hs->new_session->certs)) {
      OPENSSL_PUT_ERROR(SSL, SSL_R_SERVER_CERT_CHANGED);
      ssl3_send_alert(ssl, SSL3_AL_FATAL, SSL_AD_ILLEGAL_PARAMETER);
      return ssl_verify_invalid;
    }

    for (size_t i = 0; i < sk_CRYPTO_BUFFER_num(hs->new_session->certs); i++) {
      const CRYPTO_BUFFER *old_cert =
          sk_CRYPTO_BUFFER_value(prev_session->certs, i);
      const CRYPTO_BUFFER *new_cert =
          sk_CRYPTO_BUFFER_value(hs->new_session->certs, i);
      if (CRYPTO_BUFFER_len(old_cert) != CRYPTO_BUFFER_len(new_cert) ||
          OPENSSL_memcmp(CRYPTO_BUFFER_data(old_cert),
                         CRYPTO_BUFFER_data(new_cert),
                         CRYPTO_BUFFER_len(old_cert)) != 0) {
        OPENSSL_PUT_ERROR(SSL, SSL_R_SERVER_CERT_CHANGED);
        ssl3_send_alert(ssl, SSL3_AL_FATAL, SSL_AD_ILLEGAL_PARAMETER);
        return ssl_verify_invalid;
      }
    }

    /* The certificate is identical, so we may skip re-verifying the
     * certificate. Since we only authenticated the previous one, copy other
     * authentication from the established session and ignore what was newly
     * received. */
    set_crypto_buffer(&hs->new_session->ocsp_response,
                      prev_session->ocsp_response);
    set_crypto_buffer(&hs->new_session->signed_cert_timestamp_list,
                      prev_session->signed_cert_timestamp_list);
    hs->new_session->verify_result = prev_session->verify_result;
    return ssl_verify_ok;
  }

  uint8_t alert = SSL_AD_CERTIFICATE_UNKNOWN;
  enum ssl_verify_result_t ret;
  if (ssl->custom_verify_callback != nullptr) {
    ret = ssl->custom_verify_callback(ssl, &alert);
    switch (ret) {
      case ssl_verify_ok:
        hs->new_session->verify_result = X509_V_OK;
        break;
      case ssl_verify_invalid:
        hs->new_session->verify_result = X509_V_ERR_APPLICATION_VERIFICATION;
        break;
      case ssl_verify_retry:
        break;
    }
  } else {
    ret = ssl->ctx->x509_method->session_verify_cert_chain(
              hs->new_session.get(), ssl, &alert)
              ? ssl_verify_ok
              : ssl_verify_invalid;
  }

  if (ret == ssl_verify_invalid) {
    OPENSSL_PUT_ERROR(SSL, SSL_R_CERTIFICATE_VERIFY_FAILED);
    ssl3_send_alert(ssl, SSL3_AL_FATAL, alert);
  }

  return ret;
}

uint16_t ssl_get_grease_value(const SSL *ssl, enum ssl_grease_index_t index) {
  /* Use the client_random or server_random for entropy. This both avoids
   * calling |RAND_bytes| on a single byte repeatedly and ensures the values are
   * deterministic. This allows the same ClientHello be sent twice for a
   * HelloRetryRequest or the same group be advertised in both supported_groups
   * and key_shares. */
  uint16_t ret = ssl->server ? ssl->s3->server_random[index]
                             : ssl->s3->client_random[index];
  /* The first four bytes of server_random are a timestamp prior to TLS 1.3, but
   * servers have no fields to GREASE until TLS 1.3. */
  assert(!ssl->server || ssl3_protocol_version(ssl) >= TLS1_3_VERSION);
  /* This generates a random value of the form 0xωaωa, for all 0 ≤ ω < 16. */
  ret = (ret & 0xf0) | 0x0a;
  ret |= ret << 8;
  return ret;
}

}  // namespace bssl
