# Third-party components in Light v0.7

The custom kernel remains SHENLONG. These libraries run in the guest.

## BearSSL 0.6

- Source: https://bearssl.org/bearssl-0.6.tar.gz
- Author: Thomas Pornin. MIT license: `vendor/bearssl-0.6/LICENSE.txt`.
- Vendored sources are unchanged. The freestanding build disables OS entropy,
  OS clocks and intrinsics; `kernel/tls.c` supplies RDRAND entropy and firmware
  time, limits negotiation to TLS 1.2 ECDHE/AEAD suites, and validates X.509.
- `kernel/libc/` and `kernel/libc_compat.c` provide freestanding compatibility.

## Root certificates

- Source: https://curl.se/ca/cacert.pem (Mozilla extraction, 2026-11-05).
- Stored verbatim in `vendor/cacert.pem`; 118 roots.
- SHA-256: `427c064216761a1e72a8b30f0d954d08cb6e4435a82091fb865afd20c8497285`.
- Generated header: `kernel/tls_roots.h`. Reproduce from the checked-in bundle:

```sh
make -C vendor/bearssl-0.6 -j4 CC=clang
vendor/bearssl-0.6/build/brssl ta vendor/cacert.pem > kernel/tls_roots.h
```

Do not replace the roots with an unverified download during a normal build.

## PL_MPEG

- Source: https://github.com/phoboslab/pl_mpeg/blob/master/pl_mpeg.h
- Retrieved 2026-11-07. Author: Dominic Szablewski, https://phoboslab.org.
- Upstream header declares `SPDX-License-Identifier: MIT` and the author.
  The complete header, including that notice, is included in the ISO.
- Local modification: replace left-shifting a potentially negative DC
  predictor with multiplication by 256 (same values, defined C behavior).
- Modified header SHA-256:
  `d5eeb1c70649772011f77c3aa02ff9dab97d6bf2328ffa7e2aa779cb33e48b59`.
- The guest wrapper bounds the file, dimensions and allocation arena. It is
  an experimental decoder, without process isolation or a general codec API.

## Test media

`assets/media/demo.mpg` is an original procedural test pattern and sine tone,
generated with FFmpeg. No downloaded film, music or YouTube media is bundled.
FFmpeg is used only to prepare this asset, not during guest playback.

```sh
ffmpeg -f lavfi -i testsrc2=size=640x360:rate=25 \
  -f lavfi -i sine=frequency=440:sample_rate=44100 -t 4 \
  -c:v mpeg1video -q:v 4 -c:a mp2 -b:a 128k -ac 2 \
  -f mpeg assets/media/demo.mpg
```

The logo is the user's supplied artwork. Font sources and OFL licenses are
in `assets/fonts/`. Limine retains its own notices under `limine/`.
