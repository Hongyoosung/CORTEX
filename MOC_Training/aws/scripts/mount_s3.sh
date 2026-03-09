#!/bin/bash
# Mount an S3 bucket via s3fs using the EC2 instance IAM role.
# Called automatically inside the container if S3_BUCKET is set.
set -euo pipefail

BUCKET="${S3_BUCKET:-}"
MOUNT_POINT="${S3_MOUNT:-/mnt/s3-training}"

if [ -z "${BUCKET}" ]; then
    echo "[mount_s3] S3_BUCKET not set — skipping mount"
    exit 0
fi

mkdir -p "${MOUNT_POINT}"

s3fs "${BUCKET}" "${MOUNT_POINT}" \
    -o iam_role=auto \
    -o allow_other \
    -o umask=0022 \
    -o endpoint=us-east-1 \
    -o connect_timeout=10 \
    -o retries=5

echo "[mount_s3] Mounted s3://${BUCKET} → ${MOUNT_POINT}"
