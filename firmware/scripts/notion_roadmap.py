#!/usr/bin/env python3
"""Publie ROADMAP.md sur Notion : crée la page si absente, sinon remplace son contenu."""
import json, urllib.request, re, sys

KEY = open("/home/randomaccessmemories/.config/notion/api_key").read().strip()
PARENT = "3b6c9566-7341-8105-80b0-e3245d291056"   # page AMEN_MINI
TITLE = "AMEN_MINI — Roadmap firmware (todo exécutable)"
URL = "https://api.notion.com/v1"

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
        elif ln.startswith("<callout"):
            # callout : contenu = lignes suivantes jusqu'a </callout>
            buf = []
            i += 1
            while i < len(lines) and not lines[i].startswith("</callout>"):
                buf.append(lines[i].rstrip()); i += 1
            content = "\n".join(buf).strip()
            out.append({"object": "block", "type": "callout",
                        "callout": {"icon": {"type": "emoji", "emoji": "🎯"},
                                    "color": "blue_background",
                                    "rich_text": [{"type": "text", "text": {"content": content}}]}})
        elif re.match(r"^\d+\.\s", ln):
            out.append(block("numbered_list_item", re.sub(r"^\d+\.\s", "", ln).strip()))
        elif ln.startswith("- "):
            out.append(block("bulleted_list_item", ln[2:].strip()))
        else:
            out.append(block("paragraph", ln.strip()))
        i += 1
    return out

def find_page():
    body = {"query": "Roadmap firmware (todo exécutable)"}
    res = api("POST", f"{URL}/search", body)
    for r in res.get("results", []):
        if r.get("object") == "page":
            props = r.get("properties", {})
            title = (props.get("title", {}).get("title") or [{}])[0].get("plain_text", "")
            if title == TITLE:
                return r["id"]
    return None

def clear_children(pid):
    """Archive tous les blocs enfants existants."""
    while True:
        res = api("GET", f"{URL}/blocks/{pid}/children")
        kids = res.get("results", [])
        if not kids:
            return
        for b in kids:
            api("PATCH", f"{URL}/blocks/{b['id']}", {"archived": True})
        if not res.get("has_more"):
            return

def main():
    md = open("/home/randomaccessmemories/GIT/AMEN_MINI/firmware/docs/ROADMAP.md").read()
    blocks = to_blocks(md.split("\n"))
    nchars = sum(len(b.get(b["type"], {}).get("rich_text", [{}])[0].get("text", {}).get("content", "")) for b in blocks)
    print(f"{len(blocks)} blocks, {nchars} chars")

    pid = find_page()
    if pid:
        print("page existante:", pid)
        clear_children(pid)
        rest = blocks
        while rest:
            chunk, rest = rest[:90], rest[90:]
            api("PATCH", f"{URL}/blocks/{pid}/children", {"children": chunk})
        print("contenu remplace")
    else:
        payload = {"parent": {"page_id": PARENT},
                   "properties": {"title": {"title": [{"text": {"content": TITLE}}]}},
                   "children": blocks[:90]}
        page = api("POST", f"{URL}/pages", payload)
        pid = page["id"]
        rest = blocks[90:]
        while rest:
            chunk, rest = rest[:90], rest[90:]
            api("PATCH", f"{URL}/blocks/{pid}/children", {"children": chunk})
        print("PAGE CREEE")
    print("PAGE_ID:", pid)
    print("URL: https://www.notion.so/" + pid.replace("-", ""))

if __name__ == "__main__":
    main()
