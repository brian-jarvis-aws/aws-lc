/* Written by Dr Stephen N Henson (steve@openssl.org) for the OpenSSL
 * project 2007.
 */
/* ====================================================================
 * Copyright (c) 2007 The OpenSSL Project.  All rights reserved.
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
 *    for use in the OpenSSL Toolkit. (http://www.OpenSSL.org/)"
 *
 * 4. The names "OpenSSL Toolkit" and "OpenSSL Project" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For written permission, please contact
 *    licensing@OpenSSL.org.
 *
 * 5. Products derived from this software may not be called "OpenSSL"
 *    nor may "OpenSSL" appear in their names without prior written
 *    permission of the OpenSSL Project.
 *
 * 6. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit (http://www.OpenSSL.org/)"
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

#include <openssl/digest.h>
#include <openssl/evp.h>
#include <openssl/mem.h>

#include "internal.h"


static int hmac_size(OPENSSL_UNUSED const EVP_PKEY *pkey) {
  return EVP_MAX_MD_SIZE;
}

static void hmac_key_free(EVP_PKEY *pkey) {
  HMAC_KEY *key = pkey->pkey.ptr;
  OPENSSL_free(key);
}

const EVP_PKEY_ASN1_METHOD hmac_asn1_meth = {
    EVP_PKEY_HMAC,
    {0xff} /* placeholder oid */,
    0 /* oid_len */,
    NULL /* pub_decode */,
    NULL /* pub_encode */,
    NULL /* pub_cmp */,
    NULL /*priv_decode */,
    NULL /* priv_encode */,
    NULL /* priv_encode_v2 */,
    NULL /* set_priv_raw */,
    NULL /* set_pub_raw */,
    NULL /* get_priv_raw */,
    NULL /* get_pub_raw */,
    NULL /* pkey_opaque */,
    hmac_size /* pkey_size */,
    NULL /* pkey_bits */,
    NULL /* param_missing */,
    NULL /* param_copy */,
    NULL /* param_cmp */,
    hmac_key_free /* pkey_free */
};
