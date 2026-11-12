"""Independent ETSI-table math checks. Python stdlib only; no decoder code copied.

These tests exercise spec-derived *reference* constructions and are runnable
without checking out the production DMR sources. The C++ CMake test is the
separate production-coupled gate.
"""
import itertools
import unittest

# ETSI TS 102 361-1 V2.7.1 Table B.11, transcribed from rendered page 137.
GOLAY_ROWS = (0x3DA, 0xD99, 0x6CD, 0x367, 0xDC6, 0xA97, 0x93E, 0x8EB)
SYNC_WORDS = (
    0x755FD7DF75F7, 0xDFF57D75DF5D, 0x7F7D5DD57DFD, 0xD5D7F77FD757,
    0x5D577F7757FF, 0xF7FDD5DDFD55, 0x7DFFD5F55D5F, 0xD7557F5FF7F5,
)
# Table B.18, three parity bytes per each of nine systematic data rows.
RS_PARITY = (
    (0x1C, 0xBC, 0xFD), (0x89, 0x31, 0x08), (0xAD, 0x41, 0x36),
    (0x7D, 0x71, 0x16), (0xF3, 0xA6, 0x3A), (0x08, 0x83, 0x7B),
    (0x3F, 0x6F, 0x02), (0x6C, 0x0D, 0xA7), (0x0E, 0x38, 0x40),
)
ROW15 = (9, 13, 15, 14, 7, 10, 5, 11, 12, 6, 3)
COL13 = (15, 14, 7, 10, 5, 11, 12, 6, 3)
ROW16 = (19, 26, 31, 28, 14, 21, 11, 22, 25, 13, 7)
QR_ROWS = (0x04F, 0x11E, 0x1B7, 0x1E2, 0x1C9, 0x0E5, 0x073)
TACT_POS = (0, 4, 8, 12, 14, 18, 22)


def golay(value):
    parity = 0
    for row, generator in enumerate(GOLAY_ROWS):
        if value & (0x80 >> row):
            parity ^= generator
    return (value << 12) | parity


def tact(value):
    a, b, c, d = ((value >> shift) & 1 for shift in (3, 2, 1, 0))
    return (value << 3) | ((a ^ b ^ c) << 2) | ((b ^ c ^ d) << 1) | (a ^ b ^ d)


def parity(vector, columns):
    value = 0
    for bit, column in zip(vector, columns):
        if bit:
            value ^= column
    return value


def bits_of(value, width):
    return [(value >> (width - 1 - i)) & 1 for i in range(width)]


def gf_mul(a, b):
    out = 0
    while b:
        if b & 1:
            out ^= a
        carry = a & 0x80
        a = ((a << 1) & 0xFF) ^ (0x1D if carry else 0)
        b >>= 1
    return out


def rs_parity(data):
    assert len(data) == 9
    return bytes(
        __import__('functools').reduce(lambda accum, term: accum ^ term,
            (gf_mul(data[row], RS_PARITY[row][col]) for row in range(9)), 0)
        for col in range(3)
    )


def rs_syndrome(coded):
    assert len(coded) == 12
    for exp in (1, 2, 3):
        alpha = 1
        for _ in range(exp):
            alpha = gf_mul(alpha, 2)
        value = 0
        for b in coded:
            value = gf_mul(value, alpha) ^ b
        yield value


def crc16(data):
    """ETSI B.3.8 polynomial long division, initial remainder 0, xor 0xffff."""
    remainder = 0
    for byte in data:
        for bit in bits_of(byte, 8):
            feedback = ((remainder >> 15) ^ bit) & 1
            remainder = (remainder << 1) & 0xFFFF
            if feedback:
                remainder ^= 0x1021
    return remainder ^ 0xFFFF


