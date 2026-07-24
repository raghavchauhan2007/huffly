#!/bin/bash

. .env.test

# -H "Content-Type: application/json" \
# -d "${DATA}" \

curl -Ss -X POST "${URL}"/compress \
     -w "\n%{stderr}HTTP Status Code: %{http_code}\nTotal Time: %{time_total}s\n\n" \
     -F "file=@api/avatar.png" \
     -o .tmp_response

jq '.' .tmp_response 2>/dev/null || bat .tmp_response

rm -f .tmp_response

# HTTP Version: %{http_version}
# Bytes Downloaded: %{size_download}
# Bytes Uploaded: %{size_upload}
# Remote IP & PORT: %{remote_ip}:%{remote_port}
# DNS Lookup Time: %{time_namelookup}s
# TCP Connection Time: %{time_connect}s
# TLS Handshake Time: %{time_appconnect}s
# Time to First Byte: %{time_starttransfer}s
