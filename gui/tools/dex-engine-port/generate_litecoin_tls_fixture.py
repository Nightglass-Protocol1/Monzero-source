#!/usr/bin/env python3
"""Generate disposable test-only TLS material; never production credentials."""
import argparse
import datetime as dt
import hashlib
import json
from pathlib import Path
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.x509.oid import NameOID


def generate(directory):
    directory.mkdir(mode=0o700, parents=False, exist_ok=False)
    now = dt.datetime.now(dt.timezone.utc)

    def authority(name):
        key = ec.generate_private_key(ec.SECP256R1())
        subject = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, name)])
        cert = (x509.CertificateBuilder().subject_name(subject).issuer_name(subject)
                .public_key(key.public_key()).serial_number(x509.random_serial_number())
                .not_valid_before(now - dt.timedelta(days=2))
                .not_valid_after(now + dt.timedelta(days=14))
                .add_extension(x509.BasicConstraints(ca=True, path_length=0), critical=True)
                .add_extension(x509.SubjectKeyIdentifier.from_public_key(key.public_key()), critical=False)
                .add_extension(x509.AuthorityKeyIdentifier.from_issuer_public_key(key.public_key()), critical=False)
                .add_extension(x509.KeyUsage(False, False, False, False, False,
                                            True, True, False, False), critical=True)
                .sign(key, hashes.SHA256()))
        return key, cert

    key, root = authority('Monzero disposable TLS fixture CA')
    _, other = authority('Monzero unrelated TLS fixture CA')
    server_key = ec.generate_private_key(ec.SECP256R1())

    def leaf(expired):
        return (x509.CertificateBuilder()
                .subject_name(x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, 'localhost')]))
                .issuer_name(root.subject).public_key(server_key.public_key())
                .serial_number(x509.random_serial_number())
                .not_valid_before(now - dt.timedelta(days=2))
                .not_valid_after(now + dt.timedelta(days=-1 if expired else 7))
                .add_extension(x509.BasicConstraints(ca=False, path_length=None), critical=True)
                .add_extension(x509.SubjectKeyIdentifier.from_public_key(server_key.public_key()), critical=False)
                .add_extension(x509.AuthorityKeyIdentifier.from_issuer_public_key(key.public_key()), critical=False)
                .add_extension(x509.SubjectAlternativeName([x509.DNSName('localhost')]), critical=False)
                .add_extension(x509.ExtendedKeyUsage([x509.oid.ExtendedKeyUsageOID.SERVER_AUTH]), critical=False)
                .sign(key, hashes.SHA256()))

    data = {'root.pem': root.public_bytes(serialization.Encoding.PEM),
            'other-root.pem': other.public_bytes(serialization.Encoding.PEM),
            'empty.pem': b'',
            'root.der': root.public_bytes(serialization.Encoding.DER),
            'other-root.der': other.public_bytes(serialization.Encoding.DER),
            'server.der': leaf(False).public_bytes(serialization.Encoding.DER),
            'expired.der': leaf(True).public_bytes(serialization.Encoding.DER),
            'server-key.der': server_key.private_bytes(serialization.Encoding.DER,
                serialization.PrivateFormat.PKCS8, serialization.NoEncryption())}
    for name, value in data.items():
        path = directory / name
        with path.open('xb') as output:
            path.chmod(0o600)
            output.write(value)
    (directory / 'manifest.json').write_text(json.dumps({
        'purpose': 'disposable local TLS tests only', 'created': now.isoformat(),
        'sha256': {name: hashlib.sha256(value).hexdigest() for name, value in data.items()}
    }, indent=2) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('directory', type=Path)
    generate(parser.parse_args().directory)
