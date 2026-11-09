#!/usr/bin/env python3
"""Reference client and test run for the web login and signed control requests (docs/hardware/WEB_LAYER_DESIGN.md section 14).

  python tools/p4_web_auth_test.py <device-ip> [--password PASSWORD]

Needs only the standard library. Uses the factory password unless --password is given. It sets the tuned frequency and volume to values it
reads first and restores them, but it does retune the radio briefly: run it on a bench unit nobody is listening to.
Every check prints PASS or FAIL; the exit code is the number of failures.
"""
import argparse
import hashlib
import hmac
import http.client
import json
import sys
import time


class Device:
    def __init__(self, ip, password):
        self.ip, self.password = ip, password

    def call(self, method, target, body=b"", headers=None, host=None):
        conn = http.client.HTTPConnection(self.ip, 80, timeout=8)
        h = {"Host": host or self.ip}
        h.update(headers or {})
        conn.request(method, target, body=body, headers=h)
        r = conn.getresponse()
        data = r.read()
        conn.close()
        try:
            return r.status, json.loads(data)
        except ValueError:
            return r.status, data

    def challenge(self):
        status, j = self.call("GET", "/api/v1/auth/challenge")
        assert status == 200, (status, j)
        return j

    @staticmethod
    def hm(key, *parts):
        return hmac.new(key, b"".join(parts), hashlib.sha256).digest()

    def key_for(self, j, password=None):
        return hashlib.pbkdf2_hmac("sha256", (password or self.password).encode(), bytes.fromhex(j["salt"]), j["iterations"], 32)

    def login_body(self, j, key):
        nonce = bytes.fromhex(j["nonce"])
        return ("nonce=%s&proof=%s" % (j["nonce"], self.hm(key, b"login|", nonce, b"|admin").hex())).encode()

    def login(self, password=None):
        j = self.challenge()
        key = self.key_for(j, password)
        status, r = self.call("POST", "/api/v1/auth/login", self.login_body(j, key))
        if status != 200:
            return status, r, None
        nonce = bytes.fromhex(j["nonce"])
        assert r["device"] == self.hm(key, b"device|", nonce).hex(), "device proof does not verify"
        return status, r, Session(self, r["session"], self.hm(key, b"session|", nonce))


class Session:
    def __init__(self, dev, sid, key):
        self.dev, self.sid, self.key, self.seq = dev, sid, key, 0

    def sig(self, seq, method, target, body):
        msg = ("%d\n%s\n%s\n%s" % (seq, method, target, hashlib.sha256(body).hexdigest())).encode()
        return hmac.new(self.key, msg, hashlib.sha256).hexdigest()

    def headers(self, seq, method, target, body):
        return {"X-Orc-Session": self.sid, "X-Orc-Seq": str(seq), "X-Orc-Sig": self.sig(seq, method, target, body)}

    def post(self, target, body=b"", seq=None, sign_target=None, sign_body=None):
        if seq is None:
            self.seq += 1
            seq = self.seq
        h = self.headers(seq, "POST", sign_target or target, body if sign_body is None else sign_body)
        return self.dev.call("POST", target, body, h)


fails = 0


