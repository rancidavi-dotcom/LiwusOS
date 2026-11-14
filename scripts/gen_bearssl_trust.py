#!/usr/bin/env python3
"""Generate BearSSL trust anchors from a PEM CA bundle.

Only self-issued CA certificates with RSA or a BearSSL-supported named EC
curve are emitted. The source bundle is pinned in the repository so builds
are reproducible and do not inherit the host's trust configuration.
"""

from __future__ import annotations

import argparse
import base64
import pathlib
import sys


def tlv(data: bytes, pos: int = 0) -> tuple[int, int, int, int]:
    if pos >= len(data):
        raise ValueError("truncated DER tag")
    tag = data[pos]
    pos += 1
    if pos >= len(data):
        raise ValueError("truncated DER length")
    length = data[pos]
    pos += 1
    if length & 0x80:
        count = length & 0x7F
        if count == 0 or count > 4 or pos + count > len(data):
            raise ValueError("invalid DER length")
        length = int.from_bytes(data[pos : pos + count], "big")
        pos += count
    end = pos + length
    if end > len(data):
        raise ValueError("DER value extends past input")
    return tag, pos, end, end


def children(data: bytes, start: int, end: int):
    pos = start
    while pos < end:
        tag, value_start, value_end, next_pos = tlv(data, pos)
        yield tag, pos, value_start, value_end
        pos = next_pos
    if pos != end:
        raise ValueError("misaligned DER children")


def one(data: bytes, pos: int, expected: int | None = None):
    tag, value_start, value_end, next_pos = tlv(data, pos)
    if expected is not None and tag != expected:
        raise ValueError(f"unexpected DER tag {tag:#x}, expected {expected:#x}")
    return tag, value_start, value_end, next_pos


def is_ca(cert: bytes, tbs_fields) -> bool:
    for tag, _start, value_start, value_end in tbs_fields:
        if tag != 0xA3:
            continue
        _, seq_start, seq_end, _ = one(cert, value_start, 0x30)
        for _etag, _es, evs, eve in children(cert, seq_start, seq_end):
            _, e_start, e_end, _ = one(cert, _es, 0x30)
            ext = list(children(cert, e_start, e_end))
            if len(ext) < 2:
                continue
            oid = cert[ext[0][2] : ext[0][3]]
            if oid != bytes.fromhex("551d13"):
                continue
            octet = ext[-1]
            _, os, oe, _ = one(cert, octet[1], 0x04)
            if os == oe:
                return False
            _, bs, be, _ = one(cert, os, 0x30)
            for btag, _bs, bvs, bve in children(cert, bs, be):
                if btag == 0x01 and cert[bvs:bve] != b"\x00":
                    return True
            return False
    return False


def parse_cert(cert: bytes):
    _, cert_start, cert_end, _ = one(cert, 0, 0x30)
    cert_fields = list(children(cert, cert_start, cert_end))
    if len(cert_fields) < 1:
        raise ValueError("empty certificate")
    tbs = cert_fields[0]
    _, tbs_start, tbs_end, _ = one(cert, tbs[1], 0x30)
    tbs_fields = list(children(cert, tbs_start, tbs_end))
    idx = 1 if tbs_fields and tbs_fields[0][0] == 0xA0 else 0
    # serial, signature algorithm, issuer, validity, subject, SPKI
    if len(tbs_fields) < idx + 6:
        raise ValueError("truncated TBSCertificate")
    issuer, subject, spki = tbs_fields[idx + 2], tbs_fields[idx + 4], tbs_fields[idx + 5]
    issuer_der = cert[issuer[1] : issuer[3]]
    subject_der = cert[subject[1] : subject[3]]
    if issuer_der != subject_der or not is_ca(cert, tbs_fields):
        return None

    _, spki_start, spki_end, _ = one(cert, spki[1], 0x30)
    spki_fields = list(children(cert, spki_start, spki_end))
    if len(spki_fields) != 2:
        raise ValueError("invalid SubjectPublicKeyInfo")
    _, alg_start, alg_end, _ = one(cert, spki_fields[0][1], 0x30)
    alg_fields = list(children(cert, alg_start, alg_end))
    if not alg_fields:
        raise ValueError("missing public key algorithm")
    alg_oid = cert[alg_fields[0][2] : alg_fields[0][3]]
    _, bits_start, bits_end, _ = one(cert, spki_fields[1][1], 0x03)
    bits = cert[bits_start:bits_end]
    if not bits or bits[0] != 0:
        raise ValueError("invalid SubjectPublicKey BIT STRING")
    key_data = bits[1:]

    if alg_oid == bytes.fromhex("2a864886f70d010101"):
        _, rsa_start, rsa_end, _ = one(key_data, 0, 0x30)
        rsa_fields = list(children(key_data, rsa_start, rsa_end))
        if len(rsa_fields) != 2:
            raise ValueError("invalid RSA public key")
        modulus = key_data[rsa_fields[0][2] : rsa_fields[0][3]].lstrip(b"\0")
        exponent = key_data[rsa_fields[1][2] : rsa_fields[1][3]].lstrip(b"\0")
        if not modulus or not exponent:
            raise ValueError("empty RSA key component")
        return {"dn": subject_der, "type": "rsa", "n": modulus, "e": exponent}

    if alg_oid == bytes.fromhex("2a8648ce3d0201") and len(alg_fields) >= 2:
        curve_oid = cert[alg_fields[1][2] : alg_fields[1][3]]
        curves = {
            bytes.fromhex("2a8648ce3d030107"): 23,  # secp256r1
            bytes.fromhex("2b81040022"): 24,  # secp384r1
            bytes.fromhex("2b81040023"): 25,  # secp521r1
            bytes.fromhex("2b8104000a"): 22,  # secp256k1
            bytes.fromhex("2b2403030208010107"): 26,  # brainpoolP256r1
            bytes.fromhex("2b240303020801010b"): 27,  # brainpoolP384r1
            bytes.fromhex("2b240303020801010d"): 28,  # brainpoolP512r1
        }
        curve = curves.get(curve_oid)
        if curve is not None and key_data[:1] == b"\x04":
            return {"dn": subject_der, "type": "ec", "curve": curve, "q": key_data}
    return None


