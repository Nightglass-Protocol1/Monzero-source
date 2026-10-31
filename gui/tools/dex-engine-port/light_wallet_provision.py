#!/usr/bin/env python3
"""Provision or refresh a disposable test-network Litecoin or Bitcoin light wallet through native-ltc-light.

Standard library only. Mirrors create_light_wallet()/verify_light_refund() in
litecoin_app_light_fixture.py (same setup, request, launcher schemas and scan limits) but
runs on the machine that will own the wallet and returns only filtered public fields:

  create:  address, helperSha256, configurationFile, launcher
  refresh: observedConfirmedAtomic, pendingReceivedAtomic

The generated password stays in DIRECTORY/light-password (0600). Seeds, passwords and raw
helper output are never returned or printed; helper failures are reported as fixed messages.
"""
import argparse
import hashlib
import hmac
import json
import os
import re
import secrets
import selectors
import signal
import stat
import subprocess
import sys
import time
import uuid

# Light-wallet network names per coin, as the helper's setup files spell them.
COIN_NETWORKS = {'ltc': ('regtest', 'test'), 'btc': ('regtest', 'testnet4', 'signet')}
NETWORKS = COIN_NETWORKS['ltc']


def setup_schema(coin):
    return f'monzero-private-{coin}-setup-v1'


def launcher_schema(coin):
    return f'monzero-private-{coin}-light-launcher-v1'
SEED_ENCODING = 'bip39-english-v1'
STORAGE_KEYS = ('setupDirectory', 'vaultDirectory', 'addressBookDirectory',
                'backupDirectory', 'spendDirectory', 'cacheDirectory')
WALLET_SCAN = dict(gap=2, addressesPerBranch=3, historyEntriesPerBranch=50)
MAXIMUM_HEIGHT_LIMIT = 10_000_000
TIMEOUT_SECONDS = 240
OUTPUT_LIMIT = 1 << 20
LAUNCHER_LIMIT = 1 << 16
ADDRESS = re.compile(r'[A-Za-z0-9]{14,100}')
ATOMIC = re.compile(r'0|[1-9][0-9]{0,20}')
DIGEST = re.compile(r'[0-9a-f]{64}')


class ProvisionError(RuntimeError):
    """Failure whose message is a fixed string containing no helper output or secrets."""


def require(condition, message):
    if not condition:
        raise ProvisionError(message)


def write_private(path, value):
    with os.fdopen(os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600), 'w') as out:
        if isinstance(value, str):
            out.write(value)
        else:
            json.dump(value, out, separators=(',', ':'))
        out.flush()
        os.fsync(out.fileno())


def make_private_directory(path):
    os.mkdir(path, 0o700)
    os.chmod(path, 0o700)


def file_digest(path):
    digest = hashlib.sha256()
    with open(path, 'rb') as source:
        for block in iter(lambda: source.read(1 << 20), b''):
            digest.update(block)
    return digest.hexdigest()


def checked_helper(helper):
    helper = os.path.abspath(os.fspath(helper))
    try:
        info = os.stat(helper)
    except (OSError, ValueError):
        raise ProvisionError('Helper executable not found') from None
    require(stat.S_ISREG(info.st_mode) and os.access(helper, os.X_OK), 'Helper is not an executable file')
    require(not info.st_mode & 0o022, 'Helper is writable by other users')
    return helper


def certificate_file(tls_root):
    path = os.path.abspath(os.fspath(tls_root))
    if os.path.isdir(path):
        path = os.path.join(path, 'root.pem')
    require(os.path.isfile(path), 'TLS root certificate not found')
    return path


def contained_file(path, root, message):
    """Resolve a helper- or launcher-supplied path and require a regular file under root."""
    try:
        require(isinstance(path, str) and os.path.isabs(path), message)
        resolved, base = os.path.realpath(path), os.path.realpath(root)
        require(resolved != base and os.path.commonpath([resolved, base]) == base, message)
        require(stat.S_ISREG(os.lstat(resolved).st_mode), message)
    except (OSError, ValueError):
        raise ProvisionError(message) from None
    return resolved


def field(value, key, kind, message):
    require(isinstance(value, dict) and isinstance(value.get(key), kind)
            and not isinstance(value.get(key), bool), message)
    return value[key]


def _kill(process):
    try:
        os.killpg(process.pid, signal.SIGKILL)
    except (ProcessLookupError, PermissionError):
        pass
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        pass


