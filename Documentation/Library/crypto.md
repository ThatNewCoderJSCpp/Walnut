# crypto

`crypto` has hashes (SHA-1, the SHA-2 family and BLAKE2b), password hashing (Argon2id), encryption (XChaCha20-Poly1305), HMAC, key derivation (Argon2id, PBKDF2 and HKDF), encodings (hex, base64, base64url, base32 and URL encoding), secure random bytes, tokens and UUIDs, a constant-time comparison, and checksums. The hashing, HMAC, key derivation, encodings and randomness come from Fizmo's crypto library.

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

`blake2b(data, length = 64, key = "")` gives a BLAKE2b digest of `length` bytes (1 to 64) as hex, and `blake2b_bytes` the raw bytes. With a `key` (up to 64 bytes) it's a keyed hash, which works as a fast MAC. BLAKE2b is faster than SHA-256 and at least as strong.

## Passwords

Never store a password, or a plain hash of one: anyone who gets the hashes can try billions of guesses a second against SHA-256. `hash_password` uses Argon2id, which is built to be slow and to need a lot of memory, so each guess costs an attacker the same time and memory it costs you once.

```walnut
import module.io
import module.crypto

using namespace std::crypto;

function main() -> int {
    string: stored = hash_password("correct horse battery staple");
    std::io::println(stored);
    std::io::println(verify_password("correct horse battery staple", stored), " ", verify_password("guess", stored));
    return 0;
} end function main
```

This prints something like `$argon2id$v=19$m=65536,t=2,p=1$c2FsdHNhbHRzYWx0c2FsdA$...` and then `1 0`. The string holds everything needed to check the password later (a random salt, the settings and the hash), so store it as it is. It's the standard Argon2 format, so other languages' Argon2 libraries can check it too, and Walnut can check theirs.

| Function | Does |
|---|---|
| `hash_password(password, strength = Strength::interactive)` | Hashes with a new random salt, at one of the strength levels below |
| `hash_password(password, settings)` | The same with your own `PasswordSettings` |
| `verify_password(password, stored)` | Whether the password matches; compares in constant time. `false` for anything that isn't a password hash |
| `password_needs_rehash(stored, strength = Strength::interactive)` (or `settings`) | Whether the hash was made with weaker settings than these; after a successful login, hash the password again and store the new one |
| `is_password_hash(text)` | Whether `text` is a well-formed Argon2 hash string |
| `hash_password_with_pepper(password, pepper, strength = Strength::interactive)` (or `settings`) | Like `hash_password`, with a secret pepper mixed in as Argon2's secret key |
| `verify_password_with_pepper(password, stored, pepper)` | Checks a hash made with that pepper |
| `generate_salt(length = 16)` | `length` random bytes (8 to 1024) from the operating system, for `argon2id`, `derive_key` and `pbkdf2` |

**Salts and peppers.** `hash_password` makes a new random salt for every hash and stores it in the result, so two people with the same password get different hashes and nothing about the salt needs handling. Where you pass a salt yourself (`argon2id`, `derive_key`, `pbkdf2`), it has to be at least 8 bytes, and `generate_salt()` makes a good 16-byte one; use a new salt for each password, never a fixed one. A pepper is a secret that isn't stored with the hashes, such as a key kept in a server's configuration, so a stolen database alone can't be attacked. It has to be at least 16 bytes (`generate_key()` makes a 32-byte one), and the same pepper is needed to check the password later: if it's lost, every hash made with it is useless. `verify_password` and `verify_password_with_pepper` refuse stored hashes whose settings are beyond 16 GiB of memory, 1024 passes or 255 lanes, so a planted hash can't make a login use all the server's memory.

**How slow.** `Strength` picks how much memory and time each hash takes. Slower is safer, but a login waits that long too:

| `Strength` | Memory | Passes | Roughly | Use it for |
|---|---|---|---|---|
| `minimum` | 19 MiB | 2 | 0.05 s | The OWASP minimum; a busy server that has to keep logins cheap |
| `interactive` (default) | 64 MiB | 2 | 0.2 s | Logins and anything a person waits for |
| `moderate` | 256 MiB | 3 | 1 s | Rarely used secrets, such as an admin account |
| `sensitive` | 1 GiB | 4 | 4 s | Keys that protect a lot, such as a password manager's master password |

The times are for one core of a typical desktop; measure on the machine that will do the work. For anything in between, start from a level and change it:

```walnut
PasswordSettings: settings = PasswordSettings::of(Strength::interactive);
settings.memory_kib = 131072;
settings.iterations = 3;
string: stored = hash_password(password, settings);
```

