# SPDX-License-Identifier: GPL-3.0-or-later
"""Pinned build-only replacement of MicroLink's implicit identity generation."""
import hashlib

PINNED_KEYS_SHA = '2f734567a1c3b92273e23560fab0ebdc8698167b14515122b2e87a761dcb22c5'


def strict_identity_source(source):
    start = source.index('static void generate_keypair(')
    end = source.index('\n/* ============================================================================', start)
    if hashlib.sha256(source[start:end].encode()).hexdigest() != PINNED_KEYS_SHA:
        raise ValueError('Pinned identity loader changed; review strict reuse integration')
    if source.count('load_or_generate_keys(ml)') != 1:
        raise ValueError('Pinned identity call changed; review strict reuse integration')
    result = source[:start] + '#include "RetainedKeys.h"\n' + source[end:]
    return result.replace('load_or_generate_keys(ml)', 'booksync_load_retained_keys(ml)')
