/*
 * Written by Dr Stephen N Henson (steve@openssl.org) for the OpenSSL
 * project.
 */
/* ====================================================================
 * Copyright (c) 2015 The OpenSSL Project.  All rights reserved.
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
 */

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <openssl/aes.h>
#include <openssl/cipher.h>
#include <openssl/err.h>
#include <openssl/nid.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <openssl/span.h>

#include "../test/file_test.h"
#include "../test/test_util.h"
#include "../test/wycheproof_util.h"
#include "./internal.h"


static const EVP_CIPHER *GetCipher(const std::string &name) {
  if (name == "DES-CBC") {
    return EVP_des_cbc();
  } else if (name == "DES-ECB") {
    return EVP_des_ecb();
  } else if (name == "DES-EDE") {
    return EVP_des_ede();
  } else if (name == "DES-EDE3") {
    return EVP_des_ede3();
  } else if (name == "DES-EDE-CBC") {
    return EVP_des_ede_cbc();
  } else if (name == "DES-EDE3-CBC") {
    return EVP_des_ede3_cbc();
  } else if (name == "RC4") {
    return EVP_rc4();
  } else if (name == "AES-128-ECB") {
    return EVP_aes_128_ecb();
  } else if (name == "AES-256-ECB") {
    return EVP_aes_256_ecb();
  } else if (name == "AES-128-CBC") {
    return EVP_aes_128_cbc();
  } else if (name == "AES-128-GCM") {
    return EVP_aes_128_gcm();
  } else if (name == "AES-128-OFB") {
    return EVP_aes_128_ofb();
  } else if (name == "AES-192-CBC") {
    return EVP_aes_192_cbc();
  } else if (name == "AES-192-CTR") {
    return EVP_aes_192_ctr();
  } else if (name == "AES-192-ECB") {
    return EVP_aes_192_ecb();
  } else if (name == "AES-192-GCM") {
    return EVP_aes_192_gcm();
  } else if (name == "AES-192-OFB") {
    return EVP_aes_192_ofb();
  } else if (name == "AES-256-CBC") {
    return EVP_aes_256_cbc();
  } else if (name == "AES-128-CTR") {
    return EVP_aes_128_ctr();
  } else if (name == "AES-256-CTR") {
    return EVP_aes_256_ctr();
  } else if (name == "AES-256-GCM") {
    return EVP_aes_256_gcm();
  } else if (name == "AES-256-OFB") {
    return EVP_aes_256_ofb();
  } else if (name == "CHACHA20-POLY1305") {
    return EVP_chacha20_poly1305();
  }
  return nullptr;
}

enum class Operation {
  // kBoth tests both encryption and decryption.
  kBoth,
  // kEncrypt tests encryption. The result of encryption should always
  // successfully decrypt, so this should only be used if the test file has a
  // matching decrypt-only vector.
  kEncrypt,
  // kDecrypt tests decryption. This should only be used if the test file has a
  // matching encrypt-only input, or if multiple ciphertexts are valid for
  // a given plaintext and this is a non-canonical ciphertext.
  kDecrypt,
  // kInvalidDecrypt tests decryption and expects it to fail, e.g. due to
  // invalid tag or padding.
  kInvalidDecrypt,
};

static const char *OperationToString(Operation op) {
  switch (op) {
    case Operation::kBoth:
      return "Both";
    case Operation::kEncrypt:
      return "Encrypt";
    case Operation::kDecrypt:
      return "Decrypt";
    case Operation::kInvalidDecrypt:
      return "InvalidDecrypt";
  }
  abort();
}

// MaybeCopyCipherContext, if |copy| is true, replaces |*ctx| with a, hopefully
// equivalent, copy of it.
static bool MaybeCopyCipherContext(bool copy,
                                   bssl::UniquePtr<EVP_CIPHER_CTX> *ctx) {
  if (!copy) {
    return true;
  }
  bssl::UniquePtr<EVP_CIPHER_CTX> ctx2(EVP_CIPHER_CTX_new());
  if (!ctx2 || !EVP_CIPHER_CTX_copy(ctx2.get(), ctx->get())) {
    return false;
  }
  *ctx = std::move(ctx2);
  return true;
}