| `PasswordSettings` field | Means |
|---|---|
| `memory_kib` | Memory per hash in KiB (at least 8 for each lane of `parallelism`) |
| `iterations` | Passes over that memory (at least 1) |
| `parallelism` | Lanes, worked on by that many threads at once (1 to 255) |
| `hash_length` | Bytes of hash to store (16 to 1024; 32 is plenty) |
| `salt_length` | Bytes of random salt for each hash (8 to 1024; 16 is the default) |

All of the fields are `unsigned long int`.

More memory is the better way to slow down an attacker; raise `iterations` once the memory is as high as you can afford. Settings outside those ranges are a runtime error that says which one.

## Encryption

`encrypt` and `decrypt` use XChaCha20-Poly1305, an authenticated cipher: besides hiding the data, it notices if a single bit was changed and refuses to decrypt it.

```walnut
import module.io
import module.crypto

using namespace std::crypto;

function main() -> int {
    string: key = generate_key();
    string: sealed = encrypt(key, "attack at dawn");
    std::io::println(decrypt(key, sealed));
    string: locked = encrypt_with_password("open sesame", "my diary");
    std::io::println(decrypt_with_password("open sesame", locked));
    return 0;
} end function main
```

| Function | Does |
|---|---|
| `generate_key()` | A new random 32-byte key |
| `encrypt(key, plaintext, associated = "")` | Encrypts with a 32-byte key. Each call picks a new random nonce, so the same message encrypts differently every time. The result is 44 bytes longer than the plaintext |
| `decrypt(key, sealed, associated = "")` | Gives the plaintext back; throws `std::errors::crypto_error` when the key or the associated data is wrong or the data was changed |
| `try_decrypt(key, sealed, plaintext, associated = "")` | The same without throwing: puts the plaintext in `plaintext` and gives whether it worked |
| `encrypt_with_password(password, plaintext, strength = Strength::interactive, associated = "")` (or `settings`) | Derives a key from the password with Argon2id and encrypts with it. The salt and the settings are stored in the result, so only the password is needed to decrypt |
| `decrypt_with_password(password, sealed, associated = "")`, `try_decrypt_with_password(password, sealed, plaintext, associated = "")` | The matching decryption; it reads the settings back from `sealed` |
| `derive_key(password, salt, strength = Strength::interactive)` (or `settings`) | A 32-byte key from a password and a salt of at least 8 bytes, for `encrypt`. The same password and salt always give the same key |
| `argon2id(password, salt, settings, length = 32)` | Raw Argon2id output of any length, for other uses of a password-derived key |
| `argon2id(password, salt, secret, settings, length = 32)` | The same with a secret key (a pepper) mixed in |

**Associated data** is something that isn't secret but belongs with the message, such as a user id or a file name. It isn't stored in the result, but decrypting needs the same value, so a message can't be moved to another user or file without being noticed.

**Keys** have to be secret and exactly 32 bytes; `generate_key()` or `derive_key()` make them. To store or send a key as text, use `to_hex` or `base64_encode`. A key can encrypt as many messages as you like, since the 24-byte random nonces never repeat in practice.

The data is laid out as `FZE1`, the 24-byte nonce, the ciphertext and a 16-byte tag; a password-sealed message adds `FZP1`, the Argon2 settings and the 16-byte salt in front, and all of it is checked by the tag.

## HMAC

`hmac(algorithm, key, data)` gives the raw HMAC and `hmac_hex(algorithm, key, data)` the hex form; `hmac_sha1`, `hmac_sha256` and `hmac_sha512` are shortcuts that give hex. `verify_hmac(algorithm, key, data, expected_hex)` checks an HMAC in constant time. A `Mac`, made with `Mac::of(algorithm, key)`, takes the data in pieces like a `Hasher`.

## Key derivation

For keys made from passwords, prefer `derive_key` or `argon2id` (see [Encryption](#encryption)): PBKDF2 can be computed cheaply on graphics cards. `pbkdf2(algorithm, password, salt, iterations, length)` derives `length` bytes from a password, with SHA-1, SHA-256 or SHA-512; `pbkdf2_sha256(password, salt, iterations, length = 32)` is the common case. The salt has to be at least 8 bytes; `generate_salt()` makes one. `hkdf(algorithm, salt, key, info, length)` and `hkdf_sha256(salt, key, info, length = 32)` are HKDF (RFC 5869), with SHA-256 or SHA-512. Asking for another algorithm throws a `std::errors::crypto_error`.

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
    bool: shorter = std::crypto::strequal("s3cret-token", "s3cret");
    return 0;
} end function main
```

Here `same` is `true`, and `wrong` and `shorter` are `false`.

## Checksums

`crc32(data)`, `adler32(data)` and `fnv1a64(data)` give fast non-cryptographic checksums as an `unsigned long int`. They catch accidental changes, not deliberate ones.
