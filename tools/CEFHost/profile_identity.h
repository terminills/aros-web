/* SHA256 core: public-domain LibTomCrypt implementation by Tom St Denis,
 * as retained in AROS WirelessManager src/crypto/sha256-internal.c.
 * Adaptation and profile identity wrapper: AROS Development Team, 2026.
 */
#ifndef CEF_HOST_PROFILE_IDENTITY_H
#define CEF_HOST_PROFILE_IDENTITY_H
#include <stdint.h>
#include <string.h>
struct sha256_state { uint64_t length; uint32_t state[8], curlen; unsigned char buf[64]; };
static uint32_t profile_get_be32(const unsigned char *p)
{ return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]; }
static void profile_put_be32(unsigned char *p, uint32_t v)
{ p[0]=v>>24; p[1]=v>>16; p[2]=v>>8; p[3]=v; }
static void profile_put_be64(unsigned char *p, uint64_t v)
{ profile_put_be32(p, (uint32_t)(v>>32)); profile_put_be32(p+4, (uint32_t)v); }
/* ===== start - public domain SHA256 implementation ===== */

/* This is based on SHA256 implementation in LibTomCrypt that was released into
 * public domain by Tom St Denis. */

/* the K array */
static const unsigned long K[64] = {
	0x428a2f98UL, 0x71374491UL, 0xb5c0fbcfUL, 0xe9b5dba5UL, 0x3956c25bUL,
	0x59f111f1UL, 0x923f82a4UL, 0xab1c5ed5UL, 0xd807aa98UL, 0x12835b01UL,
	0x243185beUL, 0x550c7dc3UL, 0x72be5d74UL, 0x80deb1feUL, 0x9bdc06a7UL,
	0xc19bf174UL, 0xe49b69c1UL, 0xefbe4786UL, 0x0fc19dc6UL, 0x240ca1ccUL,
	0x2de92c6fUL, 0x4a7484aaUL, 0x5cb0a9dcUL, 0x76f988daUL, 0x983e5152UL,
	0xa831c66dUL, 0xb00327c8UL, 0xbf597fc7UL, 0xc6e00bf3UL, 0xd5a79147UL,
	0x06ca6351UL, 0x14292967UL, 0x27b70a85UL, 0x2e1b2138UL, 0x4d2c6dfcUL,
	0x53380d13UL, 0x650a7354UL, 0x766a0abbUL, 0x81c2c92eUL, 0x92722c85UL,
	0xa2bfe8a1UL, 0xa81a664bUL, 0xc24b8b70UL, 0xc76c51a3UL, 0xd192e819UL,
	0xd6990624UL, 0xf40e3585UL, 0x106aa070UL, 0x19a4c116UL, 0x1e376c08UL,
	0x2748774cUL, 0x34b0bcb5UL, 0x391c0cb3UL, 0x4ed8aa4aUL, 0x5b9cca4fUL,
	0x682e6ff3UL, 0x748f82eeUL, 0x78a5636fUL, 0x84c87814UL, 0x8cc70208UL,
	0x90befffaUL, 0xa4506cebUL, 0xbef9a3f7UL, 0xc67178f2UL
};


/* Various logical functions */
#define RORc(x, y) \
( ((((unsigned long) (x) & 0xFFFFFFFFUL) >> (unsigned long) ((y) & 31)) | \
   ((unsigned long) (x) << (unsigned long) (32 - ((y) & 31)))) & 0xFFFFFFFFUL)
#define Ch(x,y,z)       (z ^ (x & (y ^ z)))
#define Maj(x,y,z)      (((x | y) & z) | (x & y))
#define S(x, n)         RORc((x), (n))
#define R(x, n)         (((x)&0xFFFFFFFFUL)>>(n))
#define Sigma0(x)       (S(x, 2) ^ S(x, 13) ^ S(x, 22))
#define Sigma1(x)       (S(x, 6) ^ S(x, 11) ^ S(x, 25))
#define Gamma0(x)       (S(x, 7) ^ S(x, 18) ^ R(x, 3))
#define Gamma1(x)       (S(x, 17) ^ S(x, 19) ^ R(x, 10))

/* compress 512-bits */
static int sha256_compress(struct sha256_state *md, unsigned char *buf)
{
	uint32_t S[8], W[64], t0, t1;
	uint32_t t;
	int i;

	/* copy state into S */
	for (i = 0; i < 8; i++) {
		S[i] = md->state[i];
	}

	/* copy the state into 512-bits into W[0..15] */
	for (i = 0; i < 16; i++)
		W[i] = profile_get_be32(buf + (4 * i));

	/* fill W[16..63] */
	for (i = 16; i < 64; i++) {
		W[i] = Gamma1(W[i - 2]) + W[i - 7] + Gamma0(W[i - 15]) +
			W[i - 16];
	}

	/* Compress */
#define RND(a,b,c,d,e,f,g,h,i)                          \
	t0 = h + Sigma1(e) + Ch(e, f, g) + K[i] + W[i];	\
	t1 = Sigma0(a) + Maj(a, b, c);			\
	d += t0;					\
	h  = t0 + t1;

	for (i = 0; i < 64; ++i) {
		RND(S[0], S[1], S[2], S[3], S[4], S[5], S[6], S[7], i);
		t = S[7]; S[7] = S[6]; S[6] = S[5]; S[5] = S[4];
		S[4] = S[3]; S[3] = S[2]; S[2] = S[1]; S[1] = S[0]; S[0] = t;
	}

	/* feedback */
	for (i = 0; i < 8; i++) {
		md->state[i] = md->state[i] + S[i];
	}
	return 0;
}


