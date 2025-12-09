# GPG Signing for M110A Modem Releases

This directory contains tools and keys for signing release artifacts.

## Setup (One-Time)

### 1. Export your Proton Mail PGP Key

1. Log into Proton Mail web interface
2. Go to **Settings → Encryption and keys → Email encryption keys**
3. Click on your key and select **Export** → **Public key**
4. Save as `gpg/public_key.asc`

### 2. Export Private Key (for signing)

From Proton Mail:
1. Go to **Settings → Encryption and keys → Email encryption keys**
2. Click on your key and select **Export** → **Private key**
3. Enter your password when prompted
4. Save as `gpg/private_key.asc` (KEEP THIS SECRET - don't commit!)

### 3. Download Portable GPG

Download GPG4Win portable or GnuPG binary:
- Place `gpg.exe` in this directory
- Or use the signing script which can download it

## Signing Releases

Run the signing script:
```powershell
.\gpg\sign_release.ps1 -ZipFile "M110A_Modem_v1.2.0_Build148.zip"
```

This creates:
- `M110A_Modem_v1.2.0_Build148.zip.sig` - Detached signature

## Verification (for users)

Users can verify the release:
```powershell
# Import the public key
gpg --import public_key.asc

# Verify the signature
gpg --verify M110A_Modem_v1.2.0_Build148.zip.sig M110A_Modem_v1.2.0_Build148.zip
```

## Files

- `public_key.asc` - Public key (safe to distribute, commit to repo)
- `private_key.asc` - Private key (NEVER commit, add to .gitignore)
- `sign_release.ps1` - Signing script
- `gnupg/` - Portable GPG home directory (not committed)

## Security Notes

- The private key is password-protected
- Never commit `private_key.asc` or the `gnupg/` directory
- The public key should be published so users can verify signatures
