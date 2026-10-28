# RDP certificate trust on original 3DS (experimental)

The full native client supports **two** ways of trusting Windows RDP TLS
certificates, without disabling certificate verification:

1. **Interactive certificate pool (trust on first use):** When connecting to
   a new server, the 3DS shows the full leaf SHA-256 fingerprint, hostname and
   port. After checking that fingerprint through a trusted channel, press
   **A** for this session only, **X** to trust it and save, or
   **B/START** to reject. Saved certificates are recognized automatically.
2. **Optional manually provisioned pin:** A trusted administrator can place
   `cert-pin.txt` on the SD card without any first-use prompt. A matching
   manual pin takes precedence over pool entries for its host and port.

**Trust on first use is vulnerable to a man-in-the-middle during the first
connection.** The displayed fingerprint is not evidence of server identity
unless you separately compare it with the certificate on the intended
Windows machine. There is no global "always accept unknown devices" switch.

## Interactive certificate pool

The application manages:

```text
sdmc:/3ds/3ds-rdp/trusted-certs.txt
```

You do not need to create this file in advance. It is written only when you
press **X** at the unknown-certificate prompt. The format is append-only
UTF-8 ASCII with three TAB-separated fields per record:

```text
rdp.example.com<TAB>3389<TAB>0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef
10.0.0.25<TAB>3390<TAB>aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
```

In the example, `<TAB>` represents an actual tab character. The hashes
are examples and **do not represent real trusted certificates**. A maximum
of 32 unique host:port entries is supported. Hashes are SHA-256 over the
DER-encoded **leaf X.509 certificate**, not SHA-256 of the PEM text, public
key or Windows SHA-1 thumbprint.

If a saved server later sends a different certificate, the client **blocks**
the connection with a certificate-changed error. It will not replace that
entry on a single button press. To rotate trust, inspect and verify the
replacement certificate on the Windows host, then manually delete that
server's record from `trusted-certs.txt`. The next connection will show the
new fingerprint for your approval. If the file becomes malformed (including
a partially written record after power loss), the entire pool is rejected:
back up and repair the file on a trusted workstation. SD/FAT appends are not
guaranteed power-fail atomic.

## Optional manual pin (highest priority for matching endpoint)

Place `sdmc:/3ds/3ds-rdp/cert-pin.txt` alongside `host.txt`.
Only one manually pinned endpoint is currently supported:

```ini
host=rdp.example.com
port=3389
sha256=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef
```

Replace the example fingerprint. The `host` must match `host.txt` exactly
(including case) and the port must be the same. An unrelated manual pin does
not prevent a separately configured host from using the pool. An unreadable or
malformed manual pin file blocks the session until fixed. If the manual pin is
for the active endpoint and the server certificate changes, the connection is
rejected even if the pool has another certificate.

On a **trusted** PC, you can derive the digest from the actual RDP leaf
certificate exported from the intended server:

```sh
# PEM certificate
openssl x509 -in trusted-rdp-server.pem -outform DER | openssl dgst -sha256
# DER certificate
openssl x509 -inform DER -in trusted-rdp-server.cer -outform DER | openssl dgst -sha256
```

## Validation rules and limits

The X.509 callback checks the exact configured hostname and port, the 3DS
date/time, certificate validity dates, and the DNS name or IPv4 subject
alternative name **before** offering any trust choice. A certificate with an
incorrect hostname, expired dates, bad clock, unexpected redirect flags or
unparseable encoding is rejected even when the endpoint is otherwise unknown.
Make sure you connect using a hostname listed in the server certificate (or
an IP with a matching IP SAN). Unknown, self-signed certificates with a
matching name can be explicitly approved.

For FreeRDP's external-certificate callback, both session-only acceptance
and pooled acceptance are reported as valid **for that connection**; the
application maintains its own pool and does not rely on FreeRDP's automatic
known-hosts saving. NLA/CredSSP remains enabled and legacy login is disabled.
The full client currently prompts locally for a password **before** the TLS
handshake; the credential is not intended to be sent to the endpoint until
the certificate is approved. This has not been validated on O3DS.

The pin store is not encrypted or tamper-resistant. Physical SD-card access
can replace both connection settings and trusted fingerprints. Leaf pinning
does not check revocation and must be updated if Windows renews or rotates
its RDP certificate.

**Status:** Source and ARM11 compilation are tested through GitHub Actions,
but accepting a certificate, saving it to SD/FAT, completing NLA and rendering
a Windows desktop are **not yet hardware tested**. Avoid using valuable
credentials until a secure end-to-end session is validated.
