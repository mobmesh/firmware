#!/usr/bin/env python3
"""
Generate auto_commands.json from auto_cli_commands.md for the serial console help system.

This script parses the CLI commands documentation and extracts command usage,
parameters, and notes into a structured JSON format used by the web-based
serial flasher console.

Usage:
    python3 scripts/generate-commands-json.py
"""

import json
import re
from pathlib import Path


def extract_commands_from_markdown(md_file_path):
    """
    Extract CLI commands from markdown documentation.

    Returns a list of command dictionaries with 'usage', 'parameters', and 'note' keys.
    """
    with open(md_file_path, 'r') as f:
        lines = f.readlines()

    commands = []
    i = 0

    while i < len(lines):
        line = lines[i]

        # Look for ### or #### headings which mark command sections
        if line.startswith('### ') or line.startswith('#### '):
            i += 1
            usage_block = []
            parameters_block = []
            notes_block = []
            current_block = None

            # Process lines until we hit the next ## or ### or #### or end of file
            while i < len(lines):
                current = lines[i]

                # Stop at next section header (## or ### or ####)
                if current.startswith('## ') or current.startswith('### ') or current.startswith('#### '):
                    break

                # Skip separators
                if current.strip() == '---':
                    i += 1
                    break

                # Detect block markers
                if current.startswith('**Usage:**'):
                    current_block = 'usage'
                    i += 1
                    continue
                elif current.startswith('**Parameters:**'):
                    current_block = 'parameters'
                    i += 1
                    continue
                elif current.startswith('**Note:**'):
                    current_block = 'notes'
                    i += 1
                    # Extract note from same line if present
                    note_text = current[len('**Note:**'):].strip()
                    if note_text:
                        note_text = note_text.replace('_**', '').replace('**_', '')
                        note_text = note_text.replace('**', '').replace('`', '')
                        if note_text:
                            notes_block.append(note_text)
                    continue
                elif current.startswith('**Serial Only:**') or current.startswith('**Warning:**'):
                    i += 1
                    continue
                elif current.strip().startswith('**') and ':' in current:
                    # Other metadata fields (e.g. **Returns:**) -- not usage/parameters/notes.
                    # Clear current_block so this field's own prose (which may contain
                    # backticked text of its own, e.g. example values) doesn't get scanned
                    # as more usage-block content by whichever block was active before it.
                    current_block = None
                    i += 1
                    continue

                # Process content lines
                if current_block == 'usage':
                    if current.strip().startswith('- `'):
                        # Extract backtick-delimited code
                        match = re.search(r'`([^`]+)`', current)
                        if match:
                            usage_block.append(match.group(1))
                    elif current.strip().startswith('-') and not current.strip().startswith('-- '):
                        # Handle "or" continuations
                        text = current.strip()[1:].strip()
                        if text.startswith('`'):
                            match = re.search(r'`([^`]+)`', text)
                            if match:
                                usage_block.append(match.group(1))
                    elif current.strip() and not current.startswith('**'):
                        # Inline usage or continuation
                        if '`' in current:
                            match = re.search(r'`([^`]+)`', current)
                            if match:
                                usage_block.append(match.group(1))
                elif current_block == 'parameters':
                    if current.strip().startswith('- `'):
                        param_text = current.strip()[2:]  # Remove "- "
                        parameters_block.append(param_text)
                elif current_block == 'notes':
                    if current.strip() and not current.startswith('**'):
                        note_text = current.strip()
                        note_text = note_text.replace('_**', '').replace('**_', '')
                        note_text = note_text.replace('**', '').replace('`', '')
                        if note_text:
                            notes_block.append(note_text)

                i += 1

            # Combine into command entry
            usage_str = " / ".join(usage_block) if usage_block else ""
            note_str = " ".join(notes_block) if notes_block else ""

            if usage_str:
                commands.append({
                    "usage": usage_str,
                    "parameters": parameters_block,
                    "note": note_str
                })
        else:
            i += 1

    return commands


def main():
    # Paths
    script_dir = Path(__file__).parent
    repo_dir = script_dir.parent
    md_file = repo_dir / "mods" / "hotspot-ota" / "docs" / "auto_cli_commands.md"
    json_file = repo_dir / "pages" / "flasher" / "auto_commands.json"

    if not md_file.exists():
        print(f"Error: {md_file} not found")
        return 1

    print(f"Extracting commands from {md_file.name}...")
    commands = extract_commands_from_markdown(str(md_file))

    print(f"Found {len(commands)} commands")

    # Write JSON
    json_file.parent.mkdir(parents=True, exist_ok=True)
    with open(json_file, 'w') as f:
        json.dump(commands, f, indent=2)

    print(f"Wrote {json_file}")
    return 0


if __name__ == '__main__':
    exit(main())
