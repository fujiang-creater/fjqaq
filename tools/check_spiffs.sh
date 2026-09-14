#!/usr/bin/env bash
set -euo pipefail
grep -q '^storage,data,spiffs' robotdogsay/partitions.csv
