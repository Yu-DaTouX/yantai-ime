"""Compile the production legacy candidate-page reader without a live mapping."""
from pathlib import Path
import sys

from generate_reconnect_fixture import extract_body

source = Path(sys.argv[1]).read_text(encoding="utf-8")
signature = "bool TryReadCandidatePageFromSharedMemory(std::wstring *candidatePage)"
Path(sys.argv[2]).write_text(
    "// Generated from the production candidate-page reader; do not edit.\n" +
    signature + " " + extract_body(source, signature) + "\n", encoding="utf-8")
