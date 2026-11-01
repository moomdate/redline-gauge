"""Ed25519 (RFC 8032) in pure Python, standard library only.

Here so that signing, and above all VERIFYING, needs nothing installed: a third party
(or a court) can re-run tools/sign/verify.py with a stock Python and reach the same result,
which is what makes the proof reproducible. Adapted from the public-domain reference
implementation by Daniel J. Bernstein et al. Not constant-time; that is fine for signing
our own builds and verifying someone else's firmware offline.
"""
import hashlib

b = 256
q = 2 ** 255 - 19
L = 2 ** 252 + 27742317777372353535851937790883648493


def _H(m):
    return hashlib.sha512(m).digest()


def _inv(x):
    return pow(x, q - 2, q)


d = -121665 * _inv(121666) % q
I = pow(2, (q - 1) // 4, q)


def _xrecover(y):
    xx = (y * y - 1) * _inv(d * y * y + 1)
    x = pow(xx, (q + 3) // 8, q)
    if (x * x - xx) % q != 0:
        x = (x * I) % q
    if x % 2 != 0:
        x = q - x
    return x


By = 4 * _inv(5) % q
Bx = _xrecover(By)
B = [Bx % q, By % q]


def _edwards(P, Q):
    x1, y1 = P
    x2, y2 = Q
    x3 = (x1 * y2 + x2 * y1) * _inv(1 + d * x1 * x2 * y1 * y2)
    y3 = (y1 * y2 + x1 * x2) * _inv(1 - d * x1 * x2 * y1 * y2)
    return [x3 % q, y3 % q]


def _scalarmult(P, e):
    if e == 0:
        return [0, 1]
    Q = _scalarmult(P, e // 2)
    Q = _edwards(Q, Q)
    if e & 1:
        Q = _edwards(Q, P)
    return Q


def _bit(h, i):
    return (h[i // 8] >> (i % 8)) & 1


def _encodeint(y):
    return bytes((y >> (8 * i)) & 0xFF for i in range(b // 8))


def _encodepoint(P):
    x, y = P
    bits = [(y >> i) & 1 for i in range(b - 1)] + [x & 1]
    return bytes(sum(bits[i * 8 + j] << j for j in range(8)) for i in range(b // 8))


def _Hint(m):
    h = _H(m)
    return sum(2 ** i * _bit(h, i) for i in range(2 * b))


def publickey(sk):
    """32-byte public key from a 32-byte secret seed."""
    h = _H(sk)
    a = 2 ** (b - 2) + sum(2 ** i * _bit(h, i) for i in range(3, b - 2))
    A = _scalarmult(B, a)
    return _encodepoint(A)


def sign(msg, sk, pk=None):
    """64-byte signature over msg (bytes) with 32-byte seed sk."""
    if pk is None:
        pk = publickey(sk)
    h = _H(sk)
    a = 2 ** (b - 2) + sum(2 ** i * _bit(h, i) for i in range(3, b - 2))
    r = _Hint(h[b // 8:b // 4] + msg)
    R = _scalarmult(B, r)
    S = (r + _Hint(_encodepoint(R) + pk + msg) * a) % L
    return _encodepoint(R) + _encodeint(S)


def _decodeint(s):
    return sum(2 ** i * _bit(s, i) for i in range(0, b))


def _isoncurve(P):
    x, y = P
    return (-x * x + y * y - 1 - d * x * x * y * y) % q == 0


def _decodepoint(s):
    y = sum(2 ** i * _bit(s, i) for i in range(0, b - 1))
    x = _xrecover(y)
    if x & 1 != _bit(s, b - 1):
        x = q - x
    P = [x, y]
    if not _isoncurve(P):
        raise ValueError("decoding point that is not on curve")
    return P


def verify(sig, msg, pk):
    """True if sig (64 bytes) is a valid signature over msg under pk (32 bytes)."""
    try:
        if len(sig) != b // 4 or len(pk) != b // 8:
            return False
        R = _decodepoint(sig[0:b // 8])
        A = _decodepoint(pk)
        S = _decodeint(sig[b // 8:b // 4])
        h = _Hint(_encodepoint(R) + pk + msg)
        return _scalarmult(B, S) == _edwards(R, _scalarmult(A, h))
    except Exception:
        return False
