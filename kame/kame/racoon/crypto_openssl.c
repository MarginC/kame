/*
 * Copyright (C) 1995, 1996, 1997, and 1998 WIDE Project.
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/* YIPS $Id: crypto_openssl.c,v 1.35 2000/08/30 05:41:31 sakane Exp $ */

#include <sys/types.h>
#include <sys/param.h>

#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <string.h>

/* get openssl/ssleay version number */
#ifdef HAVE_OPENSSL_OPENSSLV_H
# include <openssl/opensslv.h>
#else
# error no opensslv.h found.
#endif

#ifndef OPENSSL_VERSION_NUMBER
#error OPENSSL_VERSION_NUMBER is not defined. OpenSSL0.9.4 or later required.
#endif

#ifdef HAVE_OPENSSL_PEM_H
#include <openssl/pem.h>
#endif
#ifdef HAVE_OPENSSL_EVP_H
#include <openssl/evp.h>
#endif
#ifdef HAVE_OPENSSL_X509_H
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>
#endif
#include <openssl/bn.h>
#include <openssl/dh.h>
#include <openssl/md5.h>
#include <openssl/sha.h>
#include <openssl/des.h>
#include <openssl/crypto.h>
#ifdef HAVE_OPENSSL_IDEA_H
#include <openssl/idea.h>
#endif
#include <openssl/blowfish.h>
#ifdef HAVE_OPENSSL_RC5_H
#include <openssl/rc5.h>
#endif
#include <openssl/cast.h>
#include <openssl/err.h>

#include "var.h"
#include "misc.h"
#include "vmbuf.h"
#include "plog.h"
#include "crypto_openssl.h"
#include "debug.h"

/*
 * I hate to cast every parameter to des_xx into void *, but it is
 * necessary for SSLeay/OpenSSL portability.  It sucks.
 */

#ifdef HAVE_SIGNING_C
static int cb_check_cert __P((int, X509_STORE_CTX *));
static X509 *mem2x509 __P((vchar_t *));

/* X509 Certificate */
/*
 * this functions is derived from apps/verify.c in OpenSSL0.9.5
 */
int
eay_check_x509cert(cert, CApath)
	vchar_t *cert;
	char *CApath;
{
	X509_STORE *cert_ctx = NULL;
	X509_LOOKUP *lookup = NULL;
	X509 *x509 = NULL;
#if OPENSSL_VERSION_NUMBER >= 0x00905100L
	X509_STORE_CTX *csc;
#else
	X509_STORE_CTX csc;
#endif
	int error = -1;

	/* XXX define only functions required. */
#if OPENSSL_VERSION_NUMBER >= 0x00905100L
	OpenSSL_add_all_algorithms();
#else
	SSLeay_add_all_algorithms();
#endif

	cert_ctx = X509_STORE_new();
	if (cert_ctx == NULL)
		goto end;
	X509_STORE_set_verify_cb_func(cert_ctx, cb_check_cert);

	lookup = X509_STORE_add_lookup(cert_ctx, X509_LOOKUP_file());
	if (lookup == NULL)
		goto end;
	X509_LOOKUP_load_file(lookup, NULL, X509_FILETYPE_DEFAULT); /* XXX */

	lookup = X509_STORE_add_lookup(cert_ctx, X509_LOOKUP_hash_dir());
	if (lookup == NULL)
		goto end;
	error = X509_LOOKUP_add_dir(lookup, CApath, X509_FILETYPE_PEM);
	if(!error) {
		error = -1;
		goto end;
	}
	error = -1;	/* initialized */

	/* read the certificate to be verified */
	x509 = mem2x509(cert);
	if (x509 == NULL)
		goto end;

#if OPENSSL_VERSION_NUMBER >= 0x00905100L
	csc = X509_STORE_CTX_new();
	if (csc == NULL)
		goto end;
	X509_STORE_CTX_init(csc, cert_ctx, x509, NULL);
	error = X509_verify_cert(csc);
	X509_STORE_CTX_cleanup(csc);
#else
	X509_STORE_CTX_init(&csc, cert_ctx, x509, NULL);
	error = X509_verify_cert(&csc);
	X509_STORE_CTX_cleanup(&csc);
#endif

	/*
	 * if x509_verify_cert() is successful then the value of error is
	 * set non-zero.
	 */
	error = error ? 0 : -1;

end:
	if (error)
		printf("%s\n", eay_strerror());
	if (cert_ctx != NULL)
		X509_STORE_free(cert_ctx);
	if (x509 != NULL)
		X509_free(x509);

	return(error);
}

/*
 * callback function for verifing certificate.
 * this function is derived from cb() in openssl/apps/s_server.c
 */
