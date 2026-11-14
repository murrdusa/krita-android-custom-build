"""Check patch scripts reference real overlay files and are all applied."""
import ast
from pathlib import Path
import unittest

REPO = Path(__file__).resolve().parents[2]
PATCH_DIR = REPO / "tools" / "patches"
CORE = REPO / "mobile_studio" / "include" / "mobile_studio"
SRC = REPO / "mobile_studio" / "src"
ROOTS = {"CORE": CORE, "SRC": SRC}


def overlay_references(script: Path):
    tree = ast.parse(script.read_text(encoding="utf-8"))
    for node in ast.walk(tree):
        if (isinstance(node, ast.BinOp) and isinstance(node.op, ast.Div)
                and isinstance(node.left, ast.Name) and node.left.id in ROOTS
                and isinstance(node.right, ast.Constant)
                and isinstance(node.right.value, str)):
            yield ROOTS[node.left.id], node.right.value


class PatchSourceTests(unittest.TestCase):
    def test_overlay_references_exist(self):
        refs = {ref for script in PATCH_DIR.glob("apply_*.py")
                for ref in overlay_references(script)}
        self.assertTrue(refs, "no overlay references found")
        missing = sorted(str(root / name) for root, name in refs
                         if not (root / name).is_file())
        self.assertEqual(missing, [], "patch scripts reference missing overlay files")

    def test_apply_all_covers_every_patch(self):
        tree = ast.parse((PATCH_DIR / "apply_all.py").read_text(encoding="utf-8"))
        order = next(
            ast.literal_eval(node.value) for node in ast.walk(tree)
            if isinstance(node, ast.Assign)
            and getattr(node.targets[0], "id", None) == "ORDER")
        scripts = {p.name for p in PATCH_DIR.glob("apply_*.py")} - {"apply_all.py"}
        self.assertEqual(scripts - set(order), set(), "patch missing from apply_all order")


if __name__ == "__main__":
    unittest.main()
