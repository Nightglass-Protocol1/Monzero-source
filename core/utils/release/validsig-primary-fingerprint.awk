# Extract the trusted primary-key fingerprint from one GnuPG VALIDSIG status
# line. Field 3 is the key that made the signature; the final field is the
# primary-key fingerprint when a signing subkey was used.
$1 == "[GNUPG:]" && $2 == "VALIDSIG" {
  if (found) {
    exit 2
  }
  signing = $3
  primary = $NF
  fingerprint = (length(primary) == 40 && primary ~ /^[0-9A-F]+$/) ? primary : signing
  if (length(fingerprint) != 40 || fingerprint !~ /^[0-9A-F]+$/) {
    exit 3
  }
  print fingerprint
  found = 1
}

END {
  if (!found) {
    exit 1
  }
}
