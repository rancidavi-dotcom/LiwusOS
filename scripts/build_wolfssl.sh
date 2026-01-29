#!/bin/bash
set -e

WOLFSSL_DIR="/mnt/c/Users/davivbr/Desktop/LiwusOS/LiwusOS/third_party/wolfssl"
OUT_DIR="/tmp/wolfssl_build2"
CFLAGS="-I$WOLFSSL_DIR -I$WOLFSSL_DIR/wolfcrypt/src -include $WOLFSSL_DIR/user_settings.h -D__LiwusOS__ -D__x86_64__ -Wno-error"

mkdir -p $OUT_DIR/wolfcrypt $OUT_DIR/wolfssl

echo "Compiling wolfcrypt..."
for f in $WOLFSSL_DIR/wolfcrypt/src/aes.c $WOLFSSL_DIR/wolfcrypt/src/arc4.c $WOLFSSL_DIR/wolfcrypt/src/ascon.c $WOLFSSL_DIR/wolfcrypt/src/asn.c $WOLFSSL_DIR/wolfcrypt/src/cmac.c $WOLFSSL_DIR/wolfcrypt/src/coding.c $WOLFSSL_DIR/wolfcrypt/src/compress.c $WOLFSSL_DIR/wolfcrypt/src/des3.c $WOLFSSL_DIR/wolfcrypt/src/dh.c $WOLFSSL_DIR/wolfcrypt/src/ecc.c $WOLFSSL_DIR/wolfcrypt/src/error.c $WOLFSSL_DIR/wolfcrypt/src/hmac.c $WOLFSSL_DIR/wolfcrypt/src/integer.c $WOLFSSL_DIR/wolfcrypt/src/logging.c $WOLFSSL_DIR/wolfcrypt/src/md2.c $WOLFSSL_DIR/wolfcrypt/src/md4.c $WOLFSSL_DIR/wolfcrypt/src/md5.c $WOLFSSL_DIR/wolfcrypt/src/memory.c $WOLFSSL_DIR/wolfcrypt/src/pkcs7.c $WOLFSSL_DIR/wolfcrypt/src/pkcs12.c $WOLFSSL_DIR/wolfcrypt/src/poly1305.c $WOLFSSL_DIR/wolfcrypt/src/random.c $WOLFSSL_DIR/wolfcrypt/src/ripemd.c $WOLFSSL_DIR/wolfcrypt/src/rsa.c $WOLFSSL_DIR/wolfcrypt/src/sha.c $WOLFSSL_DIR/wolfcrypt/src/sha256.c $WOLFSSL_DIR/wolfcrypt/src/sha512.c $WOLFSSL_DIR/wolfcrypt/src/signature.c $WOLFSSL_DIR/wolfcrypt/src/sp_int.c $WOLFSSL_DIR/wolfcrypt/src/sp_c64.c $WOLFSSL_DIR/wolfcrypt/src/srp.c $WOLFSSL_DIR/wolfcrypt/src/wc_encrypt.c $WOLFSSL_DIR/wolfcrypt/src/wc_port.c $WOLFSSL_DIR/wolfcrypt/src/wolfevent.c $WOLFSSL_DIR/wolfcrypt/src/hash.c $WOLFSSL_DIR/wolfcrypt/src/chacha.c $WOLFSSL_DIR/wolfcrypt/src/chacha20_poly1305.c $WOLFSSL_DIR/wolfcrypt/src/ed25519.c $WOLFSSL_DIR/wolfcrypt/src/curve25519.c $WOLFSSL_DIR/wolfcrypt/src/blake2.c $WOLFSSL_DIR/wolfcrypt/src/blake2b.c $WOLFSSL_DIR/wolfcrypt/src/blake2s.c $WOLFSSL_DIR/wolfcrypt/src/argon2.c $WOLFSSL_DIR/wolfcrypt/src/hkdf.c; do
    gcc $CFLAGS -c $f -o $OUT_DIR/wolfcrypt/$(basename $f .c).o
done

echo "Compiling wolfssl..."
for f in $WOLFSSL_DIR/src/ssl.c $WOLFSSL_DIR/src/bio.c $WOLFSSL_DIR/src/conf.c $WOLFSSL_DIR/src/crl.c $WOLFSSL_DIR/src/dtls.c $WOLFSSL_DIR/src/dtls13.c $WOLFSSL_DIR/src/internal.c $WOLFSSL_DIR/src/keys.c $WOLFSSL_DIR/src/ocsp.c $WOLFSSL_DIR/src/pk.c $WOLFSSL_DIR/src/pk_ec.c $WOLFSSL_DIR/src/pk_rsa.c $WOLFSSL_DIR/src/quic.c $WOLFSSL_DIR/src/sniffer.c $WOLFSSL_DIR/src/ssl_api_cert.c $WOLFSSL_DIR/src/ssl_api_crl_ocsp.c $WOLFSSL_DIR/src/ssl_api_dtls.c $WOLFSSL_DIR/src/ssl_api_ext.c $WOLFSSL_DIR/src/ssl_api_hs.c $WOLFSSL_DIR/src/ssl_api_pk.c $WOLFSSL_DIR/src/ssl_api_rw.c $WOLFSSL_DIR/src/ssl_asn1.c $WOLFSSL_DIR/src/ssl_bn.c $WOLFSSL_DIR/src/ssl_certman.c $WOLFSSL_DIR/src/ssl_crypto.c $WOLFSSL_DIR/src/ssl_ech.c $WOLFSSL_DIR/src/ssl_err.c $WOLFSSL_DIR/src/ssl_load.c $WOLFSSL_DIR/src/ssl_misc.c; do
    gcc $CFLAGS -c $f -o $OUT_DIR/wolfssl/$(basename $f .c).o
done

echo "Creating static library..."
ar rcs $OUT_DIR/libwolfssl.a $OUT_DIR/wolfcrypt/*.o $OUT_DIR/wolfssl/*.o

echo "Copying headers..."
cp -r $WOLFSSL_DIR/wolfssl/*.h /mnt/c/Users/davivbr/Desktop/LiwusOS/LiwusOS/sdk/include/ 2>/dev/null
cp -r $WOLFSSL_DIR/wolfcrypt/*.h /mnt/c/Users/davivbr/Desktop/LiwusOS/LiwusOS/sdk/include/ 2>/dev/null

echo "Copying library..."
cp $OUT_DIR/libwolfssl.a /mnt/c/Users/davivbr/Desktop/LiwusOS/LiwusOS/sdk/lib/libwolfssl.a

echo "DONE"
ls -la $OUT_DIR/libwolfssl.a