static void TestCipherAPI(const EVP_CIPHER *cipher, Operation op, bool padding,
                          bool copy, bool in_place, bool use_evp_cipher,
                          size_t chunk_size, bssl::Span<const uint8_t> key,
                          bssl::Span<const uint8_t> iv,
                          bssl::Span<const uint8_t> plaintext,
                          bssl::Span<const uint8_t> ciphertext,
                          bssl::Span<const uint8_t> aad,
                          bssl::Span<const uint8_t> tag) {
  bool encrypt = op == Operation::kEncrypt;
  bool is_custom_cipher =
      EVP_CIPHER_flags(cipher) & EVP_CIPH_FLAG_CUSTOM_CIPHER;
  bssl::Span<const uint8_t> in = encrypt ? plaintext : ciphertext;
  bssl::Span<const uint8_t> expected = encrypt ? ciphertext : plaintext;
  bool is_aead = EVP_CIPHER_flags(cipher) & EVP_CIPH_FLAG_AEAD_CIPHER;

  // Some |EVP_CIPHER|s take a variable-length key, and need to first be
  // configured with the key length, which requires configuring the cipher.
  bssl::UniquePtr<EVP_CIPHER_CTX> ctx(EVP_CIPHER_CTX_new());
  ASSERT_TRUE(ctx);
  ASSERT_TRUE(EVP_CipherInit_ex(ctx.get(), cipher, /*engine=*/nullptr,
                                /*key=*/nullptr, /*iv=*/nullptr,
                                encrypt ? 1 : 0));
  ASSERT_TRUE(EVP_CIPHER_CTX_set_key_length(ctx.get(), key.size()));
  if (!padding) {
    ASSERT_TRUE(EVP_CIPHER_CTX_set_padding(ctx.get(), 0));
  }

  // Configure the key.
  ASSERT_TRUE(MaybeCopyCipherContext(copy, &ctx));
  ASSERT_TRUE(EVP_CipherInit_ex(ctx.get(), /*cipher=*/nullptr,
                                /*engine=*/nullptr, key.data(), /*iv=*/nullptr,
                                /*enc=*/-1));

  // Configure the IV to run the actual operation. Callers that wish to use a
  // key for multiple, potentially concurrent, operations will likely copy at
  // this point. The |EVP_CIPHER_CTX| API uses the same type to represent a
  // pre-computed key schedule and a streaming operation.
  ASSERT_TRUE(MaybeCopyCipherContext(copy, &ctx));
  if (is_aead) {
    ASSERT_LE(iv.size(), size_t{INT_MAX});
    ASSERT_TRUE(EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_AEAD_SET_IVLEN,
                                    static_cast<int>(iv.size()), 0));
  } else {
    ASSERT_EQ(iv.size(), EVP_CIPHER_CTX_iv_length(ctx.get()));
  }
  ASSERT_TRUE(EVP_CipherInit_ex(ctx.get(), /*cipher=*/nullptr,
                                /*engine=*/nullptr,
                                /*key=*/nullptr, iv.data(), /*enc=*/-1));

  if (is_aead && !encrypt) {
    ASSERT_TRUE(EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_AEAD_SET_TAG,
                                    tag.size(),
                                    const_cast<uint8_t *>(tag.data())));
  }

  // Note: the deprecated |EVP_CIPHER|-based AEAD API is sensitive to whether
  // parameters are NULL, so it is important to skip the |in| and |aad|
  // |EVP_CipherUpdate| calls when empty.
  while (!aad.empty()) {
    size_t todo =
        chunk_size == 0 ? aad.size() : std::min(aad.size(), chunk_size);
    if (use_evp_cipher) {
      // AEADs always use the "custom cipher" return value convention. Passing a
      // null output pointer triggers the AAD logic.
      ASSERT_TRUE(is_custom_cipher);
      ASSERT_EQ(static_cast<int>(todo),
                EVP_Cipher(ctx.get(), nullptr, aad.data(), todo));
    } else {
      int len;
      ASSERT_TRUE(EVP_CipherUpdate(ctx.get(), nullptr, &len, aad.data(), todo));
      // Although it doesn't output anything, |EVP_CipherUpdate| should claim to
      // output the input length.
      EXPECT_EQ(len, static_cast<int>(todo));
    }
    aad = aad.subspan(todo);
  }

  // Set up the output buffer.
  size_t max_out = in.size();
  size_t block_size = EVP_CIPHER_CTX_block_size(ctx.get());
  if (block_size > 1 &&
      (EVP_CIPHER_CTX_flags(ctx.get()) & EVP_CIPH_NO_PADDING) == 0 &&
      EVP_CIPHER_CTX_encrypting(ctx.get())) {
    max_out += block_size - (max_out % block_size);
  }
  std::vector<uint8_t> result(max_out);
  if (in_place) {
    std::copy(in.begin(), in.end(), result.begin());
    in = bssl::MakeConstSpan(result).first(in.size());
  }

  size_t total = 0;
  int len;
  while (!in.empty()) {
    size_t todo = chunk_size == 0 ? in.size() : std::min(in.size(), chunk_size);
    EXPECT_LE(todo, static_cast<size_t>(INT_MAX));
    ASSERT_TRUE(MaybeCopyCipherContext(copy, &ctx));
    if (use_evp_cipher) {
      // |EVP_Cipher| sometimes returns the number of bytes written, or -1 on
      // error, and sometimes 1 or 0, implicitly writing |in_len| bytes.
      if (is_custom_cipher) {
        len = EVP_Cipher(ctx.get(), result.data() + total, in.data(), todo);
      } else {
        ASSERT_EQ(
            1, EVP_Cipher(ctx.get(), result.data() + total, in.data(), todo));
        len = static_cast<int>(todo);
      }
    } else {
      ASSERT_TRUE(EVP_CipherUpdate(ctx.get(), result.data() + total, &len,
                                   in.data(), static_cast<int>(todo)));
    }
    ASSERT_GE(len, 0);
    total += static_cast<size_t>(len);
    in = in.subspan(todo);
  }
  if (op == Operation::kInvalidDecrypt) {
    if (use_evp_cipher) {
      // Only the "custom cipher" return value convention can report failures.
      // Passing all nulls should act like |EVP_CipherFinal_ex|.
      ASSERT_TRUE(is_custom_cipher);
      EXPECT_EQ(-1, EVP_Cipher(ctx.get(), nullptr, nullptr, 0));
    } else {
      // Invalid padding and invalid tags all appear as a failed
      // |EVP_CipherFinal_ex|.
      EXPECT_FALSE(EVP_CipherFinal_ex(ctx.get(), result.data() + total, &len));
    }
  } else {
    if (use_evp_cipher) {
      if (is_custom_cipher) {
        // Only the "custom cipher" convention has an |EVP_CipherFinal_ex|
        // equivalent.
        len = EVP_Cipher(ctx.get(), nullptr, nullptr, 0);
      } else {
        len = 0;
      }
    } else {
      ASSERT_TRUE(EVP_CipherFinal_ex(ctx.get(), result.data() + total, &len));
    }
    ASSERT_GE(len, 0);
    total += static_cast<size_t>(len);
    result.resize(total);
    EXPECT_EQ(Bytes(expected), Bytes(result));
    if (encrypt && is_aead) {
      uint8_t rtag[16];
      ASSERT_LE(tag.size(), sizeof(rtag));
      ASSERT_TRUE(MaybeCopyCipherContext(copy, &ctx));
      ASSERT_TRUE(EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_AEAD_GET_TAG,
                                      tag.size(), rtag));
      EXPECT_EQ(Bytes(tag), Bytes(rtag, tag.size()));
    }
  }
}