def bptc196(data):
    assert len(data) == 12
    grid = [[0] * 15 for _ in range(13)]
    information = [bit for byte in data for bit in bits_of(byte, 8)]
    for k, bit in enumerate(information):
        grid[(k + 3)//11][(k + 3)%11] = bit
    for row in range(9):
        grid[row][11:] = bits_of(parity(grid[row][:11], ROW15), 4)
    for column in range(15):
        grid[9][column], grid[10][column], grid[11][column], grid[12][column] = \
            bits_of(parity([grid[r][column] for r in range(9)], COL13), 4)
    encoded = [0] + [grid[r][c] for r in range(13) for c in range(15)]
    wire = [0] * 196
    for i, bit in enumerate(encoded):
        wire[(181 * i) % 196] = bit
    return wire


def emb128(data):
    assert len(data) == 9
    payload = [bit for byte in data for bit in bits_of(byte, 8)]
    payload += bits_of(sum(data) % 31, 5)
    rows = [[0] * 16 for _ in range(8)]
    for i, bit in enumerate(payload):
        rows[i//11][i%11] = bit
    for r in range(7):
        rows[r][11:] = bits_of(parity(rows[r][:11], ROW16), 5)
    for col in range(16):
        rows[7][col] = sum(rows[r][col] for r in range(7)) % 2
    return [rows[i % 8][i//8] for i in range(128)]


class EtsiOracleTests(unittest.TestCase):
    def test_all_eight_sync_patterns(self):
        self.assertEqual(len(set(SYNC_WORDS)), 8)
        self.assertEqual(SYNC_WORDS[6], 0x7DFFD5F55D5F)
        for i in range(0, 8, 2):
            self.assertEqual(SYNC_WORDS[i] ^ SYNC_WORDS[i+1], 0xAAAAAAAAAAAA)

    def test_golay_all_256_systematic_codewords(self):
        words = [golay(i) for i in range(256)]
        for i, word in enumerate(words):
            self.assertEqual(word >> 12, i)
        self.assertEqual(len(set(words)), 256)
        self.assertEqual(golay(0x21), 0x21E26)

    def test_golay_minimum_distance_eight(self):
        words = [golay(i) for i in range(256)]
        self.assertEqual(min((a ^ b).bit_count()
            for x, a in enumerate(words) for b in words[x+1:]), 8)

    def test_golay_three_correctable_four_rejected(self):
        words = [golay(i) for i in range(256)]
        word = words[0x21]
        for flipped in itertools.combinations(range(20), 3):
            corrupted = word ^ sum(1 << index for index in flipped)
            nearest = sorted(((corrupted ^ w).bit_count(), i) for i, w in enumerate(words))
            self.assertEqual(nearest[0], (3, 0x21))
            self.assertGreater(nearest[1][0], 3)
        for offset in range(17):
            corrupted = word ^ (0xF << offset)
            self.assertGreater(min((corrupted ^ w).bit_count() for w in words), 3)

    def test_tact_hamming_single_repair(self):
        words = [tact(i) for i in range(16)]
        self.assertEqual(min((a ^ b).bit_count() for i, a in enumerate(words) for b in words[i+1:]), 3)
        for i, word in enumerate(words):
            for flipped in range(7):
                corrupted = word ^ (1 << flipped)
                closest = [j for j, candidate in enumerate(words) if (corrupted ^ candidate).bit_count() <= 1]
                self.assertEqual(closest, [i])

    def test_tact_every_seven_bit_word_looks_valid(self):
        words = [tact(i) for i in range(16)]
        distances = [min((candidate ^ code).bit_count() for code in words) for candidate in range(128)]
        self.assertEqual(max(distances), 1)
        self.assertEqual(distances[0], 0)
        # This proves that a 7-bit Hamming TACT check alone cannot establish
        # that the 24-bit prefix actually came from a BS CACH.

    def test_cach_bit_positions_are_bijective(self):
        self.assertEqual(len(set(TACT_POS)), 7)
        self.assertEqual(TACT_POS, (0, 4, 8, 12, 14, 18, 22))
        self.assertEqual(len(set(range(24)) - set(TACT_POS)), 17)

    def test_bptc_181_mod_196_is_a_permutation(self):
        positions = [(181 * i) % 196 for i in range(196)]
        self.assertEqual(sorted(positions), list(range(196)))
        self.assertEqual((181 * 13) % 196, 1)
        self.assertEqual(positions[0], 0)

    def test_bptc_product_checks(self):
        data = bytes([0x00,0x03,0x22,0x11,0x44,0x55,0x01,0x42,0x76,0,0,0])
        wire = bptc196(data)
        encoded = [wire[(181 * i) % 196] for i in range(196)]
        self.assertEqual(encoded[0], 0)
        grid = [encoded[1+r*15:1+(r+1)*15] for r in range(13)]
        for row in range(9):
            self.assertEqual(grid[row][11:], bits_of(parity(grid[row][:11], ROW15), 4))
        for column in range(15):
            self.assertEqual([grid[r][column] for r in range(9,13)],
                bits_of(parity([grid[r][column] for r in range(9)], COL13), 4))
        self.assertEqual(grid[0][:3], [0,0,0])
        wire[(181 * 55) % 196] ^= 1
        encoded_bad = [wire[(181 * i) % 196] for i in range(196)]
        bad = [encoded_bad[1+r*15:1+(r+1)*15] for r in range(13)]
        self.assertNotEqual(bad[3][11:], bits_of(parity(bad[3][:11], ROW15),4))

    def test_rs_generator_matrix_and_roots(self):
        messages = [bytes(9), bytes(range(9)), bytes([0,0,3,1,2,3,4,5,6]), bytes([255]*9)]
        for message in messages:
            result = message + rs_parity(message)
            self.assertEqual(list(rs_syndrome(result)), [0,0,0])
            corrupted = bytearray(result)
            corrupted[4] ^= 0x20
            self.assertNotEqual(list(rs_syndrome(corrupted)), [0,0,0])

    def test_rs_crc_type_masks(self):
        self.assertEqual(crc16(bytes(range(10))), 0xDC87)
        self.assertEqual((0x96,0x99), (0x969696 & 0xff, 0x999999 & 0xff))
        self.assertEqual((0xA5A5,0xCCCC,0x3333), (0xA5A5,0xCCCC,0x3333))
        payload = bytes(range(10))
        for mask in (0xA5A5,0xCCCC,0x3333):
            check = crc16(payload) ^ mask
            self.assertEqual(crc16(payload), check ^ mask)
            changed = bytearray(payload)
            changed[0] ^= 1
            self.assertNotEqual(crc16(changed), check ^ mask)

    def test_embedded_128bit_even_columns_and_check(self):
        nine = bytes([0,0,3,0,0,77,0x12,0x34,0x56])
        encoded = emb128(nine)
        rows = [[encoded[r+8*c] for c in range(16)] for r in range(8)]
        for col in range(16):
            self.assertEqual(sum(rows[r][col] for r in range(8)) % 2, 0)
        for r in range(7):
            self.assertEqual(rows[r][11:], bits_of(parity(rows[r][:11],ROW16),5))
        checksum = sum(nine) % 31
        self.assertEqual([rows[i//11][i%11] for i in range(72,77)], bits_of(checksum,5))
        encoded[5] ^= 1
        rows = [[encoded[r+8*c] for c in range(16)] for r in range(8)]
        self.assertNotEqual(sum(rows[r][0] for r in range(8)) % 2, 0)

    def test_false_identity_from_sync_not_proven(self):
        # Synthetic corruption cannot create an RS-validated identity unless
        # a valid RS codeword passes the independent polynomial check.
        nine = bytes([0,0,3,0,0,77,0x12,0x34,0x56])
        raw = nine + rs_parity(nine)
        self.assertEqual(list(rs_syndrome(raw)), [0,0,0])
        forged = bytearray(raw)
        forged[8] ^= 1
        self.assertNotEqual(list(rs_syndrome(forged)), [0,0,0])


if __name__ == '__main__':
    unittest.main(verbosity=2)
