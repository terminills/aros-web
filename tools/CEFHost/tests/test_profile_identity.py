#!/usr/bin/env python3
"""Host-only identity encoding checks; native profile persistence is separate."""
import hashlib
import os
from pathlib import Path
import random
import string
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
rng = random.Random(254)
pairs = [
    ("A", "primary"), ("a", "primary"), ("A", "Primary"),
    ("a-b", "c"), ("a", "b-c"), ("A" * 47, "z" * 47),
    ("a" * 27, "b" * 28), ("a" * 28, "b" * 28),
    ("a" * 31, "b" * 32),
]
alphabet = string.ascii_letters + string.digits + "_-"
pairs += [tuple("".join(rng.choices(alphabet, k=rng.randrange(1, 48)))
                for _ in range(2)) for _ in range(250)]
with tempfile.TemporaryDirectory(prefix="cef-profile-identity-") as temporary:
    work = Path(temporary)
    source = work / "identity.c"
    source.write_text('''#include "profile_identity.h"
#include <stdio.h>
int main(int argc, char **argv) {
    char out[67];
    if (argc != 3) return 2;
    profile_identity(out, argv[1], argv[2]);
    puts(out);
    return 0;
}
''')
    binary = work / "identity"
    subprocess.run([os.environ.get("CC", "cc"), "-std=c99", "-Wall", "-Wextra",
                    "-Werror", "-I", str(root), str(source), "-o", str(binary)],
                   check=True)
    observed = set()
    for app, profile in pairs:
        result = subprocess.check_output([str(binary), app, profile], text=True).strip()
        expected = "p-" + hashlib.sha256(app.encode() + b"\0" + profile.encode()).hexdigest()
        assert result == expected, (app, profile, result, expected)
        assert len(result) == 66 and result == result.lower()
        observed.add(result)
    assert len(observed) == len(set(pairs)), "distinct tested identities aliased"
print(f"PASS: {len(pairs)} profile identity cases against hashlib SHA256")
