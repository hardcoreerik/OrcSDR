#!/usr/bin/env python3
"""Non-mutating HTTP auth/session check for an explicitly supplied P4 address.

This test never changes radio, audio, screen, network, or persistent settings. Supply the
current web password explicitly when it is not the factory password.
"""
import argparse
import hashlib
import hmac
import http.client
import json
import sys


class Device:
    def __init__(self, host, password):
        self.host = host
        self.password = password

    def call(self, method, target, body=b"", headers=None, host=None, origin=None):
        connection = http.client.HTTPConnection(self.host, 80, timeout=5)
        request_headers = {"Host": host or self.host}
        if origin is not None:
            request_headers["Origin"] = origin
        request_headers.update(headers or {})
        connection.request(method, target, body=body, headers=request_headers)
        response = connection.getresponse()
        data = response.read()
        connection.close()
        try:
            return response.status, json.loads(data)
        except (ValueError, TypeError):
            return response.status, data

    def challenge(self):
        status, payload = self.call("GET", "/api/v1/auth/challenge")
        assert status == 200, (status, payload)
        assert payload.get("nonce") and payload.get("salt") and payload.get("iterations")
        return payload

    def login(self):
        challenge = self.challenge()
        key = hashlib.pbkdf2_hmac("sha256", self.password.encode(), bytes.fromhex(challenge["salt"]), challenge["iterations"], 32)
        nonce = bytes.fromhex(challenge["nonce"])
        proof = hmac.new(key, b"login|" + nonce + b"|admin", hashlib.sha256).hexdigest()
        body = ("nonce=%s&proof=%s" % (challenge["nonce"], proof)).encode()
        status, response = self.call("POST", "/api/v1/auth/login", body)
        assert status == 200 and response.get("ok") is True, (status, response)
        expected_device = hmac.new(key, b"device|" + nonce, hashlib.sha256).hexdigest()
        assert response.get("device") == expected_device, "device proof mismatch"
        session_key = hmac.new(key, b"session|" + nonce, hashlib.sha256).digest()
        return Session(self, response["session"], session_key)


class Session:
    def __init__(self, device, session_id, key):
        self.device, self.session_id, self.key = device, session_id, key
        self.sequence = 0

    def signed_headers(self, target, sequence=None, signature_override=None):
        if sequence is None:
            self.sequence += 1
            sequence = self.sequence
        body_hash = hashlib.sha256(b"").hexdigest()
        message = ("%d\nPOST\n%s\n%s" % (sequence, target, body_hash)).encode()
        signature = hmac.new(self.key, message, hashlib.sha256).hexdigest()
        if signature_override is not None:
            signature = signature_override
        return {"X-Orc-Session": self.session_id, "X-Orc-Seq": str(sequence), "X-Orc-Sig": signature}

    def post(self, target, sequence=None, signature_override=None):
        return self.device.call("POST", target, headers=self.signed_headers(target, sequence, signature_override))


def check(name, predicate, detail=None):
    if not predicate:
        print("FAIL %s%s" % (name, (": %r" % (detail,)) if detail is not None else ""))
        return 1
    print("PASS %s" % name)
    return 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("ip")
    parser.add_argument("--password", default="password")
    args = parser.parse_args()
    device = Device(args.ip, args.password)
    failures = 0

    status, response = device.call("POST", "/api/v1/home?action=volume")
    failures += check("unsigned action is rejected before dispatch", status == 401 and response.get("error") == "session_required", (status, response))
    status, response = device.call("GET", "/api/v1/auth/challenge", host="attacker.invalid")
    failures += check("untrusted Host is rejected", status == 403 and response.get("error") == "host_rejected", (status, response))
    status, response = device.call("GET", "/api/v1/auth/challenge", origin="http://attacker.invalid")
    failures += check("cross-site Origin is rejected", status == 403 and response.get("error") == "origin_rejected", (status, response))

    session = device.login()
    status, response = session.post("/api/v1/home?action=volume")
    failures += check("signed device-volume action is refused", status == 400 and response.get("error") == "action_not_allowed", (status, response))
    replay = session.post("/api/v1/home?action=volume", sequence=session.sequence)
    failures += check("replayed sequence is rejected", replay[0] == 401 and replay[1].get("error") == "bad_sequence", replay)
    status, response = session.post("/api/v1/home?action=volume", signature_override="00" * 32)
    failures += check("invalid signature is rejected", status == 401 and response.get("error") == "bad_signature", (status, response))

    status, home = device.call("GET", "/api/v1/home")
    failures += check("Home GET returns the actual read-only state", status == 200 and home.get("ok") is True and "frequency_hz" in home and "band" in home, (status, home))
    status, response = session.post("/api/v1/auth/logout")
    failures += check("signed logout succeeds", status == 200 and response.get("ok") is True, (status, response))
    return failures


if __name__ == "__main__":
    sys.exit(main())
