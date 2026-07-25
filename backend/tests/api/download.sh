#!/bin/bash

. .env.test

ID=$(jq -r '.id' api/cache.json)
FILENAME=$(jq -r '.filename' api/cache.json)

curl -Ss -X GET "${URL}"/download/"${ID}/"${FILENAME} \
     -w "\n%{stderr}HTTP Status Code: %{http_code}\nTotal Time: %{time_total}s\n\n" \
     -H "Content-Type: application/json" \
     -O
     # -o .tmp_response

# jq '.' .tmp_response 2>/dev/null || bat .tmp_response

# rm -f .tmp_response

# HTTP Version: %{http_version}
# Bytes Downloaded: %{size_download}
# Bytes Uploaded: %{size_upload}
# Remote IP & PORT: %{remote_ip}:%{remote_port}
# DNS Lookup Time: %{time_namelookup}s
# TCP Connection Time: %{time_connect}s
# TLS Handshake Time: %{time_appconnect}s
# Time to First Byte: %{time_starttransfer}s
