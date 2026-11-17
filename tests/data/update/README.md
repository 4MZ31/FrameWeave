Test fixtures for signed updates (tests/unit/UpdateVerifyTests.cpp). `test-only-public.pem` is the public
half of a throwaway RSA-3072 key made only for this test; its private half was deleted after signing
`SHA256SUMS.txt` with OpenSSL (RSA-PSS, SHA-256, 32-byte salt), exactly as `scripts/release/publish.sh`
signs a release. It has nothing to do with the real release key.