static void TestLowLevelAPI(
    const EVP_CIPHER *cipher, Operation op, bool in_place, size_t chunk_size,
    bssl::Span<const uint8_t> key, bssl::Span<const uint8_t> iv,
    bssl::Span<const uint8_t> plaintext, bssl::Span<const uint8_t> ciphertext) {
  bool encrypt = op == Operation::kEncrypt;
  bssl::Span<const uint8_t> in = encrypt ? plaintext : ciphertext;
  bssl::Span<const uint8_t> expected = encrypt ? ciphertext : plaintext;
  int nid = EVP_CIPHER_nid(cipher);
  bool is_ctr = nid == NID_aes_128_ctr || nid == NID_aes_192_ctr ||
                nid == NID_aes_256_ctr;
  bool is_cbc = nid == NID_aes_128_cbc || nid == NID_aes_192_cbc ||
                nid == NID_aes_256_cbc;
  bool is_ofb = nid == NID_aes_128_ofb128 || nid == NID_aes_192_ofb128 ||
                nid == NID_aes_256_ofb128;
  if (!is_ctr && !is_cbc && !is_ofb) {
    return;
  }

  // Invalid ciphertexts are not possible in any of the ciphers where this API
  // applies.
  ASSERT_NE(op, Operation::kInvalidDecrypt);

  AES_KEY aes;
  if (encrypt || !is_cbc) {
    ASSERT_EQ(0, AES_set_encrypt_key(key.data(), key.size() * 8, &aes));
  } else {
    ASSERT_EQ(0, AES_set_decrypt_key(key.data(), key.size() * 8, &aes));
  }

  std::vector<uint8_t> result;
  if (in_place) {
    result.assign(in.begin(), in.end());
  } else {
    result.resize(expected.size());
  }
  bssl::Span<uint8_t> out = bssl::MakeSpan(result);
  // Input and output sizes for all the low-level APIs should match.
  ASSERT_EQ(in.size(), out.size());

  // The low-level APIs all use block-size IVs.
  ASSERT_EQ(iv.size(), size_t{AES_BLOCK_SIZE});
  uint8_t ivec[AES_BLOCK_SIZE];
  OPENSSL_memcpy(ivec, iv.data(), iv.size());

  if (is_ctr) {
    unsigned num = 0;
    uint8_t ecount_buf[AES_BLOCK_SIZE];
    if (chunk_size == 0) {
      AES_ctr128_encrypt(in.data(), out.data(), in.size(), &aes, ivec,
                         ecount_buf, &num);
    } else {
      do {
        size_t todo = std::min(in.size(), chunk_size);
        AES_ctr128_encrypt(in.data(), out.data(), todo, &aes, ivec, ecount_buf,
                           &num);
        in = in.subspan(todo);
        out = out.subspan(todo);
      } while (!in.empty());
    }
    EXPECT_EQ(Bytes(expected), Bytes(result));
  } else if (is_cbc && chunk_size % AES_BLOCK_SIZE == 0) {
    // Note |AES_cbc_encrypt| requires block-aligned chunks.
    if (chunk_size == 0) {
      AES_cbc_encrypt(in.data(), out.data(), in.size(), &aes, ivec, encrypt);
    } else {
      do {
        size_t todo = std::min(in.size(), chunk_size);
        AES_cbc_encrypt(in.data(), out.data(), todo, &aes, ivec, encrypt);
        in = in.subspan(todo);
        out = out.subspan(todo);
      } while (!in.empty());
    }
    EXPECT_EQ(Bytes(expected), Bytes(result));
  } else if (is_ofb) {
    int num = 0;
    if (chunk_size == 0) {
      AES_ofb128_encrypt(in.data(), out.data(), in.size(), &aes, ivec, &num);
    } else {
      do {
        size_t todo = std::min(in.size(), chunk_size);
        AES_ofb128_encrypt(in.data(), out.data(), todo, &aes, ivec, &num);
        in = in.subspan(todo);
        out = out.subspan(todo);
      } while (!in.empty());
    }
    EXPECT_EQ(Bytes(expected), Bytes(result));
  }
}

