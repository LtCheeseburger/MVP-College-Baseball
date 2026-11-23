"""Extract roster facts from reviewed, downloaded official Sidearm pages.

Developer-only: requires beautifulsoup4. No network requests or remote code execution.
"""
import hashlib
import json
import re
import sys
from pathlib import Path
from urllib.parse import urljoin
from bs4 import BeautifulSoup

SOURCES = [
    ("ncaa:408", "Mercyhurst", "mercyhurst-2027.html", "https://hurstathletics.com/sports/baseball/roster/2026-27", 2027),
    ("ncaa:470", "New Haven", "new-haven.html", "https://newhavenchargers.com/sports/baseball/roster", 2026),
    ("ncaa:11740", "West Florida", "west-florida.html", "https://goargos.com/sports/baseball/roster/2027", 2027),
    ("ncaa:766", "West Georgia", "west-georgia.html", "https://uwgathletics.com/sports/baseball/roster/2027", 2027),
]

def extract(directory):
    result = {"format": 1, "retrievedDate": "2026-11-04", "teams": []}
    for identity, school, filename, url, season in SOURCES:
        raw = (directory / filename).read_bytes()
        page = BeautifulSoup(raw, "html.parser")
        heading = next(h.get_text(" ", strip=True) for h in page.select("h2") if "Baseball Roster" in h.get_text())
        if str(season) not in heading and not (school == "West Florida" and heading == "2026 Fall Baseball Roster"):
            raise ValueError(f"Unexpected season for {school}: {heading}")
        players = []
        for node in page.select("li.sidearm-roster-player"):
            def text(selector):
                element = node.select_one(selector)
                return element.get_text(" ", strip=True) if element else ""
            name = text(".sidearm-roster-player-name h3 a")
            if not name or " " not in name:
                raise ValueError(f"Unsplit name: {name}")
            first, last = name.split(" ", 1)
            position_node = node.select_one(".sidearm-roster-player-position")
            short = position_node.select_one(".hide-on-medium") if position_node else None
            if short:
                position = short.get_text(" ", strip=True)
            else:
                bold = position_node.select_one(".text-bold") if position_node else None
                position = bold.get_text(" ", strip=True) if bold else ""
            position = re.sub(r"\s+", "", position).upper()
            two_way = int("P" in position and any(p in position for p in ["IF", "OF", "1B", "C/"]))
            if "RHP" in position: primary = "RHP"
            elif "LHP" in position: primary = "LHP"
            elif position == "P": primary = "P"
            elif position.split("/")[0] in ("IF", "MIF", "CIF", "INF"): primary = "INF"
            elif position.startswith("C"): primary = "C"
            elif position.startswith("1B"): primary = "1B"
            elif "OF" in position: primary = "OF"
            elif position in ("UT", "UTL", "UTIL"): primary = "UT"
            elif position in ("2B", "3B", "SS", "DH"): primary = position
            else: raise ValueError(f"Unknown position: {school}, {name}: {position}")
            position_text = position_node.get_text(" ", strip=True) if position_node else ""
            hands = re.search(r"\b([RLS])/([RLS])\b", position_text)
            height = re.search(r"(\d)'\s*(\d+)", text(".sidearm-roster-player-height"))
            weight = re.search(r"\d+", text(".sidearm-roster-player-weight"))
            year = text(".sidearm-roster-player-academic-year")
            compact = year.lower().replace(" ", "")
            grade = next((v for k, v in [("fr", 1), ("so", 2), ("jr", 3), ("sr", 4), ("gr", 4), ("5th", 4)] if k in compact), None)
            if grade is None: raise ValueError(f"Unknown class: {school}, {name}: {year}")
            link = node.select_one(".sidearm-roster-player-name h3 a")
            image = node.select_one(".sidearm-roster-player-image img")
            players.append({"first": first, "last": last, "jersey": text(".sidearm-roster-player-jersey-number"),
                "pos": primary, "published_position": position, "two_way": str(two_way),
                "bats": hands[1] if hands else "", "throws": hands[2] if hands else "",
                "class_year": str(grade), "published_class": year, "redshirt": str(int(compact.startswith("r-"))),
                "height_in": str(int(height[1])*12+int(height[2])) if height else "", "weight": weight[0] if weight else "",
                "hometown": text(".sidearm-roster-player-hometown"), "previous_school": text(".sidearm-roster-player-previous-school"),
                "bio_url": urljoin(url, link["href"]), "official_player_id": node.get("data-player-id", ""),
                "headshot_url": urljoin(url, image.get("data-src", image.get("src", ""))) if image else ""})
        names = [(p["first"], p["last"]) for p in players]
        if len(names) != len(set(names)) or len(players) < 9: raise ValueError(f"Bad roster count or duplicates: {school}")
        result["teams"].append({"teamId": identity, "school": school, "season": season, "publishedHeading": heading,
            "sourceUrl": url, "snapshot": filename, "sha256": hashlib.sha256(raw).hexdigest(), "players": players})
    return result

if __name__ == "__main__":
    directory = Path(sys.argv[1])
    output = Path(sys.argv[2])
    output.write_text(json.dumps(extract(directory), ensure_ascii=False, indent=2)+"\n", encoding="utf-8")