static int
cb_check_cert(ok, ctx)
	int ok;
	X509_STORE_CTX *ctx;
{
	char buf[256], *alarm;

	if (!ok) {
		X509_NAME_oneline(
				X509_get_subject_name(ctx->current_cert),
				buf,
				256);
		/*
		 * since we are just checking the certificates, it is
		 * ok if they are self signed. But we should still warn
		 * the user.
 		 */
		switch (ctx->error) {
		case X509_V_ERR_CERT_HAS_EXPIRED:
		case X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT:
#if OPENSSL_VERSION_NUMBER >= 0x00905100L
		case X509_V_ERR_INVALID_CA:
		case X509_V_ERR_PATH_LENGTH_EXCEEDED:
		case X509_V_ERR_INVALID_PURPOSE:
#endif
			ok = 1;
			alarm = "WARNING";
			break;
		default:
			alarm = "ERROR";
		}
#ifndef EAYDEBUG
		plog(logp, LOCATION, NULL,
			"%s: %s(%d) at depth:%d SubjectName:%s\n",
			alarm,
			X509_verify_cert_error_string(ctx->error),
			ctx->error,
			ctx->error_depth,
			buf);
#else
		printf("%s: %s(%d) at depth:%d SubjectName:%s\n",
			alarm,
			X509_verify_cert_error_string(ctx->error),
			ctx->error,
			ctx->error_depth,
			buf);
#endif
	}
	ERR_clear_error();

	return ok;
}

/*
 * get a subjectAltName from X509 certificate.
 */
vchar_t *
eay_get_x509asn1subjectname(cert)
	vchar_t *cert;
{
	X509 *x509 = NULL;
	u_char *bp;
	vchar_t *name = NULL;
	int len;
	int error = -1;

	bp = cert->v;

	x509 = mem2x509(cert);
	if (x509 == NULL)
		goto end;

	/* get the length of the name */
	len = i2d_X509_NAME(x509->cert_info->subject, NULL);
	name = vmalloc(len);
	if (!name)
		goto end;
	/* get the name */
	bp = name->v;
	len = i2d_X509_NAME(x509->cert_info->subject, &bp);

	error = 0;

   end:
	if (error) {
		if (name) {
			vfree(name);
			name = NULL;
		}
#ifndef EAYDEBUG
		plog(logp, LOCATION, NULL, "%s\n", eay_strerror());
#else
		printf("%s\n", eay_strerror());
#endif
	}
	if (x509)
		X509_free(x509);

	return name;
}

/*
 * get a subjectAltName from X509 certificate.
 */
#include <openssl/x509v3.h>
vchar_t *
eay_get_x509subjectaltname(cert)
	vchar_t *cert;
{
	X509 *x509 = NULL;
	X509_EXTENSION *ext;
	X509V3_EXT_METHOD *method = NULL;
	vchar_t *altname = NULL;
	u_char *bp;
	int i;
	int error = -1;

	bp = cert->v;

	x509 = mem2x509(cert);
	if (x509 == NULL)
		goto end;

	i = X509_get_ext_by_NID(x509, NID_subject_alt_name, -1);
	if (i < 0)
		goto end;
	ext = X509_get_ext(x509, i);
	method = X509V3_EXT_get(ext);
	if(!method)
		goto end;
	
	if(method->i2s) {

		char *str = NULL;

		str = method->i2s(method, ext);
		if(!str)
			goto end;
		altname = vmalloc(strlen(str));
		if (!altname) {
			free(str);
			goto end;
		}
		memcpy(altname->v, str, altname->l);
		free(str);

	} else {

		STACK_OF(GENERAL_NAME) *name;
		CONF_VALUE *cval = NULL;
		STACK_OF(CONF_VALUE) *nval = NULL;

		bp = ext->value->data;
		name = method->d2i(NULL, &bp, ext->value->length);
		if(!name)
			goto end;

		nval = method->i2v(method, name, NULL);
		method->ext_free(name);
		name = NULL;
		if(!nval)
			goto end;

		/*
		 * XXX What is the subjectAltName which the function should
		 * return if CERT has multiple subjectlatnames ?
		 * At this moment, the function only returns 1st subjectaltname.
		 *
		 * #define MULTI_SUBJECTALTNAME
		 */
	    {
		int i = 0, tlen = 0, len = 0;
#ifdef MULTI_SUBJECTALTNAME
		for(i = 0; i < sk_CONF_VALUE_num(nval); i++) {
#endif
			cval = sk_CONF_VALUE_value(nval, i);
			len = strlen(cval->value);
			altname = vrealloc(altname, tlen + len);
			if (!altname) {
				sk_CONF_VALUE_pop_free(nval, X509V3_conf_free);
				method->ext_free(name);
				goto end;
			}
			memcpy(altname->v + tlen, cval->value, len);
#ifdef MULTI_SUBJECTALTNAME
			tlen += len;
		}
#endif
	    }
		sk_CONF_VALUE_pop_free(nval, X509V3_conf_free);
	}

	error = 0;

   end:
	if (error) {
		if (altname) {
			vfree(altname);
			altname = NULL;
		}
#ifndef EAYDEBUG
		plog(logp, LOCATION, NULL, "%s\n", eay_strerror());
#else
		printf("%s\n", eay_strerror());
#endif
	}
	if (x509)
		X509_free(x509);

	return altname;
}

/*
 * decode a X509 certificate and make a readable text terminated '\n'.
 * return the buffer allocated, so must free it later.
 */
