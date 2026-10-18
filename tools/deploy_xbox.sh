#!/usr/bin/env bash
# Deploy OpenCE UWP package to Xbox Dev Mode over LAN via Xbox Device Portal REST API
set -eo pipefail

CONSOLE_ENDPOINT="https://192.168.1.67:11443"
PACKAGE_PATH=""
USERNAME=""
PASSWORD=""
REPOSITORY="nickarcade/OpenCE-UWP-XB1"
TOKEN="${GH_TOKEN:-${GITHUB_TOKEN:-}}"
RUN_ID=""

print_usage() {
    cat <<EOF
Usage: $(basename "$0") [options]

Options:
  -e, --endpoint <url>      Xbox Device Portal URL (default: https://192.168.1.67:11443)
  -p, --package <path>      Path to .appx package, artifact .zip, or directory
  -u, --user <username>     Xbox Device Portal username (if auth is enabled)
  -w, --password <password> Xbox Device Portal password
  -r, --repo <owner/repo>   GitHub repository (default: OpenCommunityEdition/OpenCE-UWP-XB1)
  -t, --token <token>       GitHub token for downloading workflow artifacts
  --run-id <id>             Specific GitHub Actions workflow run ID
  -h, --help                Show this help message
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -e|--endpoint)
            CONSOLE_ENDPOINT="$2"; shift 2 ;;
        -p|--package)
            PACKAGE_PATH="$2"; shift 2 ;;
        -u|--user)
            USERNAME="$2"; shift 2 ;;
        -w|--password)
            PASSWORD="$2"; shift 2 ;;
        -r|--repo)
            REPOSITORY="$2"; shift 2 ;;
        -t|--token)
            TOKEN="$2"; shift 2 ;;
        --run-id)
            RUN_ID="$2"; shift 2 ;;
        -h|--help)
            print_usage; exit 0 ;;
        *)
            echo "Unknown option: $1" >&2
            print_usage; exit 1 ;;
    esac
done

echo "=== Xbox Dev Mode Deployment ==="
echo "Target Console: $CONSOLE_ENDPOINT"

AUTH_FLAGS=()
if [[ -n "$USERNAME" && -n "$PASSWORD" ]]; then
    AUTH_FLAGS=("-u" "$USERNAME:$PASSWORD")
fi

COOKIE_JAR=$(mktemp 2>/dev/null || echo "${TMPDIR:-.}/xbox_cookies.txt")
TEMP_DIR=""
cleanup() {
    rm -f "$COOKIE_JAR"
    if [[ -n "$TEMP_DIR" && -d "$TEMP_DIR" ]]; then
        rm -rf "$TEMP_DIR"
    fi
}
trap cleanup EXIT

echo "Checking connectivity and acquiring CSRF token from Xbox Device Portal..."
CSRF_TOKEN=$(curl -k -s -S -c "$COOKIE_JAR" --connect-timeout 5 "${AUTH_FLAGS[@]}" "$CONSOLE_ENDPOINT/" | grep -o 'CSRF-Token=[^;]*' | head -n 1 | cut -d= -f2 || true)
if [[ -z "$CSRF_TOKEN" && -f "$COOKIE_JAR" ]]; then
    CSRF_TOKEN=$(grep 'CSRF-Token' "$COOKIE_JAR" 2>/dev/null | awk '{print $NF}' || true)
fi

if [[ -n "$CSRF_TOKEN" ]]; then
    echo "Successfully connected to Xbox Device Portal (CSRF token acquired)."
else
    echo "Warning: Could not acquire CSRF token from $CONSOLE_ENDPOINT/. Checking /api/os/info..." >&2
    if curl -k -s -S --connect-timeout 5 "${AUTH_FLAGS[@]}" "$CONSOLE_ENDPOINT/api/os/info" > /dev/null 2>&1; then
        echo "Connected to Xbox Device Portal."
    else
        echo "Warning: Could not reach console. Please ensure Xbox One is powered on in Dev Mode." >&2
    fi
fi

if [[ -z "$PACKAGE_PATH" || ! -e "$PACKAGE_PATH" ]]; then
    echo "No local package provided. Fetching latest artifact from GitHub ($REPOSITORY)..."
    TEMP_DIR=$(mktemp -d -t xbox-deploy-XXXXXX 2>/dev/null || mktemp -d)
    
    DOWNLOADED=0
    if command -v gh >/dev/null 2>&1; then
        echo "Using GitHub CLI (gh) to fetch artifact..."
        if [[ -z "$RUN_ID" ]]; then
            RUN_ID=$(gh run list --repo "$REPOSITORY" --workflow uwp-auto.yml --status success --limit 1 --json databaseId -q '.[0].databaseId' || true)
        fi
        if [[ -n "$RUN_ID" ]]; then
            echo "Downloading artifact from workflow run $RUN_ID..."
            if gh run download "$RUN_ID" --repo "$REPOSITORY" --dir "$TEMP_DIR"; then
                DOWNLOADED=1
            fi
        fi
    fi

    if [[ "$DOWNLOADED" -eq 0 && -n "$TOKEN" ]]; then
        echo "Using GitHub REST API to fetch artifact..."
        ARTIFACT_URL=$(curl -s -H "Accept: application/vnd.github+json" \
            -H "Authorization: Bearer $TOKEN" \
            "https://api.github.com/repos/$REPOSITORY/actions/artifacts?per_page=1" | \
            grep -m 1 "archive_download_url" | cut -d '"' -f 4 || true)
        if [[ -n "$ARTIFACT_URL" ]]; then
            echo "Downloading $ARTIFACT_URL..."
            curl -s -L -H "Authorization: Bearer $TOKEN" "$ARTIFACT_URL" -o "$TEMP_DIR/artifact.zip"
            unzip -q -o "$TEMP_DIR/artifact.zip" -d "$TEMP_DIR"
            DOWNLOADED=1
        fi
    fi

    if [[ "$DOWNLOADED" -eq 0 ]]; then
        echo "Error: Failed to download artifact. Pass -p <path/to/appx> or set GH_TOKEN." >&2
        exit 1
    fi
    PACKAGE_PATH="$TEMP_DIR"
