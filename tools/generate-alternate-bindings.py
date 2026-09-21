#!/usr/bin/env python3
"""Generate a second, hash-pinned executable profile for a migrated plugin."""

import argparse
import hashlib
import json
import struct
from pathlib import Path

import pefile


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def number(value):
    return int(value, 0) if isinstance(value, str) else int(value)


def follow_jumps(image, rva):
    for _ in range(8):
        if image.get_data(rva, 1) != b"\xe9":
            return rva
        rva += 5 + struct.unpack("<i", image.get_data(rva + 1, 4))[0]
    raise ValueError("unexpected thunk chain")


def derive_blood_projection(image, matches):
    name = "?splatBlood@MedicalSystem@@QEAAXAEBVDamages@@W4CutDirection@@W4Enum@AttackDirection@@@Z"
    splat = follow_jumps(image, number(matches[name]["old"]))
    if image.get_data(splat + 0x20A, 1) != b"\xe8":
        raise ValueError("alternate blood caller changed")
    request = follow_jumps(
        image,
        splat + 0x20F + struct.unpack("<i", image.get_data(splat + 0x20B, 4))[0],
    )
    if image.get_data(request + 0x10B, 1) != b"\xe8":
        raise ValueError("alternate blood request changed")
    return follow_jumps(
        image,
        request + 0x110 + struct.unpack("<i", image.get_data(request + 0x10C, 4))[0],
    )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--profile", required=True)
    parser.add_argument("--correspondence", required=True)
    parser.add_argument("--alternate-game", required=True)
    parser.add_argument("--prefix", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    profile = json.loads(Path(args.profile).read_text(encoding="utf-8"))
    correspondence = json.loads(Path(args.correspondence).read_text(encoding="utf-8"))
    alternate_hash = sha256(args.alternate_game)
    identity = profile.get("identity")
    if not identity:
        raise ValueError("primary profile has no identity")
    if correspondence.get("result") != "PASS" or correspondence.get("pending"):
        raise ValueError("correspondence is incomplete")
    if correspondence.get("new_sha256", "").lower() != profile["game_sha256"].lower():
        raise ValueError("correspondence does not describe the primary game profile")
    if correspondence.get("old_sha256", "").lower() != alternate_hash.lower():
        raise ValueError("alternate executable hash does not match correspondence")

    image = pefile.PE(args.alternate_game, fast_load=False)
    matches = correspondence["matches"]
    rows = []
    for binding in profile["bindings"]:
        rva = binding["rva"]
        expected = binding["expected"]
        if binding["module"] == 0:
            match = matches.get(binding["name"])
            if match is not None:
                if number(match["new"]) != rva:
                    raise ValueError("primary RVA mismatch for " + binding["name"])
                rva = number(match["old"])
            elif binding["name"] == "ProjectCars::bloodProjection":
                rva = derive_blood_projection(image, matches)
            else:
                raise ValueError("missing alternate binding for " + binding["name"])
            expected = "" if binding["data"] else image.get_data(rva, 32).hex()
            if not binding["data"] and len(expected) != 64:
                raise ValueError("alternate binding is outside the image: " + binding["name"])
        rows.append({"rva": rva, "expected": expected})

    prefix = args.prefix.upper()
    lines = [
        "// Generated hash-pinned alternate executable profile.",
        "#pragma once",
        '#define %s_ALTERNATE_GAME_SHA "%s"' % (prefix, alternate_hash.upper()),
        '#define %s_ALTERNATE_BINDING_IDENTITY "%s"' % (prefix, identity),
        "struct %s_AlternateBinding {unsigned rva; unsigned char expected[32];};" % prefix,
        "static const %s_AlternateBinding %s_AlternateBindings[]={" % (prefix, prefix),
    ]
    for row in rows:
        expected = ",".join(
            "0x" + row["expected"][i : i + 2]
            for i in range(0, len(row["expected"]), 2)
        ) or "0"
        lines.append("{0x%x,{%s}}," % (row["rva"], expected))
    lines += [
        "};",
        "enum {%s_ALTERNATE_BINDING_COUNT=sizeof(%s_AlternateBindings)/sizeof(%s_AlternateBindings[0])};"
        % (prefix, prefix, prefix),
    ]
    Path(args.output).write_text("\n".join(lines) + "\n", encoding="ascii")
    print("ALTERNATE_BINDINGS_OK %s %d" % (alternate_hash, len(rows)))


if __name__ == "__main__":
    main()
