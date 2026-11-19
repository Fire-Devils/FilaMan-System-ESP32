"""Run NFC location logic on the host: python3 -m unittest discover -s tests -v.

Compile the production handlers with hardware/Arduino boundary stubs. This does
not exercise PN532 reads, NDEF/JSON parsing, FreeRTOS scheduling, or HTTP transport.
"""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def block(source, marker):
    start = source.index(marker)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


class LocationTags(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source = (ROOT / "src/nfc.cpp").read_text()
        globals_start = source.index("JsonDocument rfidData;")
        globals_end = source.index("volatile bool nfcReadingTaskSuspendRequest")
        fragments = [source[globals_start:globals_end]]
        for name in ("static void clearLastSpool(", "static void rememberSpoolForLocation("):
            if name in source:
                fragments.append(block(source, name))
        fragments.append(block(source, "bool detectBambuTag("))
        fragments.append("void scanDecodedTag(String uidString) {" +
                         block(source, "if(filamanConnected)") + "}")
        decoder = source[source.index("bool decodeNdefAndReturnJson("):]
        error = block(decoder, "if (error)\n  {\n    nfcJsonData")
        fragments.append("bool decodeNdefAndReturnJson(const uint8_t*, String uidString) {" +
                         error.replace("if (error)", "if (failDecode)", 1) +
                         "scanDecodedTag(uidString); return true; }")
        fragments.append(block(source, "bool readCompleteJsonForFastPath("))
        fragments.append(source[source.index("bool quickSpoolIdCheck("):
                                source.index("void writeJsonToTag(")])
        location = block(source, 'else if(doc["location_id"].is<int>())')
        fragments.append("void scanLocation() " + location[location.index("{"):])
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        harness = (ROOT / "tests/location_tags.cpp").read_text()
        path = Path(cls.temp.name) / "location_tags.cpp"
        path.write_text(harness.replace("// PRODUCTION_HANDLERS", "\n".join(fragments)))
        cls.binary = Path(cls.temp.name) / "location_tags"
        subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra",
                        str(path), "-o", str(cls.binary)], check=True)

    def run_case(self, case):
        result = subprocess.run([str(self.binary), case], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_uid_only_spool_can_be_located(self):
        self.run_case("uid")

    def test_uid_only_spool_replaces_previous_spool(self):
        self.run_case("replace")

    def test_location_requires_a_spool(self):
        self.run_case("empty")

    def test_id_fallback_is_consumed_once(self):
        self.run_case("id")

    def test_extended_tag_without_id_can_be_located(self):
        self.run_case("extended")

    def test_decoded_spool_carries_uid_and_id(self):
        self.run_case("decoded")

    def test_legacy_spool_carries_uid_and_id(self):
        self.run_case("legacy")

    def test_unrecognized_json_replaces_previous_spool_by_uid(self):
        self.run_case("unknown")

    def test_numeric_spool_id_uses_uid_lookup(self):
        self.run_case("numeric")

    def test_numeric_legacy_id_uses_uid_lookup(self):
        self.run_case("numeric-legacy")

    def test_unrecognized_json_scan_request_does_not_arm_location(self):
        self.run_case("unknown-scan-request")

    def test_explicit_scan_request_does_not_arm_location(self):
        self.run_case("scan-request")

    def test_short_fast_path_keeps_uid_after_full_read(self):
        self.run_case("fast-short")

    def test_extended_fast_path_keeps_uid_after_full_read(self):
        self.run_case("fast-extended")

    def test_short_fast_path_keeps_context_after_decode_error(self):
        self.run_case("fast-short-error")

    def test_extended_fast_path_keeps_context_after_decode_error(self):
        self.run_case("fast-extended-error")
