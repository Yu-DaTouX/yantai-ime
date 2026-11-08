"""Compile the production Aux sender against a deterministic pipe/clock double."""
from pathlib import Path
import sys

from generate_reconnect_fixture import extract_body

source = Path(sys.argv[1]).read_text(encoding="utf-8")
signature = "bool SendToAuxNamedpipe(const std::wstring &pipeData, bool waitForAcknowledgement)"
Path(sys.argv[2]).write_text(
    "// Generated from the production Aux sender; do not edit.\n" +
    signature + " " + extract_body(source, signature) + "\n", encoding="utf-8")