char *
eay_get_x509text(cert)
	vchar_t *cert;
{
	X509 *x509 = NULL;
	BIO *bio = NULL;
	char *text = NULL;
	u_char *bp = NULL;
	int len = 0;
	int error = -1;

	x509 = mem2x509(cert);
	if (x509 == NULL)
		goto end;

	bio = BIO_new(BIO_s_mem());
	if (bio == NULL)
		goto end;

	error = X509_print(bio, x509);
	if (error != 1) {
		error = -1;
		goto end;
	}

	len = BIO_get_mem_data(bio, &bp);
	text = malloc(len);
	if (text == NULL)
		goto end;
	memcpy(text, bp, len);

	error = 0;

    end:
	if (error) {
		if (text) {
			free(text);
			text = NULL;
		}
#ifndef EAYDEBUG
		plog(logp, LOCATION, NULL, "%s\n", eay_strerror());
#else
		printf("%s\n", eay_strerror());
#endif
	}
	if (bio)
		BIO_free(bio);
	if (x509)
		X509_free(x509);

	return text;
}

/* get X509 structure from buffer. */
static X509 *
mem2x509(cert)
	vchar_t *cert;
{
	X509 *x509;

#ifndef EAYDEBUG
    {
	u_char *bp;

	bp = cert->v;

	x509 = d2i_X509(NULL, &bp, cert->l);
    }
#else
    {
	BIO *bio;
	int len;

	bio = BIO_new(BIO_s_mem());
	if (bio == NULL)
		return NULL;
	len = BIO_write(bio, cert->v, cert->l);
	if (len == -1)
		return NULL;
	x509 = PEM_read_bio_X509(bio, NULL, NULL, NULL);
	BIO_free(bio);
    }
#endif
	return x509;
}

/*
 * get a X509 certificate from local file.
 * a certificate must be PEM format.
 * Input:
 *	path to a certificate.
 * Output:
 *	NULL if error occured
 *	other is the cert.
 */
vchar_t *
eay_get_x509cert(path)
	char *path;
{
	FILE *fp;
	X509 *x509;
	vchar_t *cert;
	u_char *bp;
	int len;
	int error;

	/* Read private key */
	fp = fopen(path, "r");
	if (fp == NULL)
		return NULL;
#if OPENSSL_VERSION_NUMBER >= 0x00904100L
	x509 = PEM_read_X509(fp, NULL, NULL, NULL);
#else
	x509 = PEM_read_X509(fp, NULL, NULL);
#endif
	fclose (fp);

	if (x509 == NULL)
		return NULL;

	len = i2d_X509(x509, NULL);
	cert = vmalloc(len);
	if (cert == NULL) {
		X509_free(x509);
		return NULL;
	}
	bp = cert->v;
	error = i2d_X509(x509, &bp);
	X509_free(x509);

	if (error == 0)
		return NULL;

	return cert;
}

/*
 * sign a souce by X509 signature.
 * XXX: to be get hash type from my cert ?
 *	to be handled EVP_dss().
 */
vchar_t *
eay_get_x509sign(source, privkey, cert)
	vchar_t *source;
	vchar_t *privkey;
	vchar_t *cert;
{
	EVP_PKEY *evp;
	u_char *bp;
	vchar_t *sig = NULL;
	int len;

	bp = privkey->v;

	/* XXX to be handled EVP_PKEY_DSA */
	evp = d2i_PrivateKey(EVP_PKEY_RSA, NULL, &bp, privkey->l);
	if (evp == NULL)
		return NULL;

	/* XXX: to be handled EVP_dss() */
	/* XXX: Where can I get such parameters ?  From my cert ? */

	len = RSA_size(evp->pkey.rsa);

	sig = vmalloc(len);
	if (sig == NULL)
		return NULL;

	len = RSA_private_encrypt(source->l, source->v, sig->v,
				evp->pkey.rsa, RSA_PKCS1_PADDING);
	EVP_PKEY_free(evp);
	if (len == 0 || len != sig->l) {
		vfree(sig);
		sig = NULL;
	}

	return sig;
}

/*
 * check a X509 signature
 *	XXX: to be get hash type from my cert ?
 *		to be handled EVP_dss().
 * OUT: return -1 when error.
 *	0
 */
int
eay_check_x509sign(source, sig, cert)
	vchar_t *source;
	vchar_t *sig;
	vchar_t *cert;
{
	X509 *x509;
	EVP_PKEY *evp;
	u_char *bp;
	vchar_t *xbuf = NULL;
	int error, len;

	bp = cert->v;

	x509 = d2i_X509(NULL, &bp, cert->l);
	if (x509 == NULL)
		return -1;

	evp = X509_get_pubkey(x509);
	X509_free(x509);
	if (evp == NULL)
		return -1;

	/* Verify the signature */
	/* XXX: to be handled EVP_dss() */

	len = RSA_size(evp->pkey.rsa);

	xbuf = vmalloc(len);
	if (xbuf == NULL) {
		EVP_PKEY_free(evp);
		return -1;
	}

	len = RSA_public_decrypt(sig->l, sig->v, xbuf->v,
				evp->pkey.rsa, RSA_PKCS1_PADDING);
	EVP_PKEY_free(evp);
	if (len == 0 || len != source->l) {
		vfree(xbuf);
		return -1;
	}

	error = memcmp(source->v, xbuf->v, source->l);
	vfree(xbuf);
	if (error != 0)
		return -1;

	return 0;
}

/*
 * check a signature by signed with PKCS7 certificate.
 *	XXX: to be get hash type from my cert ?
 *		to be handled EVP_dss().
 * OUT: return -1 when error.
 *	0
 */
