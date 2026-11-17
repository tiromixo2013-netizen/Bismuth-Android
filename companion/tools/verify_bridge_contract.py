#!/usr/bin/env python3
"""Fast static contract checks for the Companion <-> Shizuku <-> Geode bridge."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
USER_SERVICE = (ROOT / "companion/app/src/main/java/bismuth/companion/UserService.java").read_text()
AIDL = (ROOT / "companion/app/src/main/aidl/bismuth/companion/IBismuthUserService.aidl").read_text()
MANIFEST = (ROOT / "companion/app/src/main/AndroidManifest.xml").read_text()
APP_GRADLE = (ROOT / "companion/app/build.gradle").read_text()
GEODE = (ROOT / "geode-mod/src/main.cpp").read_text()


class BridgeContractTests(unittest.TestCase):
    def test_loopback_endpoint_matches(self):
        self.assertRegex(USER_SERVICE, r"PORT\\s*=\\s*38471")
        self.assertIn('InetAddress.getByName("127.0.0.1")', USER_SERVICE)
        self.assertIn("htons(38471)", GEODE)
        self.assertIn("htonl(INADDR_LOOPBACK)", GEODE)

    def test_csv_protocol_has_six_fields_on_both_sides(self):
        self.assertRegex(USER_SERVICE, r'"%.2f,%.2f,%.2f,%.2f,%d,%d"')
        self.assertIn("std::array<double, 6> values", GEODE)
        self.assertIn("std::getline(in, field, ',')", GEODE)

    def test_android_network_permission_and_supported_target(self):
        self.assertRegex(MANIFEST, r'<uses-permission\\s+android:name="android.permission.INTERNET"')
        self.assertRegex(APP_GRADLE, r"targetSdk\\s+35")

    def test_shizuku_user_service_binder_contract(self):
        self.assertIn("extends IBismuthUserService.Stub", USER_SERVICE)
        for method in ("startBridge", "stopBridge", "getStatus", "destroy"):
            self.assertRegex(AIDL, rf"\\b{method}\\s*\\(")
            self.assertRegex(USER_SERVICE, rf"\\b{method}\\s*\\(")


if __name__ == "__main__":
    unittest.main(verbosity=2)
