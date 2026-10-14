"""Where the English translation catalogs are.

They live in a separate repository, sarusaru-translations, so the port itself
carries no translated game text. The tools look for a checkout beside this
repository, or wherever SARUSARU_TRANSLATIONS points.
"""
import os
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]


def directory():
    override = os.environ.get('SARUSARU_TRANSLATIONS')
    return Path(override) if override else ROOT.parent / 'sarusaru-translations'


def catalog(name):
    return directory() / name


def required(name):
    path = catalog(name)
    if not path.is_file():
        raise ValueError(f'{path} not found: check out sarusaru-translations beside this repository '
                         'or set SARUSARU_TRANSLATIONS')
    return path
