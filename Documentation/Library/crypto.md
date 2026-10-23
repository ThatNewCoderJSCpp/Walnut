# crypto

`crypto` has hashes (SHA-1 and the SHA-2 family), HMAC, key derivation (PBKDF2 and HKDF), encodings (hex, base64, base64url, base32 and URL encoding), secure random bytes, tokens and UUIDs, a constant-time comparison, and checksums. The hashing, HMAC, key derivation, encodings and randomness come from Fizmo's crypto library.

```walnut
import module.crypto
```

Everything is in the namespace `std::crypto`. Bytes are held in a `string`, which can hold any byte, including zero.

## A first example

```walnut
import module.io
import module.crypto

function main() -> int {
    std::io::println(std::crypto::sha256("abc"));
    std::io::println(std::crypto::hmac_sha256("key", "The quick brown fox jumps over the lazy dog"));
    std::io::println(std::crypto::base64_encode("hello"), " ", std::crypto::base64_decode("aGVsbG8="));
    std::io::println(std::crypto::crc32("123456789"));
    return 0;
} end function main
```

This prints:

```
ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8
aGVsbG8= hello
3421780262
```

## Hashes

`sha1(data)`, `sha224(data)`, `sha256(data)`, `sha384(data)` and `sha512(data)` give the digest as lowercase hex. The `_bytes` forms, such as `sha256_bytes(data)`, give the raw digest instead.

The `Algorithm` enum (`sha1`, `sha224`, `sha256`, `sha384`, `sha512`) picks one at run time: `hash(algorithm, data)` gives raw bytes and `hash_hex(algorithm, data)` gives hex.

A `Hasher` hashes data that arrives in pieces:

```walnut
import module.io
import module.crypto

function main() -> int {
    auto: h = std::crypto::Hasher::of(std::crypto::Algorithm::sha256);
    h.update("a");
    h.update("bc");
    std::io::println(h.bytes_hashed(), " ", h.finish_hex() == std::crypto::sha256("abc"));
    return 0;
} end function main
```

This prints `3 1`. A `Hasher` has `update(data)`, `finish()` (raw bytes), `finish_hex()`, `reset()` and `bytes_hashed()`; `finish` starts it over.

## HMAC

`hmac(algorithm, key, data)` gives the raw HMAC and `hmac_hex(algorithm, key, data)` the hex form; `hmac_sha1`, `hmac_sha256` and `hmac_sha512` are shortcuts that give hex. `verify_hmac(algorithm, key, data, expected_hex)` checks an HMAC in constant time. A `Mac`, made with `Mac::of(algorithm, key)`, takes the data in pieces like a `Hasher`.

## Key derivation

`pbkdf2(algorithm, password, salt, iterations, length)` derives `length` bytes from a password, with SHA-1, SHA-256 or SHA-512; `pbkdf2_sha256(password, salt, iterations, length = 32)` is the common case. `hkdf(algorithm, salt, key, info, length)` and `hkdf_sha256(salt, key, info, length = 32)` are HKDF (RFC 5869), with SHA-256 or SHA-512. Asking for another algorithm throws a `std::errors::crypto_error`.

## Encodings

| Function | Does |
|---|---|
| `to_hex(data, upper = false)`, `from_hex(text)`, `is_hex(text)` | Hexadecimal |
| `base64_encode(data, pad = true)`, `base64_decode(text)` | Base64 |
| `base64url_encode(data, pad = false)`, `base64url_decode(text)` | Base64 with the URL-safe alphabet |
| `base32_encode(data, pad = true)`, `base32_decode(text)` | Base32 (RFC 4648) |
| `url_encode(text, space_as_plus = false)`, `url_decode(text, plus_as_space = false)` | Percent-encoding |

Decoding text that isn't in the right form is a runtime error that says so.

## Randomness and comparison

`random_bytes(count)` gives bytes from the operating system's secure random source. `random_token(bytes = 32)` gives that many random bytes as hex, `random_token_base64url(bytes = 32)` as base64url, and `uuid_v4()` a random UUID such as `0f8fad5b-d9cb-469f-a165-70867728950e`.

`secure_equals(a, b)` compares two strings in time that doesn't depend on where they differ, which is how secrets such as tokens and MACs should be compared. It still answers at once when the lengths differ.

`strequal(a, b)` goes further: it never stops early. It goes through every character of the longer string, notes any difference (including a difference in length) and only gives its answer at the end, so the time it takes depends only on the longer length. Use it to check a password or token typed by a user against the real one.

```walnut
import module.crypto

function main() -> int {
    bool: same = std::crypto::strequal("s3cret-token", "s3cret-token");
    bool: wrong = std::crypto::strequal("s3cret-token", "s3cret-tokem");
    bool: short = std::crypto::strequal("s3cret-token", "s3cret");
    return 0;
} end function main
```

Here `same` is `true`, and `wrong` and `short` are `false`.

## Checksums

`crc32(data)`, `adler32(data)` and `fnv1a64(data)` give fast non-cryptographic checksums as an `unsigned long int`. They catch accidental changes, not deliberate ones.