int
eay_check_pkcs7sign(source, sig, cert)
	vchar_t *source;
	vchar_t *sig;
	vchar_t *cert;
{
	X509 *x509;
	EVP_MD_CTX md_ctx;
	EVP_PKEY *evp;
	int error;
	BIO *bio = BIO_new(BIO_s_mem());
	char *bp;

	if (bio == NULL)
		return -1;
	error = BIO_write(bio, cert->v, cert->l);
	if (error != cert->l)
		return -1;

	bp = cert->v;
	x509 = PEM_read_bio_X509(bio, NULL, NULL, NULL);
	BIO_free(bio);
	if (x509 == NULL)
		return -1;

	evp = X509_get_pubkey(x509);
	X509_free(x509);
	if (evp == NULL)
		return -1;

	/* Verify the signature */
	/* XXX: to be handled EVP_dss() */
	EVP_VerifyInit(&md_ctx, EVP_sha1());
	EVP_VerifyUpdate(&md_ctx, source->v, source->l);
	error = EVP_VerifyFinal(&md_ctx, sig->v, sig->l, evp);

	EVP_PKEY_free(evp);

	if (error != 1)
		return -1;

	return 0;
}

/*
 * get PKCS#1 Private Key of PEM format from local file.
 */
vchar_t *
eay_get_pkcs1privkey(path)
	char *path;
{
	FILE *fp;
	EVP_PKEY *evp = NULL;
	vchar_t *pkey = NULL;
	u_char *bp;
	int pkeylen;
	int error = -1;

	/* Read private key */
	fp = fopen(path, "r");
	if (fp == NULL)
		return NULL;

#if OPENSSL_VERSION_NUMBER >= 0x00904100L
	evp = PEM_read_PrivateKey(fp, NULL, NULL, NULL);
#else
	evp = PEM_read_PrivateKey(fp, NULL, NULL);
#endif
	fclose (fp);

	if (evp == NULL)
		return NULL;

	pkeylen = i2d_PrivateKey(evp, NULL);
	if (pkeylen == 0)
		goto end;
	pkey = vmalloc(pkeylen);
	if (pkey == NULL)
		goto end;
	bp = pkey->v;
	pkeylen = i2d_PrivateKey(evp, &bp);
	if (pkeylen == 0)
		goto end;

	error = 0;

end:
	if (evp != NULL)
		EVP_PKEY_free(evp);
	if (error != 0 && pkey != NULL) {
		vfree(pkey);
		pkey = NULL;
	}

	return pkey;
}

/*
 * get PKCS#1 Public Key of PEM format from local file.
 */
vchar_t *
eay_get_pkcs1pubkey(path)
	char *path;
{
	FILE *fp;
	EVP_PKEY *evp = NULL;
	vchar_t *pkey = NULL;
	X509 *x509 = NULL;
	u_char *bp;
	int pkeylen;
	int error = -1;

	/* Read private key */
	fp = fopen(path, "r");
	if (fp == NULL)
		return NULL;

#if OPENSSL_VERSION_NUMBER >= 0x00904100L
	x509 = PEM_read_X509(fp, NULL, NULL, NULL);
#else
	x509 = PEM_read_X509(fp, NULL, NULL);
#endif
	fclose (fp);

	if (x509 == NULL)
		return NULL;
  
	/* Get public key - eay */
	evp = X509_get_pubkey(x509);
	if (evp == NULL)
		return NULL;

	pkeylen = i2d_PublicKey(evp, NULL);
	if (pkeylen == 0)
		goto end;
	pkey = vmalloc(pkeylen);
	if (pkey == NULL)
		goto end;
	bp = pkey->v;
	pkeylen = i2d_PublicKey(evp, &bp);
	if (pkeylen == 0)
		goto end;

	error = 0;
end:
	if (evp != NULL)
		EVP_PKEY_free(evp);
	if (error != 0 && pkey != NULL) {
		vfree(pkey);
		pkey = NULL;
	}

	return pkey;
}
#endif
  
/*
 * get error string
 * MUST load ERR_load_crypto_strings() first.
 */
char *
eay_strerror()
{
	static char ebuf[512];
	int len = 0;
	unsigned long l;
	char buf[200];
#if OPENSSL_VERSION_NUMBER >= 0x00904100L
	const char *file, *data;
#else
	char *file, *data;
#endif
	int line, flags;
	unsigned long es;

	es = CRYPTO_thread_id();

	while ((l = ERR_get_error_line_data(&file, &line, &data, &flags)) != 0){
		len += snprintf(ebuf + len, sizeof(ebuf) - len,
				"%lu:%s:%s:%d:%s ",
				es, ERR_error_string(l, buf), file, line,
				(flags & ERR_TXT_STRING) ? data : "");
		if (sizeof(ebuf) < len)
			break;
	}

	return ebuf;
}

void
eay_init_error()
{
	ERR_load_crypto_strings();
}

/*
 * DES-CBC
 */
vchar_t *
eay_des_encrypt(data, key, iv)
	vchar_t *data, *key;
	caddr_t iv;
{
	vchar_t *res;
	des_key_schedule ks;

	if (des_key_sched((void *)key->v, ks) != 0)
		return NULL;

	/* allocate buffer for result */
	if ((res = vmalloc(data->l)) == NULL)
		return NULL;

	/* decryption data */
	des_cbc_encrypt((void *)data->v, (void *)res->v, data->l,
	                ks, (void *)iv, DES_ENCRYPT);

	return res;
}