static void TestCipher(const EVP_CIPHER *cipher, Operation input_op,
                       bool padding, bssl::Span<const uint8_t> key,
                       bssl::Span<const uint8_t> iv,
                       bssl::Span<const uint8_t> plaintext,
                       bssl::Span<const uint8_t> ciphertext,
                       bssl::Span<const uint8_t> aad,
                       bssl::Span<const uint8_t> tag) {
  size_t block_size = EVP_CIPHER_block_size(cipher);
  std::vector<Operation> ops;
  if (input_op == Operation::kBoth) {
    ops = {Operation::kEncrypt, Operation::kDecrypt};
  } else {
    ops = {input_op};
  }
  for (Operation op : ops) {
    SCOPED_TRACE(OperationToString(op));
    // Zero indicates a single-shot API.
    static const size_t kChunkSizes[] = {0,  1,  2,  5,  7,  8,  9,  15, 16,
                                         17, 31, 32, 33, 63, 64, 65, 512};
    for (size_t chunk_size : kChunkSizes) {
      SCOPED_TRACE(chunk_size);
      if (chunk_size > plaintext.size() && chunk_size > ciphertext.size() &&
          chunk_size > aad.size()) {
        continue;
      }
      for (bool in_place : {false, true}) {
        SCOPED_TRACE(in_place);
        for (bool copy : {false, true}) {
          SCOPED_TRACE(copy);
          TestCipherAPI(cipher, op, padding, copy, in_place,
                        /*use_evp_cipher=*/false, chunk_size, key, iv,
                        plaintext, ciphertext, aad, tag);
          if (!padding && chunk_size % block_size == 0) {
            TestCipherAPI(cipher, op, padding, copy, in_place,
                          /*use_evp_cipher=*/true, chunk_size, key, iv,
                          plaintext, ciphertext, aad, tag);
          }
        }
        if (!padding) {
          TestLowLevelAPI(cipher, op, in_place, chunk_size, key, iv, plaintext,
                          ciphertext);
        }
      }
    }
  }
}

