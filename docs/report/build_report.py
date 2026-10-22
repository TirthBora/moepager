#!/usr/bin/env python3
"""Assemble docs/report/sections/*.md into REPORT.md and REPORT.html (for PDF)."""
from pathlib import Path

import markdown

here = Path(__file__).parent
parts = [p.read_text() for p in sorted((here / "sections").glob("*.md"))]
md = "\n\n".join(parts)
# Python-Markdown nests lists only at 4-space indents; the sections use 2.
md = "\n".join(
    (" " * (2 * (len(line) - len(line.lstrip(" "))))) + line.lstrip(" ") if line.startswith("  ") else line
    for line in md.split("\n")
)
(here / "REPORT.md").write_text(md)
body = markdown.markdown(md, extensions=["tables", "fenced_code"])
css = """
@page { size: A4; margin: 16mm 15mm; }
body { font-family: 'Liberation Sans', Arial, sans-serif; font-size: 10.5pt; color: #111;
       line-height: 1.42; max-width: 180mm; margin: auto; }
h1 { font-size: 17pt; border-bottom: 2px solid #2a78d6; padding-bottom: 3px; margin-top: 22px;
     page-break-after: avoid; }
h2 { font-size: 13pt; margin-top: 16px; page-break-after: avoid; }
h3 { font-size: 11.5pt; page-break-after: avoid; }
table { border-collapse: collapse; margin: 8px 0; font-size: 8.8pt; page-break-inside: avoid; }
th, td { border: 1px solid #c9c8c3; padding: 3px 6px; vertical-align: top; }
th { background: #eef3fb; }
img { max-width: 100%; display: block; margin: 8px auto; page-break-inside: avoid; }
code { font-family: 'Liberation Mono', monospace; font-size: 9pt; background: #f3f2ef; padding: 0 2px; }
pre { background: #f3f2ef; padding: 6px; font-size: 8.5pt; overflow-x: auto; }
em { color: #444; }
"""
html = f"<!doctype html><html><head><meta charset='utf-8'><title>moepager report</title><style>{css}</style></head><body>{body}</body></html>"
(here / "REPORT.html").write_text(html)
print("wrote REPORT.md, REPORT.html")
