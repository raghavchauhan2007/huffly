#!/bin/bash

. .env.test

curl -Ss -X POST "${URL}"/health \
     -w "\n%{stderr}HTTP Status Code: %{http_code}\nTotal Time: %{time_total}s\n\n" \
     -o .tmp_response

jq '.' .tmp_response 2>/dev/null || bat .tmp_response

rm -f .tmp_response