def run_bounded(argv, payload, env, timeout, limit):
    """Run argv with bounded combined wall time and per-stream output size."""
    process = subprocess.Popen(argv, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, env=env, close_fds=True, start_new_session=True)
    deadline = time.monotonic() + timeout
    streams = {process.stdout.fileno(): bytearray(), process.stderr.fileno(): bytearray()}
    try:
        try:
            process.stdin.write(payload)
            process.stdin.close()
        except BrokenPipeError:
            pass
        with selectors.DefaultSelector() as selector:
            for pipe in (process.stdout, process.stderr):
                selector.register(pipe, selectors.EVENT_READ)
            while selector.get_map():
                remaining = deadline - time.monotonic()
                require(remaining > 0, 'Helper timed out')
                for key, _ in selector.select(remaining):
                    chunk = os.read(key.fd, 65536)
                    if not chunk:
                        selector.unregister(key.fileobj)
                        continue
                    streams[key.fd].extend(chunk)
                    require(len(streams[key.fd]) <= limit, 'Helper output exceeded limit')
        try:
            process.wait(timeout=max(deadline - time.monotonic(), 0.01))
        except subprocess.TimeoutExpired:
            raise ProvisionError('Helper timed out') from None
        return (process.returncode, bytes(streams[process.stdout.fileno()]),
                bytes(streams[process.stderr.fileno()]))
    finally:
        if process.poll() is None:
            _kill(process)
        for pipe in (process.stdin, process.stdout, process.stderr):
            try:
                pipe.close()
            except OSError:
                pass


def call_helper(helper, mode, config, certificate, fields, secret=None,
                timeout=TIMEOUT_SECONDS, limit=OUTPUT_LIMIT, coin='ltc'):
    schema = f'monzero-{coin}-light-{mode}-request-v1'
    identity = str(uuid.uuid4())
    payload = json.dumps(dict(schema=schema, requestId=identity, **fields)).encode()
    env = dict(os.environ, SSL_CERT_FILE=certificate)
    try:
        code, out, err = run_bounded([helper, f'--{mode}-stdio-v1', config], payload, env, timeout, limit)
    except OSError:
        raise ProvisionError(f'Helper could not be started: {mode}') from None
    finally:
        del payload
    require(secret is None or secret.encode() not in out + err, f'Secret in helper output: {mode}')
    require(code == 0, f'Helper failed: {mode}')
    try:
        response = json.loads(out)
    except (ValueError, UnicodeDecodeError):
        raise ProvisionError(f'Helper returned malformed output: {mode}') from None
    require(isinstance(response, dict)
            and response.get('schema') == schema.replace('-request-', '-response-')
            and response.get('requestId') == identity
            and response.get('executable') is False,
            f'Helper response context mismatch: {mode}')
    return response


def provision(helper, directory, network, maximum_height, port, tls_root,
              timeout=TIMEOUT_SECONDS, limit=OUTPUT_LIMIT, coin='ltc'):
    require(coin in COIN_NETWORKS, 'Unsupported coin')
    require(network in COIN_NETWORKS[coin], 'Network is not a test network for this coin')
    require(type(maximum_height) is int and 1 <= maximum_height <= MAXIMUM_HEIGHT_LIMIT,
            'Invalid maximum height')
    require(type(port) is int and 1 <= port <= 65535, 'Invalid port')
    helper = checked_helper(helper)
    certificate = certificate_file(tls_root)
    directory = os.path.abspath(os.fspath(directory))
    require(not os.path.lexists(directory), 'Wallet directory already exists')
    digest = file_digest(helper)

    make_private_directory(directory)
    password = 'disposable-' + secrets.token_hex(32)
    write_private(os.path.join(directory, 'light-password'), password)
    settings = dict(schema=setup_schema(coin), network=network, walletId=str(uuid.uuid4()),
        accountIndex=0, maximumHeight=maximum_height, seedEncoding=SEED_ENCODING,
        endpoint=dict(host='localhost', port=port))
    for key in STORAGE_KEYS:
        folder = os.path.join(directory, key)
        make_private_directory(folder)
        settings[key] = folder
    setup = os.path.join(directory, 'setup.json')
    write_private(setup, settings)

    created = call_helper(helper, 'wallet-create', setup, certificate, dict(createApproval=dict(
        approvedConfigDigest=file_digest(setup), password=password)),
        secret=password, timeout=timeout, limit=limit, coin=coin)
    wallet = contained_file(field(created, 'wallet', dict, 'Helper response missing wallet').get(
        'configurationFile'), directory, 'Helper configuration path outside wallet directory')
    received = call_helper(helper, 'wallet-receive', wallet, certificate, {},
        secret=password, timeout=timeout, limit=limit, coin=coin)
    del password
    address = field(field(received, 'wallet', dict, 'Helper response missing wallet'),
                    'address', str, 'Helper response missing address')
    require(ADDRESS.fullmatch(address) is not None, 'Helper returned malformed address')
    require(hmac.compare_digest(file_digest(helper), digest), 'Helper changed during provisioning')
    launcher = os.path.join(directory, 'launcher.json')
    write_private(launcher, dict(sorted(dict(schema=launcher_schema(coin),
        configFile=wallet, helper=helper, helperDigest=digest).items())))
    return dict(address=address, helperSha256=digest, configurationFile=wallet, launcher=launcher)


