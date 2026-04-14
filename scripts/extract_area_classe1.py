import re
from pathlib import Path

import pandas as pd
import pdfplumber

PDF_PATH = Path("data/TARIF_AREA.pdf")
OUT_CSV = Path("data/tarifs_area.csv")

# --- Helpers ---
RE_CODE = re.compile(r"^\d{3,6}$")        # codes gares (ex 3007, 15018, 3400)
RE_PAGE_FOOTER = re.compile(r"^Page\s+\d+\s+de\s+\d+", re.IGNORECASE)

def num_fr_to_float(s: str) -> float:
    """Convert '3,50' -> 3.50"""
    return float(s.replace("\u00a0", "").replace(" ", "").replace(",", "."))

def clean_text(s: str) -> str:
    return s.replace("\u00a0", " ").strip()

def tokenize_line(line: str) -> list[str]:
    """
    Tokenize conservatively:
    - Keep words as tokens
    - Keep numbers like 143,00 as one token
    - Remove '€' symbols beforehand
    """
    line = clean_text(line).replace("€", "")
    return [t for t in line.split() if t]

def is_distance_token(t: str) -> bool:
    # distance tarifaire: number like 24,00 or 35,88
    return bool(re.match(r"^\d+(?:,\d+)?$", t))

def is_price_token(t: str) -> bool:
    # prices look like 3,50 12,40 0,30 etc.
    return bool(re.match(r"^\d+(?:,\d+)?$", t))

def parse_row_from_tokens(tokens: list[str]):
    """
    Expected structure per row (as in the PDF tables):
    codeE  gareE...  codeS  gareS...  distance  c1  c2  c3  c4  c5
    We keep distance as distance_km.
    """
    if not tokens:
        return None

    # 1) code entrée
    if not RE_CODE.match(tokens[0]):
        return None
    code_entree = tokens[0]

    # 2) gare entrée: until next token that looks like code sortie
    i = 1
    gare_entree_parts = []
    while i < len(tokens) and not RE_CODE.match(tokens[i]):
        gare_entree_parts.append(tokens[i])
        i += 1
    if i >= len(tokens):
        return None
    gare_entree = " ".join(gare_entree_parts).strip()

    # 3) code sortie
    code_sortie = tokens[i]
    i += 1

    # 4) gare sortie: until we hit distance token
    gare_sortie_parts = []
    while i < len(tokens) and not is_distance_token(tokens[i]):
        gare_sortie_parts.append(tokens[i])
        i += 1
    if i >= len(tokens):
        return None
    gare_sortie = " ".join(gare_sortie_parts).strip()

    # 5) distance (km)
    distance_tok = tokens[i]
    distance_km = num_fr_to_float(distance_tok)
    i += 1

    # 6) 5 class prices
    if i + 5 > len(tokens):
        return None
    price_tokens = tokens[i:i+5]
    if not all(is_price_token(t) for t in price_tokens):
        return None

    c1, c2, c3, c4, c5 = (num_fr_to_float(t) for t in price_tokens)

    return {
        "code_entree": code_entree,
        "gare_entree": gare_entree,
        "code_sortie": code_sortie,
        "gare_sortie": gare_sortie,
        "distance_km": distance_km,          # ✅ AJOUT
        "tarif_classe_1": c1,
        "tarif_classe_2": c2,
        "tarif_classe_3": c3,
        "tarif_classe_4": c4,
        "tarif_classe_5": c5,
    }

# --- Extraction ---
rows = []
with pdfplumber.open(str(PDF_PATH)) as pdf:
    for page_idx, page in enumerate(pdf.pages):
        text = page.extract_text() or ""
        for raw_line in text.splitlines():
            line = clean_text(raw_line)

            # skip headers/footers/common noise
            if not line:
                continue
            if line.startswith("AREA - Tarifs de péage"):
                continue
            if "Code Entrée" in line and "Gare d'entrée" in line:
                continue
            if line.startswith("en vigueur au"):
                continue
            if RE_PAGE_FOOTER.match(line):
                continue

            tokens = tokenize_line(line)

            # Many non-data lines won't start with a code
            if not tokens or not RE_CODE.match(tokens[0]):
                continue

            row = parse_row_from_tokens(tokens)
            if row:
                rows.append(row)

df = pd.DataFrame(rows).drop_duplicates()

# Optional: sort for convenience
df = df.sort_values(["code_entree", "code_sortie", "gare_entree", "gare_sortie"]).reset_index(drop=True)

df.to_csv(OUT_CSV, index=False, encoding="utf-8", sep=";")
print(f"OK: {len(df)} lignes -> {OUT_CSV.resolve()}")
print(df.head(10).to_string(index=False))
