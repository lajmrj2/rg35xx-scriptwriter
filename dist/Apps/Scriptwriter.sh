#!/bin/sh
cd "$(dirname "$0")/Scriptwriter" || exit 1
chmod +x ./scriptwriter 2>/dev/null
exec ./scriptwriter
