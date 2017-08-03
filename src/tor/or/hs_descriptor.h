/* Copyright (c) 2016, The Tor Project, Inc. */
/* See LICENSE for licensing information */

/**
 * \file hs_descriptor.h
 * \brief Header file for hs_descriptor.c
 **/

#ifndef TOR_HS_DESCRIPTOR_H
#define TOR_HS_DESCRIPTOR_H

#include <stdint.h>

#include "or.h"
#include "address.h"
#include "container.h"
#include "crypto.h"
#include "crypto_ed25519.h"
#include "torcert.h"

/* The earliest descriptor format version we support. */
#define HS_DESC_SUPPORTED_FORMAT_VERSION_MIN 3
/* The latest descriptor format version we support. */
#define HS_DESC_SUPPORTED_FORMAT_VERSION_MAX 3

/* Maximum lifetime of a descriptor in seconds. The value is set at 12 hours
 * which is 720 minutes or 43200 seconds. */
#define HS_DESC_MAX_LIFETIME (12 * 60 * 60)
/* Lifetime of certificate in the descriptor. This defines the lifetime of the
 * descriptor signing key and the cross certification cert of that key. */
#define HS_DESC_CERT_LIFETIME (24 * 60 * 60)
/* Length of the salt needed for the encrypted section of a descriptor. */
#define HS_DESC_ENCRYPTED_SALT_LEN 16
/* Length of the secret input needed for the KDF construction which derives
 * the encryption key for the encrypted data section of the descriptor. This
 * adds up to 68 bytes being the blinded key, hashed subcredential and
 * revision counter. */
#define HS_DESC_ENCRYPTED_SECRET_INPUT_LEN \
  ED25519_PUBKEY_LEN + DIGEST256_LEN + sizeof(uint64_t)
/* Length of the KDF output value which is the length of the secret key,
 * the secret IV and MAC key length which is the length of H() output. */
#define HS_DESC_ENCRYPTED_KDF_OUTPUT_LEN \
  CIPHER256_KEY_LEN + CIPHER_IV_LEN + DIGEST256_LEN
/* We need to pad the plaintext version of the encrypted data section before
 * encryption and it has to be a multiple of this value. */
#define HS_DESC_PLAINTEXT_PADDING_MULTIPLE 128
/* XXX: Let's make sure this makes sense as an upper limit for the padded
 * plaintext section. Then we should enforce it as now only an assert will be
 * triggered if we are above it. */
/* Once padded, this is the maximum length in bytes for the plaintext. */
#define HS_DESC_PADDED_PLAINTEXT_MAX_LEN 8192
/* Minimum length in bytes of the encrypted portion of the descriptor. */
#define HS_DESC_ENCRYPTED_MIN_LEN \
  HS_DESC_ENCRYPTED_SALT_LEN + \
  HS_DESC_PLAINTEXT_PADDING_MULTIPLE + DIGEST256_LEN
/* Maximum length in bytes of a full hidden service descriptor. */
#define HS_DESC_MAX_LEN 50000 /* 50kb max size */
/* The minimum amount of fields a descriptor should contain. The parsing of
 * the fields are version specific so the only required field, as a generic
 * view of a descriptor, is 1 that is the version field. */
#define HS_DESC_PLAINTEXT_MIN_FIELDS 1

/* Key length for the descriptor symmetric encryption. As specified in the
 * protocol, we use AES-256 for the encrypted section of the descriptor. The
 * following is the length in bytes and the bit size. */
#define HS_DESC_ENCRYPTED_KEY_LEN CIPHER256_KEY_LEN
#define HS_DESC_ENCRYPTED_BIT_SIZE (HS_DESC_ENCRYPTED_KEY_LEN * 8)

/* Type of authentication in the descriptor. */
typedef enum {
  HS_DESC_AUTH_PASSWORD = 1,
  HS_DESC_AUTH_ED25519  = 2,
} hs_desc_auth_type_t;

/* Type of encryption key in the descriptor. */
typedef enum {
  HS_DESC_KEY_TYPE_LEGACY     = 1,
  HS_DESC_KEY_TYPE_CURVE25519 = 2,
} hs_desc_key_type_t;

/* Link specifier object that contains information on how to extend to the
 * relay that is the address, port and handshake type. */
typedef struct hs_desc_link_specifier_t {
  /* Indicate the type of link specifier. See trunnel ed25519_cert
   * specification. */
  uint8_t type;

  /* It's either an address/port or a legacy identity fingerprint. */
  union {
    /* IP address and port of the relay use to extend. */
    tor_addr_port_t ap;
    /* Legacy identity. A 20-byte SHA1 identity fingerprint. */
    uint8_t legacy_id[DIGEST_LEN];
  } u;
} hs_desc_link_specifier_t;

/* Introduction point information located in a descriptor. */
typedef struct hs_desc_intro_point_t {
  /* Link specifier(s) which details how to extend to the relay. This list
   * contains hs_desc_link_specifier_t object. It MUST have at least one. */
  smartlist_t *link_specifiers;

  /* Authentication key used to establish the introduction point circuit and
   * cross-certifies the blinded public key for the replica thus signed by
   * the blinded key and in turn signs it. */
  tor_cert_t *auth_key_cert;

  /* Encryption key type so we know which one to use in the union below. */
  hs_desc_key_type_t enc_key_type;

  /* Keys are mutually exclusive thus the union. */
  union {
    /* Encryption key used to encrypt request to hidden service. */
    curve25519_keypair_t curve25519;

    /* Backward compat: RSA 1024 encryption key for legacy purposes.
     * Mutually exclusive with enc_key. */
    crypto_pk_t *legacy;
  } enc_key;

  /* True iff the introduction point has passed the cross certification. Upon
   * decoding an intro point, this must be true. */
  unsigned int cross_certified : 1;
} hs_desc_intro_point_t;