static void CipherFileTest(FileTest *t) {
  std::string cipher_str;
  ASSERT_TRUE(t->GetAttribute(&cipher_str, "Cipher"));
  const EVP_CIPHER *cipher = GetCipher(cipher_str);
  ASSERT_TRUE(cipher);

  std::vector<uint8_t> key, iv, plaintext, ciphertext, aad, tag;
  ASSERT_TRUE(t->GetBytes(&key, "Key"));
  ASSERT_TRUE(t->GetBytes(&plaintext, "Plaintext"));
  ASSERT_TRUE(t->GetBytes(&ciphertext, "Ciphertext"));
  if (EVP_CIPHER_iv_length(cipher) > 0) {
    ASSERT_TRUE(t->GetBytes(&iv, "IV"));
  }
  if (EVP_CIPHER_flags(cipher) & EVP_CIPH_FLAG_AEAD_CIPHER) {
    ASSERT_TRUE(t->GetBytes(&aad, "AAD"));
    ASSERT_TRUE(t->GetBytes(&tag, "Tag"));
  }

  Operation op = Operation::kBoth;
  if (t->HasAttribute("Operation")) {
    const std::string &str = t->GetAttributeOrDie("Operation");
    if (str == "Encrypt" || str == "ENCRYPT") {
      op = Operation::kEncrypt;
    } else if (str == "Decrypt" || str == "DECRYPT") {
      op = Operation::kDecrypt;
    } else if (str == "InvalidDecrypt") {
      op = Operation::kInvalidDecrypt;
    } else {
      FAIL() << "Unknown operation: " << str;
    }
  }

  TestCipher(cipher, op, /*padding=*/false, key, iv, plaintext, ciphertext, aad,
             tag);
}

TEST(CipherTest, TestVectors) {
  FileTestGTest("crypto/cipher_extra/test/cipher_tests.txt", CipherFileTest);
}

TEST(CipherTest, CAVP_AES_128_CBC) {
  FileTestGTest("crypto/cipher_extra/test/nist_cavp/aes_128_cbc.txt",
                CipherFileTest);
}

TEST(CipherTest, CAVP_AES_128_CTR) {
  FileTestGTest("crypto/cipher_extra/test/nist_cavp/aes_128_ctr.txt",
                CipherFileTest);
}

TEST(CipherTest, CAVP_AES_192_CBC) {
  FileTestGTest("crypto/cipher_extra/test/nist_cavp/aes_192_cbc.txt",
                CipherFileTest);
}

TEST(CipherTest, CAVP_AES_192_CTR) {
  FileTestGTest("crypto/cipher_extra/test/nist_cavp/aes_192_ctr.txt",
                CipherFileTest);
}

TEST(CipherTest, CAVP_AES_256_CBC) {
  FileTestGTest("crypto/cipher_extra/test/nist_cavp/aes_256_cbc.txt",
                CipherFileTest);
}

TEST(CipherTest, CAVP_AES_256_CTR) {
  FileTestGTest("crypto/cipher_extra/test/nist_cavp/aes_256_ctr.txt",
                CipherFileTest);
}

TEST(CipherTest, CAVP_TDES_CBC) {
  FileTestGTest("crypto/cipher_extra/test/nist_cavp/tdes_cbc.txt",
                CipherFileTest);
}

TEST(CipherTest, CAVP_TDES_ECB) {
  FileTestGTest("crypto/cipher_extra/test/nist_cavp/tdes_ecb.txt",
                CipherFileTest);
}