def check(name, ok, detail=""):
    global fails
    print(("PASS " if ok else "FAIL ") + name + ((" - " + str(detail)) if detail and not ok else ""))
    fails += 0 if ok else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ip")
    ap.add_argument("--password", default="password")
    args = ap.parse_args()
    d = Device(args.ip, args.password)

    status, before = d.call("GET", "/api/v1/freq")
    freq = before["records"][0]["frequency_hz"]
    status, v0 = d.call("GET", "/api/v1/volume")
    volume = v0["records"][0]["volume"]
    check("read tier still open", status == 200)

    status, r = d.call("POST", "/api/v1/freq_set?hz=100000000", b"")
    check("control without a session is refused", status == 401 and r.get("error") == "session_required", (status, r))
    status, r = d.call("GET", "/api/v1/freq_set?hz=100000000")
    check("control over GET is refused", status == 401, (status, r))

    j = d.challenge()
    check("challenge carries salt, iterations and the default-password flag", len(j["nonce"]) == 32 and len(j["salt"]) == 32 and j["iterations"] == 10000 and "default_password" in j, j)
    status, r = d.call("POST", "/api/v1/auth/login", d.login_body(j, d.key_for(j, "wrong-password")))
    check("wrong password is refused", status == 401 and r.get("error") == "login_failed", (status, r))
    status, r = d.call("POST", "/api/v1/auth/login", d.login_body(j, d.key_for(j)))
    check("a used nonce cannot be used again", status == 401 and r.get("error") in ("bad_nonce", "login_failed") or status == 429, (status, r))
    wait = 0
    if status == 429:
        wait = 3
    time.sleep(2.5 + wait)

    status, r, s = d.login()
    check("login with the right password", status == 200 and s is not None, (status, r))
    if not s:
        print("cannot continue without a session")
        return fails + 1
    check("device proof verified by the client", True)

    status, r = s.post("/api/v1/freq_set?hz=%d" % freq)
    check("signed control request is accepted", status == 200 and r.get("ok"), (status, r))
    replay_seq = s.seq
    status, r = s.post("/api/v1/freq_set?hz=%d" % freq, seq=replay_seq)
    check("replaying the same sequence number is refused", status == 401 and r.get("error") == "bad_sequence", (status, r))
    status, r = s.post("/api/v1/freq_set?hz=%d" % freq, seq=replay_seq - 1)
    check("a lower sequence number is refused", status == 401, (status, r))
    status, r = s.post("/api/v1/freq_set?hz=999999999", sign_target="/api/v1/freq_set?hz=%d" % freq)
    check("changing the target after signing is refused", status == 401 and r.get("error") == "bad_signature", (status, r))
    status, r = s.post("/api/v1/volume_set?level=5", body=b"x", sign_body=b"")
    check("changing the body after signing is refused", status == 401 and r.get("error") == "bad_signature", (status, r))
    status, r = s.post("/api/v1/volume_set?level=99")
    check("out-of-range value is rejected as invalid", status == 400, (status, r))
    status, r = s.post("/api/v1/freq_set?hz=5")
    check("below-range frequency is rejected", status == 400, (status, r))
    status, r = s.post("/api/v1/mem")
    check("a read command over POST is not a control route", status == 404, (status, r))
    status, r = s.post("/api/v1/volume_set?level=%d" % volume)
    check("volume set accepted", status == 200 and r.get("ok"), (status, r))
    status, r = d.call("POST", "/api/v1/freq_set?hz=%d" % freq, b"", s.headers(s.seq + 1, "POST", "/api/v1/freq_set?hz=%d" % freq, b""), host="evil.example")
    check("a foreign Host header is refused", status == 403, (status, r))
    status, r = d.call("POST", "/api/v1/freq_set?hz=%d" % freq, b"", dict(s.headers(s.seq + 1, "POST", "/api/v1/freq_set?hz=%d" % freq, b""), Origin="http://evil.example"))
    check("a foreign Origin is refused", status == 403, (status, r))
    status, r = d.call("POST", "/api/v1/auth/login", b"x" * 400)
    check("an oversized body is refused", status == 413, (status, r))

    status, after = d.call("GET", "/api/v1/freq")
    check("tuning is back where it started", after["records"][0]["frequency_hz"] == freq, after)

    status, r = s.post("/api/v1/auth/logout")
    check("logout", status == 200, (status, r))
    status, r = s.post("/api/v1/freq_set?hz=%d" % freq)
    check("the session is gone after logout", status == 401 and r.get("error") == "session_required", (status, r))

    # Throttle: three wrong passwords in a row must slow the fourth attempt down.
    time.sleep(31)
    codes = []
    for _ in range(4):
        status, r, _s = d.login("wrong-%d" % len(codes))
        codes.append(status)
    check("repeated failures are throttled", 429 in codes, codes)
    time.sleep(31)
    status, r, s2 = d.login()
    check("the right password works again after the back-off", status == 200, (status, r))
    if s2:
        s2.post("/api/v1/auth/logout")

    status, w = d.call("GET", "/api/v1/web")
    rec = w["records"][0]
    print("status record:", rec)
    print("%d failed" % fails)
    return fails


if __name__ == "__main__":
    sys.exit(main())