/* The encrypted data section of a descriptor. Obviously the data in this is
 * in plaintext but encrypted once encoded. */
typedef struct hs_desc_encrypted_data_t {
  /* Bitfield of CREATE2 cell supported formats. The only currently supported
   * format is ntor. */
  unsigned int create2_ntor : 1;

  /* A list of authentication types that a client must at least support one
   * in order to contact the service. Contains NULL terminated strings. */
  smartlist_t *auth_types;

  /* Is this descriptor a single onion service? */
  unsigned int single_onion_service : 1;

  /* A list of intro points. Contains hs_desc_intro_point_t objects. */
  smartlist_t *intro_points;
} hs_desc_encrypted_data_t;

/* Plaintext data that is unencrypted information of the descriptor. */
typedef struct hs_desc_plaintext_data_t {
  /* Version of the descriptor format. Spec specifies this field as a
   * positive integer. */
  uint32_t version;

  /* The lifetime of the descriptor in seconds. */
  uint32_t lifetime_sec;

  /* Certificate with the short-term ed22519 descriptor signing key for the
   * replica which is signed by the blinded public key for that replica. */
  tor_cert_t *signing_key_cert;

  /* Signing public key which is used to sign the descriptor. Same public key
   * as in the signing key certificate. */
  ed25519_public_key_t signing_pubkey;

  /* Blinded public key used for this descriptor derived from the master
   * identity key and generated for a specific replica number. */
  ed25519_public_key_t blinded_pubkey;

  /* Revision counter is incremented at each upload, regardless of whether
   * the descriptor has changed. This avoids leaking whether the descriptor
   * has changed. Spec specifies this as a 8 bytes positive integer. */
  uint64_t revision_counter;

  /* Decoding only: The base64-decoded encrypted blob from the descriptor */
  uint8_t *encrypted_blob;

  /* Decoding only: Size of the encrypted_blob */
  size_t encrypted_blob_size;
} hs_desc_plaintext_data_t;

/* Service descriptor in its decoded form. */
typedef struct hs_descriptor_t {
  /* Contains the plaintext part of the descriptor. */
  hs_desc_plaintext_data_t plaintext_data;

  /* The following contains what's in the encrypted part of the descriptor.
   * It's only encrypted in the encoded version of the descriptor thus the
   * data contained in that object is in plaintext. */
  hs_desc_encrypted_data_t encrypted_data;

  /* Subcredentials of a service, used by the client and service to decrypt
   * the encrypted data. */
  uint8_t subcredential[DIGEST256_LEN];
} hs_descriptor_t;

/* Return true iff the given descriptor format version is supported. */
static inline int
hs_desc_is_supported_version(uint32_t version)
{
  if (version < HS_DESC_SUPPORTED_FORMAT_VERSION_MIN ||
      version > HS_DESC_SUPPORTED_FORMAT_VERSION_MAX) {
    return 0;
  }
  return 1;
}

/* Public API. */

void hs_descriptor_free(hs_descriptor_t *desc);
void hs_desc_plaintext_data_free(hs_desc_plaintext_data_t *desc);
void hs_desc_encrypted_data_free(hs_desc_encrypted_data_t *desc);

int hs_desc_encode_descriptor(const hs_descriptor_t *desc,
                              const ed25519_keypair_t *signing_kp,
                              char **encoded_out);

int hs_desc_decode_descriptor(const char *encoded,
                              const uint8_t *subcredential,
                              hs_descriptor_t **desc_out);
int hs_desc_decode_plaintext(const char *encoded,
                             hs_desc_plaintext_data_t *plaintext);
int hs_desc_decode_encrypted(const hs_descriptor_t *desc,
                             hs_desc_encrypted_data_t *desc_out);

size_t hs_desc_plaintext_obj_size(const hs_desc_plaintext_data_t *data);

#ifdef HS_DESCRIPTOR_PRIVATE

/* Encoding. */
STATIC char *encode_link_specifiers(const smartlist_t *specs);
STATIC size_t build_plaintext_padding(const char *plaintext,
                                      size_t plaintext_len,
                                      uint8_t **padded_out);
/* Decoding. */
STATIC smartlist_t *decode_link_specifiers(const char *encoded);
STATIC hs_desc_intro_point_t *decode_introduction_point(
                                const hs_descriptor_t *desc,
                                const char *text);
STATIC int decode_intro_points(const hs_descriptor_t *desc,
                               hs_desc_encrypted_data_t *desc_enc,
                               const char *data);
STATIC int encrypted_data_length_is_valid(size_t len);
STATIC int cert_is_valid(tor_cert_t *cert, uint8_t type,
                         const char *log_obj_type);
STATIC int desc_sig_is_valid(const char *b64_sig,
                             const ed25519_public_key_t *signing_pubkey,
                             const char *encoded_desc, size_t encoded_len);
STATIC void desc_intro_point_free(hs_desc_intro_point_t *ip);
#endif /* HS_DESCRIPTOR_PRIVATE */

#endif /* TOR_HS_DESCRIPTOR_H */

