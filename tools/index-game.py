"""Prepare the exact-build native index used by the source-only distribution."""
import argparse
from pathlib import Path
import parity_bindings
import parity_index

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    database = parity_bindings.index_path(args.game, root / 'build/parity-pipeline')
    print(parity_index.analyze(args.game, database))