/* Initialize the hash state */
static void sha256_init(struct sha256_state *md)
{
	md->curlen = 0;
	md->length = 0;
	md->state[0] = 0x6A09E667UL;
	md->state[1] = 0xBB67AE85UL;
	md->state[2] = 0x3C6EF372UL;
	md->state[3] = 0xA54FF53AUL;
	md->state[4] = 0x510E527FUL;
	md->state[5] = 0x9B05688CUL;
	md->state[6] = 0x1F83D9ABUL;
	md->state[7] = 0x5BE0CD19UL;
}

/**
   Process a block of memory though the hash
   @param md     The hash state
   @param in     The data to hash
   @param inlen  The length of the data (octets)
   @return CRYPT_OK if successful
*/
static int sha256_process(struct sha256_state *md, const unsigned char *in,
			  unsigned long inlen)
{
	unsigned long n;
#define block_size 64

	if (md->curlen > sizeof(md->buf))
		return -1;

	while (inlen > 0) {
		if (md->curlen == 0 && inlen >= block_size) {
			if (sha256_compress(md, (unsigned char *) in) < 0)
				return -1;
			md->length += block_size * 8;
			in += block_size;
			inlen -= block_size;
		} else {
			n = inlen < block_size - md->curlen ? inlen : block_size - md->curlen;
			memcpy(md->buf + md->curlen, in, n);
			md->curlen += n;
			in += n;
			inlen -= n;
			if (md->curlen == block_size) {
				if (sha256_compress(md, md->buf) < 0)
					return -1;
				md->length += 8 * block_size;
				md->curlen = 0;
			}
		}
	}

	return 0;
}


/**
   Terminate the hash to get the digest
   @param md  The hash state
   @param out [out] The destination of the hash (32 bytes)
   @return CRYPT_OK if successful
*/
static int sha256_done(struct sha256_state *md, unsigned char *out)
{
	int i;

	if (md->curlen >= sizeof(md->buf))
		return -1;

	/* increase the length of the message */
	md->length += md->curlen * 8;

	/* append the '1' bit */
	md->buf[md->curlen++] = (unsigned char) 0x80;

	/* if the length is currently above 56 bytes we append zeros
	 * then compress.  Then we can fall back to padding zeros and length
	 * encoding like normal.
	 */
	if (md->curlen > 56) {
		while (md->curlen < 64) {
			md->buf[md->curlen++] = (unsigned char) 0;
		}
		sha256_compress(md, md->buf);
		md->curlen = 0;
	}

	/* pad upto 56 bytes of zeroes */
	while (md->curlen < 56) {
		md->buf[md->curlen++] = (unsigned char) 0;
	}

	/* store length */
	profile_put_be64(md->buf + 56, md->length);
	sha256_compress(md, md->buf);

	/* copy output */
	for (i = 0; i < 8; i++)
		profile_put_be32(out + (4 * i), md->state[i]);

	return 0;
}

/* ===== end - public domain SHA256 implementation ===== */

/* Chrome requires a direct child of root_cache_path. Hash the exact,
 * NUL-delimited names so app/profile boundaries and case remain distinct
 * even on case-insensitive AROS filesystems. The 66-character component
 * avoids embedding or truncating either public name.
 * This is a storage identity, not an access-control boundary.
 */
static void profile_identity(char out[67], const char *app, const char *profile)
{
    struct sha256_state state;
    unsigned char digest[32];
    static const char hex[] = "0123456789abcdef";
    unsigned i;
    sha256_init(&state);
    sha256_process(&state, (const unsigned char *)app, strlen(app) + 1);
    sha256_process(&state, (const unsigned char *)profile, strlen(profile));
    sha256_done(&state, digest);
    out[0] = 'p'; out[1] = '-';
    for (i = 0; i < 32; ++i) {
        out[2 + 2*i] = hex[digest[i] >> 4];
        out[3 + 2*i] = hex[digest[i] & 15];
    }
    out[66] = 0;
}
#undef RORc
#undef Ch
#undef Maj
#undef S
#undef R
#undef Sigma0
#undef Sigma1
#undef Gamma0
#undef Gamma1
#undef RND
#undef block_size
#endif
