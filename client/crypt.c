/* cryptography (ECDSA), T13.654-T13.847 $DVS:time$ */

#include <string.h>
#include <openssl/crypto.h>
#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#include <openssl/ecdsa.h>
#include "crypt.h"
#include "transport.h"
#include "log.h"
#include "system.h"

static EC_GROUP *group;

extern unsigned int xOPENSSL_ia32cap_P[4];
extern int xOPENSSL_ia32_cpuid(unsigned int *);

// initialization of the encryption system
int cheatcoin_crypt_init(int withrandom)
{
	if (withrandom) {
		uint64_t buf[64];
		xOPENSSL_ia32_cpuid(xOPENSSL_ia32cap_P);
		cheatcoin_generate_random_array(buf, sizeof(buf));
		cheatcoin_debug("Seed  : [%s]", cheatcoin_log_array(buf, sizeof(buf)));
		RAND_seed(buf, sizeof(buf));
	}

	group = EC_GROUP_new_by_curve_name(NID_secp256k1);
	if (!group) return -1;
	
	return 0;
}

/* creates a new pair of private and public keys
 * the private key is saved to the 'privkey' array, the public key to the 'pubkey' array,
 * the parity of the public key is saved to the variable 'pubkey_bit'
 * returns a pointer to its internal representation
 */
void *cheatcoin_create_key(cheatcoin_hash_t privkey, cheatcoin_hash_t pubkey, uint8_t *pubkey_bit)
{
	uint8_t buf[sizeof(cheatcoin_hash_t) + 1];
	EC_KEY *eckey = 0;
	const BIGNUM *priv = 0;
	const EC_POINT *pub = 0;
	BN_CTX *ctx = 0;
	int res = -1;

	if (!group) {
		goto fail;
	}

	eckey = EC_KEY_new();
	
	if (!eckey) {
		goto fail;
	}

	if (!EC_KEY_set_group(eckey, group)) {
		goto fail;
	}

	if (!EC_KEY_generate_key(eckey)) {
		goto fail;
	}

	priv = EC_KEY_get0_private_key(eckey);
	if (!priv) {
		goto fail;
	}

	if (BN_bn2bin(priv, (uint8_t*)privkey) != sizeof(cheatcoin_hash_t)) {
		goto fail;
	}

	pub = EC_KEY_get0_public_key(eckey);
	if (!pub) {
		goto fail;
	}

	ctx = BN_CTX_new();
	if (!ctx) {
		goto fail;
	}

	BN_CTX_start(ctx);
	if (EC_POINT_point2oct(group, pub, POINT_CONVERSION_COMPRESSED, buf, sizeof(cheatcoin_hash_t) + 1, ctx) != sizeof(cheatcoin_hash_t) + 1) {
		goto fail;
	}

	memcpy(pubkey, buf + 1, sizeof(cheatcoin_hash_t));
	*pubkey_bit = *buf & 1;
	res = 0;

 fail:
	if (ctx) {
		BN_CTX_free(ctx);
	}

	if (res && eckey) {
		EC_KEY_free(eckey);
	}
	
	return res ? 0 : eckey;
}

// returns the internal representation of the key and the public key by the known private key
void *cheatcoin_private_to_key(const cheatcoin_hash_t privkey, cheatcoin_hash_t pubkey, uint8_t *pubkey_bit)
{
	uint8_t buf[sizeof(cheatcoin_hash_t) + 1];
	EC_KEY *eckey = 0;
	BIGNUM *priv = 0;
	EC_POINT *pub = 0;
	BN_CTX *ctx = 0;
	int res = -1;

	if (!group) {
		goto fail;
	}

	eckey = EC_KEY_new();
	if (!eckey) {
		goto fail;
	}

	if (!EC_KEY_set_group(eckey, group)) {
		goto fail;
	}

	priv = BN_new();
	if (!priv) {
		goto fail;
	}

	//	BN_init(priv);
	BN_bin2bn((uint8_t*)privkey, sizeof(cheatcoin_hash_t), priv);
	EC_KEY_set_private_key(eckey, priv);
	
	ctx = BN_CTX_new();
	if (!ctx) {
		goto fail;
	}

	BN_CTX_start(ctx);

	pub = EC_POINT_new(group);
	if (!pub) {
		goto fail;
	}

	EC_POINT_mul(group, pub, priv, NULL, NULL, ctx);
	EC_KEY_set_public_key(eckey, pub);
	if (EC_POINT_point2oct(group, pub, POINT_CONVERSION_COMPRESSED, buf, sizeof(cheatcoin_hash_t) + 1, ctx) != sizeof(cheatcoin_hash_t) + 1) {
		goto fail;
	}

	memcpy(pubkey, buf + 1, sizeof(cheatcoin_hash_t));
	*pubkey_bit = *buf & 1;
	res = 0;

 fail:
	if (ctx) {
		BN_CTX_free(ctx);
	}

	if (priv) {
		BN_free(priv);
	}

	if (pub) {
		EC_POINT_free(pub);
	}

	if (res && eckey) {
		EC_KEY_free(eckey);
	}

	return res ? 0 : eckey;
}