vchar_t *
eay_des_decrypt(data, key, iv)
	vchar_t *data, *key;
	caddr_t iv;
{
	vchar_t *res;
	des_key_schedule ks;

	if (des_key_sched((void *)key->v, ks) != 0)
		return NULL;

	/* allocate buffer for result */
	if ((res = vmalloc(data->l)) == NULL)
		return NULL;

	/* decryption data */
	des_cbc_encrypt((void *)data->v, (void *)res->v, data->l,
	                ks, (void *)iv, DES_DECRYPT);

	return res;
}

int
eay_des_weakkey(key)
	vchar_t *key;
{
	return des_is_weak_key((void *)key->v);
}

#ifdef HAVE_IDEA_H
/*
 * IDEA-CBC
 */
vchar_t *
eay_idea_encrypt(data, key, iv)
	vchar_t *data, *key;
	caddr_t iv;
{
	vchar_t *res;
	IDEA_KEY_SCHEDULE ks;

	idea_set_encrypt_key(key->v, &ks);

	/* allocate buffer for result */
	if ((res = vmalloc(data->l)) == NULL)
		return NULL;

	/* decryption data */
	idea_cbc_encrypt(data->v, res->v, data->l,
	                &ks, iv, IDEA_ENCRYPT);

	return res;
}

vchar_t *
eay_idea_decrypt(data, key, iv)
	vchar_t *data, *key;
	caddr_t iv;
{
	vchar_t *res;
	IDEA_KEY_SCHEDULE ks, dks;

	idea_set_encrypt_key(key->v, &ks);
	idea_set_decrypt_key(&ks, &dks);

	/* allocate buffer for result */
	if ((res = vmalloc(data->l)) == NULL)
		return NULL;

	/* decryption data */
	idea_cbc_encrypt(data->v, res->v, data->l,
	                &dks, iv, IDEA_DECRYPT);

	return res;
}

int
eay_idea_weakkey(key)
	vchar_t *key;
{
	return 0;	/* XXX */
}
#endif

/*
 * BLOWFISH-CBC
 */
vchar_t *
eay_bf_encrypt(data, key, iv)
	vchar_t *data, *key;
	caddr_t iv;
{
	vchar_t *res;
	BF_KEY ks;

	BF_set_key(&ks, key->l, key->v);

	/* allocate buffer for result */
	if ((res = vmalloc(data->l)) == NULL)
		return NULL;

	/* decryption data */
	BF_cbc_encrypt(data->v, res->v, data->l,
		&ks, iv, BF_ENCRYPT);

	return res;
}

vchar_t *
eay_bf_decrypt(data, key, iv)
	vchar_t *data, *key;
	caddr_t iv;
{
	vchar_t *res;
	BF_KEY ks;

	BF_set_key(&ks, key->l, key->v);

	/* allocate buffer for result */
	if ((res = vmalloc(data->l)) == NULL)
		return NULL;

	/* decryption data */
	BF_cbc_encrypt(data->v, res->v, data->l,
		&ks, iv, BF_DECRYPT);

	return res;
}

int
eay_bf_weakkey(key)
	vchar_t *key;
{
	return 0;	/* XXX to be done. refer to RFC 2451 */
}

#ifdef HAVE_RC5_H
/*
 * RC5-CBC
 */
vchar_t *
eay_rc5_encrypt(data, key, iv)
	vchar_t *data, *key;
	caddr_t iv;
{
	vchar_t *res;
	RC5_32_KEY ks;

	/* in RFC 2451, there is information about the number of round. */
	RC5_32_set_key(&ks, key->l, key->v, 16);

	/* allocate buffer for result */
	if ((res = vmalloc(data->l)) == NULL)
		return NULL;

	/* decryption data */
	RC5_32_cbc_encrypt(data->v, res->v, data->l,
		&ks, iv, RC5_ENCRYPT);

	return res;
}

vchar_t *
eay_rc5_decrypt(data, key, iv)
	vchar_t *data, *key;
	caddr_t iv;
{
	vchar_t *res;
	RC5_32_KEY ks;

	/* in RFC 2451, there is information about the number of round. */
	RC5_32_set_key(&ks, key->l, key->v, 16);

	/* allocate buffer for result */
	if ((res = vmalloc(data->l)) == NULL)
		return NULL;

	/* decryption data */
	RC5_32_cbc_encrypt(data->v, res->v, data->l,
		&ks, iv, RC5_DECRYPT);

	return res;
}

int
eay_rc5_weakkey(key)
	vchar_t *key;
{
	return 0;	/* No known weak keys when used with 16 rounds. */

}
#endif

/*
 * 3DES-CBC
 */
