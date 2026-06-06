#!/usr/bin/env python3
"""Clean version history references and Portuguese markers from source files.
Only modifies comment lines (// or inside /* */). Leaves code untouched.

Usage: python3 tools/cleanup_comments.py <file> [--dry-run]
"""

import re
import sys
import os

# ---- Patterns to remove (only in comment context) ----

# Version numbers: v3.44.0, v4.6.2, v3.44.0-alpha15, etc.
VERSION_RE = re.compile(
    r'\bv\d+\.\d+\.\d+(-alpha\d+)?(\.\d+)?\b'
    r'(?:\s*[-–—]\s*[A-Z][A-Za-z0-9_ -]+?)?'  # optional tag after version
)

# Standalone alpha references: alpha14, alpha35, etc.
ALPHA_RE = re.compile(r'\balpha\d+\b')

# Feature tags: F-OTA-BOOTLOOP, F-DISPLAY-ATOMIC, etc.
FTAG_RE = re.compile(r'\bF-[A-Z]+-[A-Z0-9]+(?:[-.][A-Z0-9]+)*\b')

# Bug/Security/Extension/Refactor tags
BUG_RE = re.compile(r'\bBUG-\d+\b')
SEC_RE = re.compile(r'\bSEC-\d+\b')
EXT_RE = re.compile(r'\bEXT-\d+\b')
REF_RE = re.compile(r'\bREF-\d+\b')

# Dates: 2026-05-10, 2026-04-22
DATE_RE = re.compile(r'\b20[0-9][0-9]-[0-9][0-9]-[0-9][0-9]\b')

# Portuguese before/after markers (in comments)
ANTES_RE = re.compile(r'\b(?:Antes|ANTES)\s*[:,:]\s*', re.IGNORECASE)
DEPOIS_RE = re.compile(r'\b(?:Depois|AGORA|AGORA)\s*[:,:]\s*', re.IGNORECASE)
PENDENTE_RE = re.compile(r'\b[Pp]endente\s+(?:para|ate|pra)\s+\S+\s*(?:proxima sessao|sessao|sprint)?[.!]*')

# Phase/step markers
FASE_RE = re.compile(r'\bFase\s+\d+[a-z]?\b', re.IGNORECASE)
ETAPA_RE = re.compile(r'\betapa\s+\d+[a-z]?\b', re.IGNORECASE)

# Portuguese common comment words/phrases that indicate changelog
CHANGELOG_PT = [
    (re.compile(r'\b(?:fix|corrigido|resolvido|implementado|adicionado|removido|movido|criado|alterado|ajustado|refatorado|extraido|splitado|migrado)\s+(?:em|no|na|pelo|pra|para)\s+\S+\s*\.?', re.IGNORECASE), ''),
    (re.compile(r'\bproxima\s+sessao\b[^.]*\.', re.IGNORECASE), ''),
    (re.compile(r'\bvalidado\s+(?:em|no|na)\s+HW\b[^.]*\.', re.IGNORECASE), ''),
    (re.compile(r'\b(?:HW validation|HW validacao|testado em HW)\s*:?\s*20[0-9][0-9]-[0-9][0-9]-[0-9][0-9]\b[^.]*\.?', re.IGNORECASE), ''),
    (re.compile(r'\b(?:reproduzido|reproduzivel)\s+(?:em|no|na)\s+(?:HW|hardware)\b[^.]*\.', re.IGNORECASE), ''),
    (re.compile(r'\b(?:Este|Esta|Esse|Essa)\s+(?:fix|patch|mudanca|alteracao|correcao)\b[^.]*\.', re.IGNORECASE), ''),
    (re.compile(r'\b(?:Nota|NOTA|Note):?\s*(?:REF-\d+|EXT-\d+|v\d+\.\d+)[^.]*\.', re.IGNORECASE), ''),
    (re.compile(r'\b[Cc]hangelist\s*:.*$', re.IGNORECASE), ''),
    (re.compile(r'\b(?:§\d+\.\d+|Changelist)\b.*$'), ''),
]

def is_comment_line(line):
    """Check if a line is entirely or predominantly a comment."""
    stripped = line.lstrip()
    if not stripped:
        return False
    # C++ single-line comment
    if stripped.startswith('//'):
        return True
    # Inside a block comment
    if stripped.startswith('*') or stripped.startswith('/*'):
        return True
    # Doxygen tags
    if stripped.startswith('@') or stripped.startswith('\\'):
        return True
    # Block comment continuation
    if stripped.endswith('*/'):
        return True
    return False

def is_inside_block_comment(line, in_block):
    """Check if line is inside a /* */ block comment."""
    if '/*' in line:
        return True, True
    if in_block:
        if '*/' in line:
            return True, False
        return True, True
    return False, False

def clean_comment_text(text):
    """Remove version/history references from comment text.
    Preserves newline characters to avoid corrupting file structure."""
    trailing = ''
    # Preserve trailing whitespace/newline
    m = re.search(r'(\s*)$', text)
    if m:
        trailing = m.group(1)
        text = text[:m.start()]
    text = VERSION_RE.sub('', text)
    text = ALPHA_RE.sub('', text)
    text = FTAG_RE.sub('', text)
    text = BUG_RE.sub('', text)
    text = SEC_RE.sub('', text)
    text = EXT_RE.sub('', text)
    text = REF_RE.sub('', text)
    text = DATE_RE.sub('', text)
    text = ANTES_RE.sub('', text)
    text = DEPOIS_RE.sub('', text)
    text = PENDENTE_RE.sub('', text)
    text = FASE_RE.sub('', text)
    text = ETAPA_RE.sub('', text)
    for pattern, replacement in CHANGELOG_PT:
        text = pattern.sub(replacement, text)
    text = re.sub(r'  +', ' ', text)
    text = re.sub(r'\(\s*\)', '', text)
    text = re.sub(r'\s*,\s*,', ',', text)
    text = re.sub(r'^\s*[-–—]\s*$', '', text)
    return text + trailing

def process_file(filepath, dry_run=False):
    """Process a single source file."""
    with open(filepath, 'r', encoding='utf-8') as f:
        lines = f.readlines()

    modified = False
    new_lines = []
    in_block = False

    for i, line in enumerate(lines):
        was_in_block = in_block
        is_block, in_block = is_inside_block_comment(line, in_block)

        if is_comment_line(line) or was_in_block or is_block:
            # This line is a comment
            cleaned = clean_comment_text(line)
            if cleaned != line:
                modified = True
                new_lines.append(cleaned)
            else:
                new_lines.append(line)
        else:
            new_lines.append(line)

    if modified and not dry_run:
        with open(filepath, 'w', encoding='utf-8') as f:
            f.writelines(new_lines)

    return modified


def main():
    if len(sys.argv) < 2:
        print("Usage: cleanup_comments.py <file> [--dry-run]")
        sys.exit(1)

    filepath = sys.argv[1]
    dry_run = '--dry-run' in sys.argv

    if not os.path.isfile(filepath):
        print(f"File not found: {filepath}")
        sys.exit(1)

    modified = process_file(filepath, dry_run)
    if modified:
        print(f"  Modified: {filepath}")
    return 0 if not modified else 1


if __name__ == '__main__':
    main()
