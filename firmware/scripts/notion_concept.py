#!/usr/bin/env python3
"""Crée la page Notion AMEN_MINI Concept & Plan depuis CONCEPT.md."""
import json, urllib.request, re, sys

KEY = open("/home/randomaccessmemories/.config/notion/api_key").read().strip()
PARENT = "3b6c9566-7341-8105-80b0-e3245d291056"  # page AMEN_MINI
TITLE = "AMEN_MINI — Concept & Plan firmware (V0.2, verrouillé 15/08)"

def api(method, url, payload=None):
    req = urllib.request.Request(url, method=method)
    req.add_header("Authorization", f"Bearer {KEY}")
    req.add_header("Notion-Version", "2022-06-28")
    req.add_header("Content-Type", "application/json")
    data = json.dumps(payload).encode() if payload else None
    with urllib.request.urlopen(req, data=data) as r:
        return json.loads(r.read())

def block(type_, text):
    return {"object": "block", "type": type_,
            type_: {"rich_text": [{"type": "text", "text": {"content": text}}]}}

def to_blocks(lines):
    out, i = [], 0
    while i < len(lines):
        ln = lines[i].rstrip()
        if not ln.strip():
            i += 1; continue
        if ln.startswith("### "):
            out.append(block("heading_3", ln[4:].strip()))
        elif ln.startswith("## "):
            out.append(block("heading_2", ln[3:].strip()))
        elif re.match(r"^\d+\.\s", ln):
            out.append(block("numbered_list_item", re.sub(r"^\d+\.\s", "", ln).strip()))
        elif ln.startswith("- "):
            out.append(block("bulleted_list_item", ln[2:].strip()))
        else:
            out.append(block("paragraph", ln.strip()))
        i += 1
    return out

def main():
    md = open("/home/randomaccessmemories/GIT/AMEN_MINI/firmware/docs/CONCEPT.md").read()
    lines = md.split("\n")
    blocks = to_blocks(lines)
    print(f"{len(blocks)} blocks, {sum(len(b[b['type']]['rich_text'][0]['text']['content']) for b in blocks)} chars")
    # créer la page avec les 100 premiers blocks max, puis append le reste
    first = blocks[:90]
    payload = {"parent": {"page_id": PARENT},
               "properties": {"title": {"title": [{"text": {"content": TITLE}}]}},
               "children": first}
    page = api("POST", "https://api.notion.com/v1/pages", payload)
    pid = page["id"]
    rest = blocks[90:]
    while rest:
        chunk, rest = rest[:90], rest[90:]
        api("PATCH", f"https://api.notion.com/v1/blocks/{pid}/children",
            {"children": chunk})
    print("PAGE_ID:", pid)
    print("URL: https://www.notion.so/" + pid.replace("-", ""))

if __name__ == "__main__":
    main()
