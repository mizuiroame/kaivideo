// MD4 (RFC 1320), only for the NT hash an SMB password is kept as (dav.c):
// the hash is what NTLM signs in with, so the password itself never needs
// to be on the card. MD4 is broken as a digest; it is used here only
// because NTLM is defined with it.
#pragma once

#include <stddef.h>
#include <stdint.h>

#define MD4_DIGEST_SIZE 16

void md4(const void *data, size_t size, uint8_t out[MD4_DIGEST_SIZE]);
