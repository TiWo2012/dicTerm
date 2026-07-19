#!/usr/bin/env bash
# Install dicTerm configuration file

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# The .conf example should be in the .config directory for better organization
CONFIG_SOURCE="$REPO_ROOT/.config/dicTerm.conf.example"
CONFIG_TARGET="$HOME/.config/dicTerm/dicTerm.conf"

# Check if the source file exists
if [[ ! -f "$CONFIG_SOURCE" ]]; then
    echo -e "${RED}Error: dicTerm.conf.example not found in repository!${NC}" >&2
    exit 1
fi

# Check if we're already running from the repository
if git rev-parse --git-dir >/dev/null 2>&1; then
    echo -e "${YELLOW}Running from git repository...${NC}"
    echo -e "Source file: $CONFIG_SOURCE"
else
    echo -e "${YELLOW}Not a git repository...${NC}"
fi

# Create the config directory
mkdir -p "$(dirname "$CONFIG_TARGET")"

# Install the configuration file
if [[ -f "$CONFIG_TARGET" ]]; then
    echo -e "${YELLOW}Configuration already exists at $CONFIG_TARGET${NC}"
    echo -e "You can edit it with your custom settings."
    echo -e "Backup the file first if you want to keep your changes: cp $CONFIG_TARGET $CONFIG_TARGET.backup"
else
    cp "$CONFIG_SOURCE" "$CONFIG_TARGET"
    echo -e "${GREEN}Configuration installed to $CONFIG_TARGET${NC}"
    echo -e "You can now edit the file to customize dicTerm settings."
    echo -e "Refer to README.md for available options."
fi

echo -e "\nConfiguration file location: $CONFIG_TARGET"
echo -e "You can also install a system-wide config at /etc/dicTerm.conf\n"