vchar_t *
eay_3des_encrypt(data, key, iv)
	vchar_t *data, *key;
	caddr_t iv;
{
	vchar_t *res;
	des_key_schedule ks1, ks2, ks3;

	if (key->l < 24)
		return NULL;

	if (des_key_sched((void *)key->v, ks1) != 0)
		return NULL;
	if (des_key_sched((void *)(key->v + 8), ks2) != 0)
		return NULL;
	if (des_key_sched((void *)(key->v + 16), ks3) != 0)
		return NULL;

	/* allocate buffer for result */
	if ((res = vmalloc(data->l)) == NULL)
		return NULL;

	/* decryption data */
	des_ede3_cbc_encrypt((void *)data->v, (void *)res->v, data->l,
	                ks1, ks2, ks3, (void *)iv, DES_ENCRYPT);

	return res;
}

vchar_t *
eay_3des_decrypt(data, key, iv)
	vchar_t *data, *key;
	caddr_t iv;
{
	vchar_t *res;
	des_key_schedule ks1, ks2, ks3;

	if (key->l < 24)
		return NULL;

	if (des_key_sched((void *)key->v, ks1) != 0)
		return NULL;
	if (des_key_sched((void *)(key->v + 8), ks2) != 0)
		return NULL;
	if (des_key_sched((void *)(key->v + 16), ks3) != 0)
		return NULL;

	/* allocate buffer for result */
	if ((res = vmalloc(data->l)) == NULL)
		return NULL;

	/* decryption data */
	des_ede3_cbc_encrypt((void *)data->v, (void *)res->v, data->l,
	                ks1, ks2, ks3, (void *)iv, DES_DECRYPT);

	return res;
}

int
eay_3des_weakkey(key)
	vchar_t *key;
{
	if (key->l < 24)
		return NULL;

	return (des_is_weak_key((void *)key->v)
		|| des_is_weak_key((void *)(key->v + 8))
		|| des_is_weak_key((void *)(key->v + 16)));
}

/*
 * CAST-CBC
 */
vchar_t *
eay_cast_encrypt(data, key, iv)
	vchar_t *data, *key;
	caddr_t iv;
{
	vchar_t *res;
	CAST_KEY ks;

	CAST_set_key(&ks, key->l, key->v);

	/* allocate buffer for result */
	if ((res = vmalloc(data->l)) == NULL)
		return NULL;

	/* decryption data */
	CAST_cbc_encrypt(data->v, res->v, data->l,
	                &ks, iv, DES_ENCRYPT);

	return res;
}

vchar_t *
eay_cast_decrypt(data, key, iv)
	vchar_t *data, *key;
	caddr_t iv;
{
	vchar_t *res;
	CAST_KEY ks;

	CAST_set_key(&ks, key->l, key->v);

	/* allocate buffer for result */
	if ((res = vmalloc(data->l)) == NULL)
		return NULL;

	/* decryption data */
	CAST_cbc_encrypt(data->v, res->v, data->l,
	                &ks, iv, DES_DECRYPT);

	return res;
}

int
eay_cast_weakkey(key)
	vchar_t *key;
{
	return 0;	/* No known weak keys. */
}

/*
 * HMAC-SHA1
 */
vchar_t *
eay_hmacsha1_oneX(key, data, data2)
	vchar_t *key, *data, *data2;
{
	vchar_t *res;
	SHA_CTX c;
	u_char k_ipad[65], k_opad[65];
	u_char *nkey;
	int nkeylen;
	int i;
	u_char tk[SHA_DIGEST_LENGTH];

	/* initialize */
	if ((res = vmalloc(SHA_DIGEST_LENGTH)) == 0)
		return(0);

	/* if key is longer than 64 bytes reset it to key=SHA1(key) */
	nkey = key->v;
	nkeylen = key->l;

	if (nkeylen > 64) {
		SHA_CTX      ctx;

		SHA1_Init(&ctx);
		SHA1_Update(&ctx, nkey, nkeylen);
		SHA1_Final(tk, &ctx);

		nkey = tk;
		nkeylen = SHA_DIGEST_LENGTH;
	}

	/* start out by string key in pads */
	memset(k_ipad, 0, sizeof(k_ipad));
	memset(k_opad, 0, sizeof(k_opad));
	memcpy(k_ipad, nkey, nkeylen);
	memcpy(k_opad, nkey, nkeylen);

	/* XOR key with ipad and opad values */
	for (i=0; i<64; i++) {
		k_ipad[i] ^= 0x36;
		k_opad[i] ^= 0x5c;
	}

	/* key */
	SHA1_Init(&c);
	SHA1_Update(&c, k_ipad, 64);

	/* finish up 1st pass */
	SHA1_Update(&c, data->v, data->l);
	SHA1_Update(&c, data2->v, data2->l);
	SHA1_Final(res->v, &c);

	/* perform outer SHA1 */
	SHA1_Init(&c);
	SHA1_Update(&c, k_opad, 64);
	SHA1_Update(&c, res->v, SHA_DIGEST_LENGTH);
	SHA1_Final(res->v, &c);

	return(res);
};

/*
 * HMAC SHA1
 */