TEST(CipherTest, Chacha20Poly1305) {
  FileTestGTest("crypto/cipher_extra/test/chacha20_poly1305_tests.txt",
    [](FileTest *t) {
      const EVP_CIPHER *cipher = EVP_chacha20_poly1305();
      ASSERT_TRUE(cipher);

      std::vector<uint8_t> key, iv, plaintext, ciphertext, aad, tag;
      ASSERT_TRUE(t->GetBytes(&key, "KEY"));
      ASSERT_TRUE(t->GetBytes(&plaintext, "IN"));
      ASSERT_TRUE(t->GetBytes(&ciphertext, "CT"));
      if (EVP_CIPHER_iv_length(cipher) > 0) {
        ASSERT_TRUE(t->GetBytes(&iv, "NONCE"));
      }
      ASSERT_TRUE(EVP_CIPHER_flags(cipher) & EVP_CIPH_FLAG_AEAD_CIPHER);
      ASSERT_TRUE(t->GetBytes(&aad, "AD"));
      ASSERT_TRUE(t->GetBytes(&tag, "TAG"));

      Operation op = Operation::kBoth;
      TestCipher(cipher, op, /*padding=*/false, key, iv, plaintext, ciphertext,
                 aad, tag);
  });
}

static void WycheproofChacha20Poly1305FileTest(FileTest *t) {
  t->IgnoreInstruction("type");
  t->IgnoreInstruction("tagSize");
  t->IgnoreInstruction("keySize");
  std::string iv_size;
  ASSERT_TRUE(t->GetInstruction(&iv_size, "ivSize"));

  std::vector<uint8_t> key, iv, msg, ct, aad, tag;
  ASSERT_TRUE(t->GetBytes(&key, "key"));
  ASSERT_TRUE(t->GetBytes(&iv, "iv"));
  ASSERT_TRUE(t->GetBytes(&msg, "msg"));
  ASSERT_TRUE(t->GetBytes(&ct, "ct"));
  ASSERT_TRUE(t->GetBytes(&aad, "aad"));
  ASSERT_TRUE(t->GetBytes(&tag, "tag"));

  WycheproofResult result;
  ASSERT_TRUE(GetWycheproofResult(t, &result));

  // Allocate a ctx object
  const EVP_CIPHER *cipher = EVP_chacha20_poly1305();
  std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> uctx(
          EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
  EVP_CIPHER_CTX *ctx = uctx.get();
  ASSERT_TRUE(ctx);

  // Initialize without the key/iv
  ASSERT_TRUE(EVP_EncryptInit_ex(ctx, cipher, NULL, NULL, NULL));

  // Set the IV size
  int res;
  int iv_size_val = std::stoi(iv_size) / 8;
  res = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_IVLEN, iv_size_val, NULL);
  int is_invalid_iv_size = iv_size_val != 12;
  if (is_invalid_iv_size && !result.IsValid()) {
    // Check that we correctly reject the IV and skip.
    // Otherwise, the cipher will simply just use IV=0.
    ASSERT_EQ(res, 0);
    t->SkipCurrent();
    return;
  } else {
    ASSERT_EQ(res, 1);
  }

  // Initialize with the key/iv
  ASSERT_TRUE(EVP_EncryptInit_ex(ctx, cipher, NULL, key.data(), iv.data()));

  // Insert AAD
  int out_len = 0;
  ASSERT_TRUE(EVP_EncryptUpdate(ctx, /*out*/ NULL, &out_len, aad.data(),
                                aad.size()));
  ASSERT_EQ(out_len, (int) aad.size());

  // Insert plaintext
  std::vector<uint8_t> computed_ct(ct.size());

  uint8_t junk_buf[1];
  uint8_t *in = msg.empty() ? junk_buf : msg.data();
  out_len = 0;
  ASSERT_TRUE(EVP_EncryptUpdate(ctx, computed_ct.data(), &out_len, in,
                          msg.size()));
  ASSERT_EQ(out_len, (int) msg.size());

  // Finish the cipher
  out_len = 0;
  ASSERT_TRUE(EVP_EncryptFinal(ctx, NULL, &out_len));
  ASSERT_EQ(out_len, 0);

  // Get the tag
  std::vector<uint8_t> computed_tag(tag.size());
  ASSERT_TRUE(EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_GET_TAG, tag.size(),
                            computed_tag.data()));

  // Initialize the decrypt context
  std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> udctx(
          EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
  EVP_CIPHER_CTX *dctx = udctx.get();
  ASSERT_TRUE(dctx);
  ASSERT_TRUE(EVP_DecryptInit_ex(dctx, cipher, NULL, key.data(), iv.data()));

  // Insert AAD
  out_len = 0;
  ASSERT_TRUE(EVP_DecryptUpdate(dctx, NULL, &out_len, aad.data(), aad.size()));
  ASSERT_EQ(out_len, (int) aad.size());

  // Insert ciphertext
  std::vector<uint8_t> computed_pt(msg.size());
  in = ct.empty() ? junk_buf : ct.data();
  out_len = 0;
  ASSERT_TRUE(EVP_DecryptUpdate(dctx, computed_pt.data(), &out_len, in,
                                ct.size()));
  ASSERT_EQ(out_len, (int) msg.size());

  // Set the tag
  ASSERT_TRUE(EVP_CIPHER_CTX_ctrl(dctx, EVP_CTRL_AEAD_SET_TAG, tag.size(),
                            tag.data()));

  // Finish decryption
  out_len = 0;
  res = EVP_DecryptFinal(dctx, NULL, &out_len);

  // Tag validation, a valid result should end with a valid tag
  ASSERT_EQ(res, result.IsValid() ? 1 : 0);
  ASSERT_EQ(out_len, 0);

  // Valid results should match the KATs
  if (result.IsValid()) {
    ASSERT_EQ(Bytes(tag.data(), tag.size()),
              Bytes(computed_tag.data(), computed_tag.size()));
    ASSERT_EQ(Bytes(msg.data(), msg.size()),
              Bytes(computed_pt.data(), computed_pt.size()));
    ASSERT_EQ(Bytes(ct.data(), ct.size()),
              Bytes(computed_ct.data(), computed_ct.size()));
  }
}

