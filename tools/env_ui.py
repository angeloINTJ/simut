#!/usr/bin/env python3
"""env_ui.py [--manifest FILE] ENV — which touch look the firmware environment ENV builds: prints `angulo` or `original`, from the `ui` of its profile in tools/features.toml
(resolved through `extends`; a profile without the key has the original). Exit 0; exit 2, with nothing on stdout, when ENV is not a profile of the manifest.

The CI asks this for each image of the firmware matrix and holds the Ângulo ones to tools/check_ui_link.py (S10c C3, 2026-10-30). It used to ask the ending of the name
(`endsWith(matrix.env, '_angulo')`), which stopped being true when the Pico 2 W's release became an Ângulo image. tools/test_env_ui.py holds the workflow to this answer.

@project SIMUT
@license MIT License
"""
import os
import sys
import tomllib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gen_features as gf  # noqa: E402


class UnknownEnv(KeyError):
    pass


def ui_of(env, M):
    """The `ui` of the profile named like the environment: "angulo" or "original" (the manifest's default)."""
    profiles = M["profiles"]
    if env not in profiles:
        raise UnknownEnv(env)
    return gf.resolve_profile(env, profiles).get("ui", gf.DEFAULT_UI)


def main(argv):
    args = list(argv)
    manifest = gf.MANIFEST
    if "--manifest" in args:
        i = args.index("--manifest")
        if i + 1 >= len(args):
            print("env_ui: --manifest needs a file", file=sys.stderr)
            return 2
        manifest = args[i + 1]
        del args[i:i + 2]
    if len(args) != 1 or args[0].startswith("-"):
        print("usage: env_ui.py [--manifest FILE] ENV", file=sys.stderr)
        return 2
    with open(manifest, "rb") as fh:
        M = tomllib.load(fh)
    try:
        print(ui_of(args[0], M))
    except UnknownEnv:
        print(f"env_ui: {args[0]} is not a profile of {os.path.relpath(manifest, gf.ROOT)}, so the CI cannot tell which touch look it builds; add the profile there "
              "(tools/gen_features.py then writes its environment).", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
