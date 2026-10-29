import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("uniform_piece", Path(__file__).parents[2] / "tools/uniform_piece.py")
piece = importlib.util.module_from_spec(spec)
spec.loader.exec_module(piece)


def texture(folder, tags):
    """Small authored raw-image fixture; no original game assets."""
    folder.mkdir(parents=True)
    tags = sorted(tags)
    head = b"SHPX" + struct.pack("<II", 0, len(tags)) + b"GIMX" + b"".join(t + bytes(4) for t in tags)
    meta = bytearray(b"XSHM" + struct.pack("<II", 1, len(head)) + head + struct.pack("<I", len(tags)))
    for i, tag in enumerate(tags):
        name = f"{i:02d}_{tag.decode().strip()}.bin".encode()
        (folder / name.decode()).write_bytes(tag * 4)
        meta.extend(struct.pack("<IBH", i, 1, len(name)) + name + bytes(16) + struct.pack("<II", 0, 0))
    (folder / "xsh.meta").write_bytes(meta)


class AuthoringTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.game = self.root / "game"
        self.source = self.game / "data/models_big/u001a.xsh"
        texture(self.source, set().union(*piece.TAGS.values()))
        texture(self.game / "data/models_big/f001a.xsh", piece.FONT_TAGS)
        (self.game / "manifest.json").write_text(json.dumps({"format": 1, "source": {"xbeSha1": "a" * 40}}))

    def create(self, part="hat", **kwargs):
        args = dict(game=self.game, output=self.root / "mod", team=44, part=part, donor="a",
                    identity="navy", name="Navy", source="u001a.xsh")
        args.update(kwargs)
        return piece.create(**args)

    def test_independent_components_and_read_only_source(self):
        original = {p.relative_to(self.game): p.read_bytes() for p in self.game.rglob("*") if p.is_file()}
        for part, tags in piece.TAGS.items():
            dest = self.root / part
            self.create(part, output=dest)
            out = dest / f"files/data/models_big/uc_44_navy_{part}.xsh"
            files = piece.component_files(out, tags)
            self.assertEqual(len(files), len(tags) + 1)
            for filename, contents in files.items():
                if filename != "xsh.meta":
                    self.assertEqual(contents, (self.source / filename).read_bytes())
            record = (dest / f"files/data/uniforms/44/{part}-navy.toml").read_text()
            self.assertIn(f'part = "{part}"', record)
            self.assertFalse(json.loads((dest / "mod.json").read_text())["affectsSaves"])
        self.assertEqual(original, {p.relative_to(self.game): p.read_bytes() for p in self.game.rglob("*") if p.is_file()})

    def test_jersey_can_supply_complete_lettering(self):
        self.create("jersey", font="f001a.xsh")
        font = self.root / "mod/files/data/models_big/uc_44_navy_font.xsh"
        self.assertEqual(len(piece.component_files(font, piece.FONT_TAGS)), 37)

    def test_missing_component_rejected_before_output_creation(self):
        texture(self.game / "data/models_big/partial.xsh", {b"hat "})
        with self.assertRaises(ValueError):
            self.create(source="partial.xsh")
        self.assertFalse((self.root / "mod").exists())

    def test_does_not_overwrite(self):
        self.create()
        before = (self.root / "mod/mod.json").read_bytes()
        with self.assertRaises(FileExistsError):
            self.create()
        self.assertEqual(before, (self.root / "mod/mod.json").read_bytes())

    def test_output_cannot_modify_source_install(self):
        with self.assertRaises(ValueError):
            self.create(output=self.game / "mod")
        self.assertFalse((self.game / "mod").exists())

    def test_team_and_part_names_do_not_collide(self):
        self.create("hat", output=self.root / "one")
        self.create("pants", output=self.root / "two")
        self.create("hat", team=45, output=self.root / "three")
        paths = []
        for folder in ("one", "two", "three"):
            root = self.root / folder / "files"
            paths.append({p.relative_to(root) for p in root.rglob("*") if p.is_file()})
        self.assertFalse(paths[0] & paths[1] or paths[0] & paths[2] or paths[1] & paths[2])

    def test_bad_authoring_values(self):
        for kwargs in ({"source": "../u001a.xsh"}, {"identity": "../bad"}, {"identity": "stock-1"},
                       {"name": "a\nb"}, {"donor": "e"}, {"team": 256}, {"font": "f001a.xsh"}):
            with self.subTest(kwargs=kwargs), self.assertRaises(ValueError):
                self.create(**kwargs)
        self.assertFalse((self.root / "mod").exists())


if __name__ == "__main__":
    unittest.main()