TEST(CipherTest, WycheproofChacha20Poly1305) {
  FileTestGTest("third_party/wycheproof_testvectors/chacha20_poly1305_test.txt",
                WycheproofChacha20Poly1305FileTest);
}

TEST(CipherTest, WycheproofAESCBC) {
  FileTestGTest("third_party/wycheproof_testvectors/aes_cbc_pkcs5_test.txt",
                [](FileTest *t) {
                  t->IgnoreInstruction("type");
                  t->IgnoreInstruction("ivSize");

                  std::string key_size;
                  ASSERT_TRUE(t->GetInstruction(&key_size, "keySize"));
                  const EVP_CIPHER *cipher;
                  switch (atoi(key_size.c_str())) {
                    case 128:
                      cipher = EVP_aes_128_cbc();
                      break;
                    case 192:
                      cipher = EVP_aes_192_cbc();
                      break;
                    case 256:
                      cipher = EVP_aes_256_cbc();
                      break;
                    default:
                      FAIL() << "Unsupported key size: " << key_size;
                  }

                  std::vector<uint8_t> key, iv, msg, ct;
                  ASSERT_TRUE(t->GetBytes(&key, "key"));
                  ASSERT_TRUE(t->GetBytes(&iv, "iv"));
                  ASSERT_TRUE(t->GetBytes(&msg, "msg"));
                  ASSERT_TRUE(t->GetBytes(&ct, "ct"));
                  WycheproofResult result;
                  ASSERT_TRUE(GetWycheproofResult(t, &result));
                  TestCipher(cipher,
                             result.IsValid() ? Operation::kBoth
                                              : Operation::kInvalidDecrypt,
                             /*padding=*/true, key, iv, msg, ct, /*aad=*/{},
                             /*tag=*/{});
                });
}

TEST(CipherTest, SHA1WithSecretSuffix) {
  uint8_t buf[SHA_CBLOCK * 4];
  RAND_bytes(buf, sizeof(buf));
  // Hashing should run in time independent of the bytes.
  CONSTTIME_SECRET(buf, sizeof(buf));

  // Exhaustively testing interesting cases in this function is cubic in the
  // block size, so we test in 3-byte increments.
  constexpr size_t kSkip = 3;
  // This value should be less than 8 to test the edge case when the 8-byte
  // length wraps to the next block.
  static_assert(kSkip < 8, "kSkip is too large");

  // |EVP_final_with_secret_suffix_sha1| is sensitive to the public length of
  // the partial block previously hashed. In TLS, this is the HMAC prefix, the
  // header, and the public minimum padding length.
  for (size_t prefix = 0; prefix < SHA_CBLOCK; prefix += kSkip) {
    SCOPED_TRACE(prefix);
    // The first block is treated differently, so we run with up to three
    // blocks of length variability.
    for (size_t max_len = 0; max_len < 3 * SHA_CBLOCK; max_len += kSkip) {
      SCOPED_TRACE(max_len);
      for (size_t len = 0; len <= max_len; len += kSkip) {
        SCOPED_TRACE(len);

        uint8_t expected[SHA_DIGEST_LENGTH];
        SHA1(buf, prefix + len, expected);
        CONSTTIME_DECLASSIFY(expected, sizeof(expected));

        // Make a copy of the secret length to avoid interfering with the loop.
        size_t secret_len = len;
        CONSTTIME_SECRET(&secret_len, sizeof(secret_len));

        SHA_CTX ctx;
        SHA1_Init(&ctx);
        SHA1_Update(&ctx, buf, prefix);
        uint8_t computed[SHA_DIGEST_LENGTH];
        ASSERT_TRUE(EVP_final_with_secret_suffix_sha1(
            &ctx, computed, buf + prefix, secret_len, max_len));

        CONSTTIME_DECLASSIFY(computed, sizeof(computed));
        EXPECT_EQ(Bytes(expected), Bytes(computed));
      }
    }
  }
}