fi

if [[ -f "$PACKAGE_PATH" && "$PACKAGE_PATH" == *.zip ]]; then
    ZIP_EXTRACT=$(mktemp -d -t xbox-zip-XXXXXX 2>/dev/null || mktemp -d)
    unzip -q -o "$PACKAGE_PATH" -d "$ZIP_EXTRACT"
    PACKAGE_PATH="$ZIP_EXTRACT"
fi

PRIMARY_APPX=""
DEPENDENCIES=()

if [[ -f "$PACKAGE_PATH" && "$PACKAGE_PATH" == *.appx ]]; then
    PRIMARY_APPX="$PACKAGE_PATH"
    PARENT_DIR=$(dirname "$PACKAGE_PATH")
    if [[ -d "$PARENT_DIR/Dependencies" ]]; then
        while IFS= read -r -d '' dep; do
            DEPENDENCIES+=("$dep")
        done < <(find "$PARENT_DIR/Dependencies" -name "*.appx" -print0)
    fi
else
    while IFS= read -r -d '' f; do
        if [[ "$f" != *"Dependencies"* ]]; then
            if [[ -z "$PRIMARY_APPX" ]]; then
                PRIMARY_APPX="$f"
            fi
        else
            DEPENDENCIES+=("$f")
        fi
    done < <(find "$PACKAGE_PATH" -name "*.appx" -print0)
fi

if [[ -z "$PRIMARY_APPX" ]]; then
    echo "Error: No .appx package found in $PACKAGE_PATH" >&2
    exit 1
fi

PACKAGE_NAME=$(basename "$PRIMARY_APPX")
echo "Found primary package: $PRIMARY_APPX ($PACKAGE_NAME)"
if [[ ${#DEPENDENCIES[@]} -gt 0 ]]; then
    echo "Found ${#DEPENDENCIES[@]} dependencies:"
    for dep in "${DEPENDENCIES[@]}"; do
        echo "  - $(basename "$dep")"
    done
fi

FORM_ARGS=("-F" "$PACKAGE_NAME=@$PRIMARY_APPX")
for dep in "${DEPENDENCIES[@]}"; do
    FORM_ARGS+=("-F" "$(basename "$dep")=@$dep")
done

CERT_FILE=""
if [[ -f "$PARENT_DIR/OpenCE-UWP.cer" ]]; then
    CERT_FILE="$PARENT_DIR/OpenCE-UWP.cer"
elif [[ -f "$PACKAGE_PATH/OpenCE-UWP.cer" ]]; then
    CERT_FILE="$PACKAGE_PATH/OpenCE-UWP.cer"
fi
if [[ -n "$CERT_FILE" ]]; then
    echo "Found signing certificate: $CERT_FILE"
    FORM_ARGS+=("-F" "$(basename "$CERT_FILE")=@$CERT_FILE")
fi

UPLOAD_URL="${CONSOLE_ENDPOINT%/}/api/app/packagemanager/package?package=$PACKAGE_NAME"
echo "Deploying to Xbox Device Portal: $UPLOAD_URL..."

CSRF_FLAGS=()
if [[ -n "$CSRF_TOKEN" ]]; then
    CSRF_FLAGS=("-b" "$COOKIE_JAR" "-H" "X-CSRF-Token: $CSRF_TOKEN")
fi

RESP_FILE=$(mktemp 2>/dev/null || echo "${TMPDIR:-.}/xbox_deploy_resp.txt")
HTTP_RESPONSE=$(curl -k -s -S -w "%{http_code}" -o "$RESP_FILE" \
    "${AUTH_FLAGS[@]}" \
    "${CSRF_FLAGS[@]}" \
    -X POST "$UPLOAD_URL" \
    "${FORM_ARGS[@]}")

if [[ "$HTTP_RESPONSE" -lt 200 || "$HTTP_RESPONSE" -ge 300 ]]; then
    echo "Deployment failed with HTTP $HTTP_RESPONSE:" >&2
    cat "$RESP_FILE" >&2
    echo "" >&2
    rm -f "$RESP_FILE"
    exit 1
fi
rm -f "$RESP_FILE"

echo "Upload accepted (HTTP $HTTP_RESPONSE). Monitoring installation status..."
STATE_URL="${CONSOLE_ENDPOINT%/}/api/app/packagemanager/state"
for i in {1..30}; do
    sleep 2
    STATE=$(curl -k -s -S "${AUTH_FLAGS[@]}" "${CSRF_FLAGS[@]}" "$STATE_URL" || true)
    echo "Status: $STATE"
    if echo "$STATE" | grep -q -i -E '"Success"\s*:\s*true|"Reason"\s*:\s*"Success"|"Status"\s*:\s*0'; then
        echo "=== OpenCE successfully installed on Xbox One! ==="
        exit 0
    fi
done

echo "Deployment finished (status polling timed out, but package was accepted)."
