"""Independent oracle: no VEIL imports, unsigned states checked with integer division."""
states = [0, 7, 23, 24, 0xFFFFFFFF, 0xABEB47EE]
expected = [0, 7, 23, 0, 15, 22]
for state, index in zip(states, expected):
    quotient, remainder = divmod(state, 24)
    assert state == quotient * 24 + index and remainder == index
assert divmod(2**32, 24) == (178956970, 16)
assert divmod(2**32, 26) == (165191049, 22)
print("Dead Air rune independent vectors PASS")