TEST(CipherTest, SHA256WithSecretSuffix) {
  uint8_t buf[SHA256_CBLOCK * 4];
  RAND_bytes(buf, sizeof(buf));
  // Hashing should run in time independent of the bytes.
  CONSTTIME_SECRET(buf, sizeof(buf));

  // Exhaustively testing interesting cases in this function is cubic in the
  // block size, so we test in 3-byte increments.
  constexpr size_t kSkip = 3;
  // This value should be less than 8 to test the edge case when the 8-byte
  // length wraps to the next block.
  static_assert(kSkip < 8, "kSkip is too large");

  // |EVP_final_with_secret_suffix_sha256| is sensitive to the public length of
  // the partial block previously hashed. In TLS, this is the HMAC prefix, the
  // header, and the public minimum padding length.
  for (size_t prefix = 0; prefix < SHA256_CBLOCK; prefix += kSkip) {
    SCOPED_TRACE(prefix);
    // The first block is treated differently, so we run with up to three
    // blocks of length variability.
    for (size_t max_len = 0; max_len < 3 * SHA256_CBLOCK; max_len += kSkip) {
      SCOPED_TRACE(max_len);
      for (size_t len = 0; len <= max_len; len += kSkip) {
        SCOPED_TRACE(len);

        uint8_t expected[SHA256_DIGEST_LENGTH];
        SHA256(buf, prefix + len, expected);
        CONSTTIME_DECLASSIFY(expected, sizeof(expected));

        // Make a copy of the secret length to avoid interfering with the loop.
        size_t secret_len = len;
        CONSTTIME_SECRET(&secret_len, sizeof(secret_len));

        SHA256_CTX ctx;
        SHA256_Init(&ctx);
        SHA256_Update(&ctx, buf, prefix);
        uint8_t computed[SHA256_DIGEST_LENGTH];
        ASSERT_TRUE(EVP_final_with_secret_suffix_sha256(
            &ctx, computed, buf + prefix, secret_len, max_len));

        CONSTTIME_DECLASSIFY(computed, sizeof(computed));
        EXPECT_EQ(Bytes(expected), Bytes(computed));
      }
    }
  }
}

TEST(CipherTest, GetCipher) {
  const EVP_CIPHER *cipher = EVP_get_cipherbynid(NID_aes_128_gcm);
  ASSERT_TRUE(cipher);
  EXPECT_EQ(NID_aes_128_gcm, EVP_CIPHER_nid(cipher));

  cipher = EVP_get_cipherbyname("aes-128-gcm");
  ASSERT_TRUE(cipher);
  EXPECT_EQ(NID_aes_128_gcm, EVP_CIPHER_nid(cipher));

  cipher = EVP_get_cipherbyname("AES-128-GCM");
  ASSERT_TRUE(cipher);
  EXPECT_EQ(NID_aes_128_gcm, EVP_CIPHER_nid(cipher));

  // We support a tcpdump-specific alias for 3DES.
  cipher = EVP_get_cipherbyname("3des");
  ASSERT_TRUE(cipher);
  EXPECT_EQ(NID_des_ede3_cbc, EVP_CIPHER_nid(cipher));

  cipher = EVP_get_cipherbyname("aes256");
  ASSERT_TRUE(cipher);
  EXPECT_EQ(NID_aes_256_cbc, EVP_CIPHER_nid(cipher));

  cipher = EVP_get_cipherbyname("aes128");
  ASSERT_TRUE(cipher);
  EXPECT_EQ(NID_aes_128_cbc, EVP_CIPHER_nid(cipher));
}