vchar_t *
eay_hmacsha1_one(key, data)
	vchar_t *key, *data;
{
	vchar_t *res;
	SHA_CTX c;
	u_char k_ipad[65], k_opad[65];
	u_char *nkey;
	int nkeylen;
	int i;
	u_char tk[SHA_DIGEST_LENGTH];

	/* initialize */
	if ((res = vmalloc(SHA_DIGEST_LENGTH)) == 0)
		return(0);

	/* if key is longer than 64 bytes reset it to key=SHA1(key) */
	nkey = key->v;
	nkeylen = key->l;

	if (nkeylen > 64) {
		SHA_CTX      ctx;

		SHA1_Init(&ctx);
		SHA1_Update(&ctx, nkey, nkeylen);
		SHA1_Final(tk, &ctx);

		nkey = tk;
		nkeylen = SHA_DIGEST_LENGTH;
	}

	/* start out by string key in pads */
	memset(k_ipad, 0, sizeof(k_ipad));
	memset(k_opad, 0, sizeof(k_opad));
	memcpy(k_ipad, nkey, nkeylen);
	memcpy(k_opad, nkey, nkeylen);

	/* XOR key with ipad and opad values */
	for (i=0; i<64; i++) {
		k_ipad[i] ^= 0x36;
		k_opad[i] ^= 0x5c;
	}

	/* key */
	SHA1_Init(&c);
	SHA1_Update(&c, k_ipad, 64);

	/* finish up 1st pass */
	SHA1_Update(&c, data->v, data->l);
	SHA1_Final(res->v, &c);

	/* perform outer SHA1 */
	SHA1_Init(&c);
	SHA1_Update(&c, k_opad, 64);
	SHA1_Update(&c, res->v, SHA_DIGEST_LENGTH);
	SHA1_Final(res->v, &c);

	return(res);
};

/*
 * HMAC MD5
 */
vchar_t *
eay_hmacmd5_one(key, data)
	vchar_t *key, *data;
{
	vchar_t *res;
	MD5_CTX c;
	u_char k_ipad[65], k_opad[65];
	u_char *nkey;
	int nkeylen;
	int i;
	u_char tk[MD5_DIGEST_LENGTH];

	/* initialize */
	if ((res = vmalloc(MD5_DIGEST_LENGTH)) == 0)
		return(0);

	/* if key is longer than 64 bytes reset it to key=MD5(key) */
	nkey = key->v;
	nkeylen = key->l;

	if (nkeylen > 64) {
		MD5_CTX      ctx;

		MD5_Init(&ctx);
		MD5_Update(&ctx, nkey, nkeylen);
		MD5_Final(tk, &ctx);

		nkey = tk;
		nkeylen = MD5_DIGEST_LENGTH;
	}

	/* start out by string key in pads */
	memset(k_ipad, 0, sizeof(k_ipad));
	memset(k_opad, 0, sizeof(k_opad));
	memcpy(k_ipad, nkey, nkeylen);
	memcpy(k_opad, nkey, nkeylen);

	/* XOR key with ipad and opad values */
	for (i=0; i<64; i++) {
		k_ipad[i] ^= 0x36;
		k_opad[i] ^= 0x5c;
	}

	/* key */
	MD5_Init(&c);
	MD5_Update(&c, k_ipad, 64);

	/* finish up 1st pass */
	MD5_Update(&c, data->v, data->l);
	MD5_Final(res->v, &c);

	/* perform outer MD5 */
	MD5_Init(&c);
	MD5_Update(&c, k_opad, 64);
	MD5_Update(&c, res->v, MD5_DIGEST_LENGTH);
	MD5_Final(res->v, &c);

	return(res);
};

/*
 * SHA functions
 */
caddr_t
eay_sha1_init()
{
	SHA_CTX *c = malloc(sizeof(*c));

	SHA1_Init(c);

	return((caddr_t)c);
}

void
eay_sha1_update(c, data)
	caddr_t c;
	vchar_t *data;
{
	SHA1_Update((SHA_CTX *)c, data->v, data->l);

	return;
}

vchar_t *
eay_sha1_final(c)
	caddr_t c;
{
	vchar_t *res;

	if ((res = vmalloc(SHA_DIGEST_LENGTH)) == 0)
		return(0);

	SHA1_Final(res->v, (SHA_CTX *)c);
	(void)free(c);

	return(res);
}

vchar_t *
eay_sha1_one(data)
	vchar_t *data;
{
	caddr_t ctx;
	vchar_t *res;

	ctx = eay_sha1_init();
	eay_sha1_update(ctx, data);
	res = eay_sha1_final(ctx);

	return(res);
}

/*
 * MD5 functions
 */
caddr_t
eay_md5_init()
{
	MD5_CTX *c = malloc(sizeof(*c));

	MD5_Init(c);

	return((caddr_t)c);
}

void
eay_md5_update(c, data)
	caddr_t c;
	vchar_t *data;
{
	MD5_Update((MD5_CTX *)c, data->v, data->l);

	return;
}

vchar_t *
eay_md5_final(c)
	caddr_t c;
{
	vchar_t *res;

	if ((res = vmalloc(MD5_DIGEST_LENGTH)) == 0)
		return(0);

	MD5_Final(res->v, (MD5_CTX *)c);
	(void)free(c);

	return(res);
}

vchar_t *
eay_md5_one(data)
	vchar_t *data;
{
	caddr_t ctx;
	vchar_t *res;

	ctx = eay_md5_init();
	eay_md5_update(ctx, data);
	res = eay_md5_final(ctx);

	return(res);
}

/*
 * eay_set_random
 *   size: number of bytes.
 */
