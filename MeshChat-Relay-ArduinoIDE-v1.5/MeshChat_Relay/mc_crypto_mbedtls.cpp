// ESP32 backend (mbedTLS as shipped with Arduino-ESP32 2.0.x). Compiled on the board only.
#if defined(ARDUINO) && !defined(MC_HOST)
#include "mc_crypto_backend.h"
#include <esp_system.h>
#include <mbedtls/ecp.h>
#include <mbedtls/ecdsa.h>
#include <mbedtls/ecdh.h>
#include <mbedtls/gcm.h>
#include <mbedtls/md.h>
#include <mbedtls/bignum.h>

namespace mc { namespace crypto { namespace be {

static int rng(void*, unsigned char* out, size_t n) { esp_fill_random(out, n); return 0; }

void random(uint8_t* out, size_t n) { esp_fill_random(out, n); }

void sha256(const uint8_t* in, size_t n, uint8_t out[32]) {
    mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), in, n, out);
}

void hmacSha256(const uint8_t* key, size_t klen, const uint8_t* data, size_t dlen, uint8_t out[32]) {
    mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), key, klen, data, dlen, out);
}

namespace {
struct Group {
    mbedtls_ecp_group g;
    Group() { mbedtls_ecp_group_init(&g); mbedtls_ecp_group_load(&g, MBEDTLS_ECP_DP_SECP256R1); }
    ~Group() { mbedtls_ecp_group_free(&g); }
};
}  // namespace

bool ecGenerate(uint8_t priv[32], uint8_t pub[65]) {
    Group G;
    mbedtls_mpi d; mbedtls_ecp_point Q;
    mbedtls_mpi_init(&d); mbedtls_ecp_point_init(&Q);
    size_t olen = 0;
    bool ok = mbedtls_ecp_gen_keypair(&G.g, &d, &Q, rng, nullptr) == 0 &&
              mbedtls_mpi_write_binary(&d, priv, 32) == 0 &&
              mbedtls_ecp_point_write_binary(&G.g, &Q, MBEDTLS_ECP_PF_UNCOMPRESSED, &olen, pub, 65) == 0 && olen == 65;
    mbedtls_mpi_free(&d); mbedtls_ecp_point_free(&Q);
    return ok;
}

bool ecPublic(const uint8_t priv[32], uint8_t pub[65]) {
    Group G;
    mbedtls_mpi d; mbedtls_ecp_point Q;
    mbedtls_mpi_init(&d); mbedtls_ecp_point_init(&Q);
    size_t olen = 0;
    bool ok = mbedtls_mpi_read_binary(&d, priv, 32) == 0 &&
              mbedtls_ecp_mul(&G.g, &Q, &d, &G.g.G, rng, nullptr) == 0 &&
              mbedtls_ecp_point_write_binary(&G.g, &Q, MBEDTLS_ECP_PF_UNCOMPRESSED, &olen, pub, 65) == 0 && olen == 65;
    mbedtls_mpi_free(&d); mbedtls_ecp_point_free(&Q);
    return ok;
}

bool ecdsaSign(const uint8_t priv[32], const uint8_t hash[32], uint8_t rs[64]) {
    Group G;
    mbedtls_mpi d, r, s;
    mbedtls_mpi_init(&d); mbedtls_mpi_init(&r); mbedtls_mpi_init(&s);
    bool ok = mbedtls_mpi_read_binary(&d, priv, 32) == 0 &&
              mbedtls_ecdsa_sign(&G.g, &r, &s, &d, hash, 32, rng, nullptr) == 0 &&
              mbedtls_mpi_write_binary(&r, rs, 32) == 0 && mbedtls_mpi_write_binary(&s, rs + 32, 32) == 0;
    mbedtls_mpi_free(&d); mbedtls_mpi_free(&r); mbedtls_mpi_free(&s);
    return ok;
}

bool ecdsaVerify(const uint8_t pub[65], const uint8_t hash[32], const uint8_t rs[64]) {
    Group G;
    mbedtls_ecp_point Q; mbedtls_mpi r, s;
    mbedtls_ecp_point_init(&Q); mbedtls_mpi_init(&r); mbedtls_mpi_init(&s);
    bool ok = mbedtls_ecp_point_read_binary(&G.g, &Q, pub, 65) == 0 && mbedtls_ecp_check_pubkey(&G.g, &Q) == 0 &&
              mbedtls_mpi_read_binary(&r, rs, 32) == 0 && mbedtls_mpi_read_binary(&s, rs + 32, 32) == 0 &&
              mbedtls_ecdsa_verify(&G.g, hash, 32, &Q, &r, &s) == 0;
    mbedtls_ecp_point_free(&Q); mbedtls_mpi_free(&r); mbedtls_mpi_free(&s);
    return ok;
}

bool ecdh(const uint8_t priv[32], const uint8_t peerPub[65], uint8_t out[32]) {
    Group G;
    mbedtls_mpi d, z; mbedtls_ecp_point Q;
    mbedtls_mpi_init(&d); mbedtls_mpi_init(&z); mbedtls_ecp_point_init(&Q);
    bool ok = mbedtls_mpi_read_binary(&d, priv, 32) == 0 &&
              mbedtls_ecp_point_read_binary(&G.g, &Q, peerPub, 65) == 0 && mbedtls_ecp_check_pubkey(&G.g, &Q) == 0 &&
              mbedtls_ecdh_compute_shared(&G.g, &z, &Q, &d, rng, nullptr) == 0 &&
              mbedtls_mpi_write_binary(&z, out, 32) == 0;
    mbedtls_mpi_free(&d); mbedtls_mpi_free(&z); mbedtls_ecp_point_free(&Q);
    return ok;
}

bool gcmEncrypt(const uint8_t key[32], const uint8_t nonce[12], const uint8_t* aad, size_t aadLen,
                const uint8_t* in, size_t n, uint8_t* out, uint8_t tag[16]) {
    mbedtls_gcm_context c;
    mbedtls_gcm_init(&c);
    bool ok = mbedtls_gcm_setkey(&c, MBEDTLS_CIPHER_ID_AES, key, 256) == 0 &&
              mbedtls_gcm_crypt_and_tag(&c, MBEDTLS_GCM_ENCRYPT, n, nonce, 12, aad, aadLen, in, out, 16, tag) == 0;
    mbedtls_gcm_free(&c);
    return ok;
}

bool gcmDecrypt(const uint8_t key[32], const uint8_t nonce[12], const uint8_t* aad, size_t aadLen,
                const uint8_t* in, size_t n, const uint8_t tag[16], uint8_t* out) {
    mbedtls_gcm_context c;
    mbedtls_gcm_init(&c);
    bool ok = mbedtls_gcm_setkey(&c, MBEDTLS_CIPHER_ID_AES, key, 256) == 0 &&
              mbedtls_gcm_auth_decrypt(&c, n, nonce, 12, aad, aadLen, tag, 16, in, out) == 0;
    mbedtls_gcm_free(&c);
    return ok;
}

}}}  // namespace mc::crypto::be
#endif
