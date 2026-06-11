#!/bin/bash
set -e

# prototype 폴더 기준으로 인증서 파일 위치 결정
CERT_DIR="$(dirname "$0")"
KEY_FILE="$CERT_DIR/key.pem"
CERT_FILE="$CERT_DIR/cert.pem"

echo "SSL 인증서 및 개인키 생성 시작..."

# OpenSSL로 로컬 개발용 사설 인증서 생성 (비대화식 -nodes -subj 사용)
openssl req -x509 -newkey rsa:2048 -keyout "$KEY_FILE" -out "$CERT_FILE" \
    -sha256 -days 365 -nodes -subj "/CN=localhost" 2>/dev/null

if [ -f "$KEY_FILE" ] && [ -f "$CERT_FILE" ]; then
    echo "SSL 인증서 및 개인키 생성 완료: $KEY_FILE, $CERT_FILE"
    exit 0
else
    echo "SSL 인증서 생성에 실패했습니다." >&2
    exit 1
fi