vchar_t *
eay_set_random(size)
	u_int32_t size;
{
	BIGNUM *r = NULL;
	vchar_t *res = 0;

	if ((r = BN_new()) == NULL)
		goto end;
	BN_rand(r, size * 8, 0, 0);
	eay_bn2v(&res, r);

end:
	if (r)
		BN_free(r);
	return(res);
}

/* DH */
int
eay_dh_generate(prime, g, publen, pub, priv)
	vchar_t *prime, **pub, **priv;
	u_int publen;
	u_int32_t g;
{
	BIGNUM *p = NULL;
	DH *dh = NULL;
	int error = -1;

	/* initialize */
	/* pre-process to generate number */
	if (eay_v2bn(&p, prime) < 0)
		goto end;

	if ((dh = DH_new()) == NULL)
		goto end;
	dh->p = p;
	p = NULL;	/* p is now part of dh structure */
	dh->g = NULL;
	if ((dh->g = BN_new()) == NULL)
		goto end;
	if (!BN_set_word(dh->g, g))
		goto end;

	if (publen != 0)
		dh->length = publen;

	/* generate public and private number */
	if (!DH_generate_key(dh))
		goto end;

	/* copy results to buffers */
	if (eay_bn2v(pub, dh->pub_key) < 0)
		goto end;
	if (eay_bn2v(priv, dh->priv_key) < 0) {
		vfree(*pub);
		goto end;
	}

	error = 0;

end:
	if (dh != NULL)
		DH_free(dh);
	if (p != 0)
		BN_free(p);
	return(error);
}

int
eay_dh_compute(prime, g, pub, priv, pub2, key)
	vchar_t *prime, *pub, *priv, *pub2, **key;
	u_int32_t g;
{
	BIGNUM *dh_pub = NULL;
	DH *dh = NULL;
#if 0
	vchar_t *gv = 0;
#endif
	int error = -1;

	/* make public number to compute */
	if (eay_v2bn(&dh_pub, pub2) < 0)
		goto end;

	/* make DH structure */
	if ((dh = DH_new()) == NULL)
		goto end;
	if (eay_v2bn(&dh->p, prime) < 0)
		goto end;
	if (eay_v2bn(&dh->pub_key, pub) < 0)
		goto end;
	if (eay_v2bn(&dh->priv_key, priv) < 0)
		goto end;
	dh->length = pub2->l * 8;

#if 1
	dh->g = NULL;
	if ((dh->g = BN_new()) == NULL)
		goto end;
	if (!BN_set_word(dh->g, g))
		goto end;
#else
	if ((gv = vmalloc(sizeof(g))) == 0)
		goto end;
	memcpy(gv->v, (caddr_t)&g, sizeof(g));
	if (eay_v2bn(&dh->g, gv) < 0)
		goto end;
#endif

	DH_compute_key((*key)->v, dh_pub, dh);

	error = 0;

end:
#if 0
	if (gv) vfree(gv);
#endif
	if (dh_pub != NULL)
		BN_free(dh_pub);
	if (dh != NULL)
		DH_free(dh);
	return(error);
}

#if 1
int
eay_v2bn(bn, var)
	BIGNUM **bn;
	vchar_t *var;
{
	if ((*bn = BN_bin2bn(var->v, var->l, NULL)) == NULL)
		return -1;

	return 0;
}
#else
/*
 * convert vchar_t <-> BIGNUM.
 *
 * vchar_t: unit is u_char, network endian, most significant byte first.
 * BIGNUM: unit is BN_ULONG, each of BN_ULONG is in host endian,
 *	least significant BN_ULONG must come first.
 *
 * hex value of "0x3ffe050104" is represented as follows:
 *	vchar_t: 3f fe 05 01 04
 *	BIGNUM (BN_ULONG = u_int8_t): 04 01 05 fe 3f
 *	BIGNUM (BN_ULONG = u_int16_t): 0x0104 0xfe05 0x003f
 *	BIGNUM (BN_ULONG = u_int32_t_t): 0xfe050104 0x0000003f
 */
int
eay_v2bn(bn, var)
	BIGNUM **bn;
	vchar_t *var;
{
	u_char *p;
	u_char *q;
	BN_ULONG *r;
	int l;
	BN_ULONG num;

	*bn = BN_new();
	if (*bn == NULL)
		goto err;
	l = (var->l * 8 + BN_BITS2 - 1) / BN_BITS2;
	if (bn_expand(*bn, l * BN_BITS2) == NULL)
		goto err;
	(*bn)->top = l;

	/* scan from least significant byte */
	p = (u_char *)var->v;
	q = (u_char *)(var->v + var->l);
	r = (*bn)->d;
	num = 0;
	l = 0;
	do {
		q--;
		num = num | ((BN_ULONG)*q << (l++ * 8));
		if (l == BN_BYTES) {
			*r++ = num;
			num = 0;
			l = 0;
		}
	} while (p < q);
	if (l)
		*r = num;
	return 0;

err:
	if (*bn)
		BN_free(*bn);
	return -1;
}
#endif

int
eay_bn2v(var, bn)
	vchar_t **var;
	BIGNUM *bn;
{
	*var = vmalloc(bn->top * BN_BYTES);
	if (*var == NULL)
		return(-1);

	(*var)->l = BN_bn2bin(bn, (*var)->v);

	return 0;
}