// Returns the internal representation of the key by the known public key
void *cheatcoin_public_to_key(const cheatcoin_hash_t pubkey, uint8_t pubkey_bit)
{
	EC_KEY *eckey = 0;
	BIGNUM *pub = 0;
	EC_POINT *p = 0;
	BN_CTX *ctx = 0;
	int res = -1;

	if (!group) {
		goto fail;
	}

	eckey = EC_KEY_new();
	if (!eckey) {
		goto fail;
	}

	if (!EC_KEY_set_group(eckey, group)) {
		goto fail;
	}

	pub = BN_new();
	if (!pub) {
		goto fail;
	}

	//	BN_init(pub);
	BN_bin2bn((uint8_t*)pubkey, sizeof(cheatcoin_hash_t), pub);
	p = EC_POINT_new(group);
	if (!p) {
		goto fail;
	}

	ctx = BN_CTX_new();
	if (!ctx) {
		goto fail;
	}

	BN_CTX_start(ctx);
	if (!EC_POINT_set_compressed_coordinates_GFp(group, p, pub, pubkey_bit, ctx)) {
		goto fail;
	}

	EC_KEY_set_public_key(eckey, p);
	res = 0;

 fail:
	if (ctx) {
		BN_CTX_free(ctx);
	}

	if (pub) {
		BN_free(pub);
	}

	if (p) {
		EC_POINT_free(p);
	}

	if (res && eckey) {
		EC_KEY_free(eckey);
	}

	return res ? 0 : eckey;
}

// removes the internal key representation
void cheatcoin_free_key(void *key)
{
	EC_KEY_free((EC_KEY*)key);
}

// sign the hash and put the result in sign_r and sign_s
int cheatcoin_sign(const void *key, const cheatcoin_hash_t hash, cheatcoin_hash_t sign_r, cheatcoin_hash_t sign_s)
{
	uint8_t buf[72], *p;
	unsigned sig_len, s;

	if (!ECDSA_sign(0, (const uint8_t*)hash, sizeof(cheatcoin_hash_t), buf, &sig_len, (EC_KEY*)key)) {
		return -1;
	}

	p = buf + 3, s = *p++;

	if (s >= sizeof(cheatcoin_hash_t)) {
		memcpy(sign_r, p + s - sizeof(cheatcoin_hash_t), sizeof(cheatcoin_hash_t));
	} else  {
		memset(sign_r, 0, sizeof(cheatcoin_hash_t));
		memcpy((uint8_t*)sign_r + sizeof(cheatcoin_hash_t) - s, p, s);
	}

	p += s + 1, s = *p++;

	if (s >= sizeof(cheatcoin_hash_t)) {
		memcpy(sign_s, p + s - sizeof(cheatcoin_hash_t), sizeof(cheatcoin_hash_t));
	} else  {
		memset(sign_s, 0, sizeof(cheatcoin_hash_t));
		memcpy((uint8_t*)sign_s + sizeof(cheatcoin_hash_t) - s, p, s);
	}

	cheatcoin_debug("Sign  : hash=[%s] sign=[%s] r=[%s], s=[%s]", cheatcoin_log_hash(hash),
					cheatcoin_log_array(buf, sig_len), cheatcoin_log_hash(sign_r), cheatcoin_log_hash(sign_s));
	
	return 0;
}

static uint8_t *add_number_to_sign(uint8_t *sign, const cheatcoin_hash_t num)
{
	uint8_t *n = (uint8_t*)num;
	int i, len, leadzero;

	for (i = 0; i < sizeof(cheatcoin_hash_t) && !n[i]; ++i);

	leadzero = (i < sizeof(cheatcoin_hash_t) && n[i] & 0x80);
	len = (sizeof(cheatcoin_hash_t) - i) + leadzero;
	*sign++ = 0x02;
	*sign++ = len;
	
	if (leadzero) {
		*sign++ = 0;
	}

	while (i < sizeof(cheatcoin_hash_t)) {
		*sign++ = n[i++];
	}
	
	return sign;
}

// verify that the signature (sign_r, sign_s) corresponds to a hash 'hash', a version for its own key
// returns 0 on success
int cheatcoin_verify_signature(const void *key, const cheatcoin_hash_t hash, const cheatcoin_hash_t sign_r, const cheatcoin_hash_t sign_s)
{
	uint8_t buf[72], *ptr;
	int res;

	ptr = add_number_to_sign(buf + 2, sign_r);
	ptr = add_number_to_sign(ptr, sign_s);
	buf[0] = 0x30, buf[1] = ptr - buf - 2;
	res = ECDSA_verify(0, (const uint8_t*)hash, sizeof(cheatcoin_hash_t), buf, ptr - buf, (EC_KEY*)key);

	cheatcoin_debug("Verify: res=%2d key=%lx hash=[%s] sign=[%s] r=[%s], s=[%s]", res, (long)key, cheatcoin_log_hash(hash),
					cheatcoin_log_array(buf, ptr - buf), cheatcoin_log_hash(sign_r), cheatcoin_log_hash(sign_s));
	
	return res != 1;
}
