#!/usr/bin/env python3
"""Test Claude Fable 5 via raw HTTPS (no SDK dependency)."""
import json, urllib.request, os, sys

API_KEY = os.environ.get("ANTHROPIC_API_KEY")
if not API_KEY:
    print("ERRO: Defina ANTHROPIC_API_KEY no ambiente")
    sys.exit(1)

body = json.dumps({
    "model": "claude-fable-5",
    "max_tokens": 1024,
    "output_config": {"effort": "high"},
    "messages": [{"role": "user", "content": "Responda em português: diga apenas 'TESTE FABLE 5 OK - plano Max ativo'"}]
}).encode()

req = urllib.request.Request(
    "https://api.anthropic.com/v1/messages",
    data=body,
    headers={
        "Content-Type": "application/json",
        "x-api-key": API_KEY,
        "anthropic-version": "2023-06-01",
    },
    method="POST"
)

try:
    with urllib.request.urlopen(req, timeout=30) as resp:
        data = json.loads(resp.read())

    print("\n=== TESTE CLAUDE FABLE 5 ===")
    for block in data["content"]:
        if block["type"] == "text":
            print(block["text"])
        elif block["type"] == "thinking":
            print(f"[Thinking: {block.get('thinking', '(resumo)')[:80]}...]")
    print(f"\nModelo: {data['model']}")
    print(f"Input tokens: {data['usage']['input_tokens']}")
    print(f"Output tokens: {data['usage']['output_tokens']}")
    print("=== SUCESSO ===")
except urllib.error.HTTPError as e:
    print(f"ERRO HTTP {e.code}: {e.read().decode()}")
except Exception as e:
    print(f"ERRO: {e}")