def read_launcher(path, coin='ltc'):
    path = os.path.abspath(os.fspath(path))
    try:
        info = os.lstat(path)
        require(stat.S_ISREG(info.st_mode) and info.st_uid == os.getuid()
                and not info.st_mode & 0o077 and info.st_size <= LAUNCHER_LIMIT,
                'Launcher is not a private regular file')
        with open(path, 'rb') as source:
            launcher = json.loads(source.read(LAUNCHER_LIMIT + 1))
    except (OSError, ValueError, UnicodeDecodeError):
        raise ProvisionError('Launcher could not be read') from None
    require(isinstance(launcher, dict) and set(launcher) == {'schema', 'configFile', 'helper', 'helperDigest'}
            and launcher['schema'] == launcher_schema(coin) and isinstance(launcher['helperDigest'], str)
            and DIGEST.fullmatch(launcher['helperDigest']) is not None, 'Launcher schema mismatch')
    config = contained_file(launcher['configFile'], os.path.dirname(path),
                            'Launcher configuration path outside wallet directory')
    return launcher['helperDigest'], config


def refresh(helper, configuration, tls_root, maximum_download_headers=10000,
            timeout=TIMEOUT_SECONDS, limit=OUTPUT_LIMIT, coin='ltc'):
    """configuration is the launcher.json written by provision(); its helper digest must match."""
    require(type(maximum_download_headers) is int
            and 1 <= maximum_download_headers <= MAXIMUM_HEIGHT_LIMIT, 'Invalid download budget')
    helper = checked_helper(helper)
    certificate = certificate_file(tls_root)
    expected, config = read_launcher(configuration, coin)
    require(hmac.compare_digest(file_digest(helper), expected), 'Helper digest does not match launcher')
    call_helper(helper, 'sync', config, certificate, dict(maximumDownloadHeaders=maximum_download_headers),
                timeout=timeout, limit=limit, coin=coin)
    observed = field(call_helper(helper, 'wallet-refresh', config, certificate, dict(walletScan=WALLET_SCAN),
        timeout=timeout, limit=limit, coin=coin), 'wallet', dict, 'Helper response missing wallet')
    result = {}
    for key in ('observedConfirmedAtomic', 'pendingReceivedAtomic'):
        value = field(observed, key, str, 'Helper response missing balance')
        require(ATOMIC.fullmatch(value) is not None, 'Helper returned malformed balance')
        result[key] = value
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    commands = parser.add_subparsers(dest='command', required=True)
    create = commands.add_parser('create', help='create a new private wallet directory')
    create.add_argument('--helper', required=True)
    create.add_argument('--directory', required=True)
    create.add_argument('--coin', choices=sorted(COIN_NETWORKS), default='ltc')
    create.add_argument('--network', required=True)
    create.add_argument('--maximum-height', required=True, type=int)
    create.add_argument('--port', required=True, type=int)
    create.add_argument('--tls-root', required=True)
    update = commands.add_parser('refresh', help='sync headers then refresh the wallet balance')
    update.add_argument('--helper', required=True)
    update.add_argument('--launcher', required=True)
    update.add_argument('--tls-root', required=True)
    update.add_argument('--maximum-download-headers', type=int, default=10000)
    update.add_argument('--coin', choices=sorted(COIN_NETWORKS), default='ltc')
    args = parser.parse_args(argv)
    try:
        if args.command == 'create':
            result = provision(args.helper, args.directory, args.network,
                               args.maximum_height, args.port, args.tls_root, coin=args.coin)
        else:
            result = refresh(args.helper, args.launcher, args.tls_root, args.maximum_download_headers,
                             coin=args.coin)
    except ProvisionError as error:
        print(f'{args.command} failed: {error}', file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print(f'{args.command} interrupted', file=sys.stderr)
        return 130
    except Exception:
        print(f'{args.command} failed: unexpected error', file=sys.stderr)
        return 1
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == '__main__':
    sys.exit(main())