def read_bundle(path: pathlib.Path):
    # Some distro bundles contain comments with non-ASCII CA names between
    # PEM blocks; the encoded certificate bodies themselves remain ASCII.
    text = path.read_text(encoding="latin-1")
    marker_a, marker_b = "-----BEGIN CERTIFICATE-----", "-----END CERTIFICATE-----"
    anchors = []
    pos = 0
    while True:
        start = text.find(marker_a, pos)
        if start < 0:
            break
        end = text.find(marker_b, start)
        if end < 0:
            raise ValueError("unterminated PEM certificate")
        body = text[start + len(marker_a) : end]
        cert = base64.b64decode("".join(body.split()), validate=True)
        try:
            anchor = parse_cert(cert)
        except ValueError as exc:
            raise ValueError(f"invalid certificate in bundle at offset {start}: {exc}") from exc
        if anchor is not None:
            anchors.append(anchor)
        pos = end + len(marker_b)
    return anchors


def cbytes(name: str, data: bytes) -> str:
    values = ", ".join(f"0x{x:02x}" for x in data)
    return f"static unsigned char {name}[] = {{ {values} }};\n"


def generate(anchors) -> str:
    if not anchors:
        raise ValueError("no supported self-issued CA certificates found")
    out = [
        "/* Generated by scripts/gen_bearssl_trust.py; do not edit. */\n",
        '#include "bearssl.h"\n\n',
    ]
    for i, a in enumerate(anchors):
        out.append(cbytes(f"ta_{i}_dn", a["dn"]))
        if a["type"] == "rsa":
            out.append(cbytes(f"ta_{i}_n", a["n"]))
            out.append(cbytes(f"ta_{i}_e", a["e"]))
        else:
            out.append(cbytes(f"ta_{i}_q", a["q"]))
    out.append("\nconst br_x509_trust_anchor liwus_trust_anchors[] = {\n")
    for i, a in enumerate(anchors):
        if a["type"] == "rsa":
            key = (
                f".pkey = {{ .key_type = BR_KEYTYPE_RSA, .key.rsa = "
                f"{{ ta_{i}_n, sizeof ta_{i}_n, ta_{i}_e, sizeof ta_{i}_e }} }}"
            )
        else:
            key = (
                f".pkey = {{ .key_type = BR_KEYTYPE_EC, .key.ec = "
                f"{{ {a['curve']}, ta_{i}_q, sizeof ta_{i}_q }} }}"
            )
        out.append(
            "    { .dn = { ta_%d_dn, sizeof ta_%d_dn }, .flags = BR_X509_TA_CA, %s },\n"
            % (i, i, key)
        )
    out.append("};\n\nconst size_t liwus_trust_anchors_count =\n    sizeof liwus_trust_anchors / sizeof liwus_trust_anchors[0];\n")
    return "".join(out)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("bundle", type=pathlib.Path)
    parser.add_argument("output", type=pathlib.Path)
    args = parser.parse_args()
    try:
        anchors = read_bundle(args.bundle)
        args.output.write_text(generate(anchors), encoding="ascii")
        print(f"generated {len(anchors)} BearSSL CA trust anchors -> {args.output}")
    except (OSError, ValueError) as exc:
        print(f"trust store generation failed: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
