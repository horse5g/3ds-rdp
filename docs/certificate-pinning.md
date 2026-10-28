# Pinned RDP server certificates (experimental native O3DS client)

The full build uses **manual, endpoint-specific X.509 certificate pinning**
rather than accepting an unknown RDP server on first use. This enables a
trust decision without requiring a system CA store on Horizon. It does **not**
establish that NLA or rendering works on hardware.

Place `sdmc:/3ds/3ds-rdp/cert-pin.txt` on the 3DS SD card, alongside
`host.txt`. Example (replace the entire SHA-256 hex string with your
server certificate's actual digest):

```ini
host=rdp.example.com
port=3389
sha256=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef
```

These are literal values, **not** a working sample pin. The `host` must
match `host.txt` exactly (including case); the `port` must match its port.
A pin is SHA-256 of the **DER encoding of the RDP server's leaf X.509
certificate**, not a public-key hash, PEM-text hash, or the Windows SHA-1
certificate thumbprint. To calculate it on a trusted workstation from a
certificate file that you exported independently from your server:

```sh
# PEM input certificate
openssl x509 -in trusted-rdp-server.pem -outform DER | openssl dgst -sha256

# DER input certificate (e.g., Windows certificate export)
openssl x509 -inform DER -in trusted-rdp-server.cer -outform DER | openssl dgst -sha256
```

Verify that this certificate actually belongs to your intended Windows RDP
service using a trusted management channel. **Do not** obtain a fingerprint
only from the untrusted network endpoint you are about to connect to and
treat that as identity verification. A different server certificate or
certificate renewal requires deliberate pin replacement. One pin for one
endpoint is currently supported.

At connection time the full client:

1. Validates `cert-pin.txt` before requesting a password.
2. Uses FreeRDP's `ExternalCertificateManagement` X.509 verification callback.
3. Requires host/port equality, a valid certificate name (DNS SAN/CN or IPv4
   IP SAN), valid X.509 start/end dates, and constant-time digest comparison.
4. Denies changed, redirected and unexpected certificates. It does not
   silently accept untrusted certificates, save first-use decisions or
   disable NLA. A successful match is trusted for this session only.

The Nintendo 3DS clock must be correct for validity-date checking; if it
is unset, the callback denies the certificate. A manually pinned leaf can
establish out-of-band identity without a CA chain, **but** pinning does not
provide certificate revocation checking or automated trust-anchor rotation.
Protect the SD configuration against modification; an attacker who can
replace both `host.txt` and `cert-pin.txt` can redirect the client.

**Limitations:** Certificate-validation and handshake code is CI-build-tested
only, not exercised on O3DS. FreeRDP's CredSSP/NLA and domain login path,
server disconnect/reconnect semantics, password lifetime within WinPR, and
actual screen updates still need runtime security validation. Do not use
valuable credentials until those checks are complete.
