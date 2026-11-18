#!/usr/bin/env python3
"""Fast static contract checks for the Companion <-> Shizuku <-> Geode bridge."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
USER_SERVICE = (ROOT / "companion/app/src/main/java/bismuth/companion/UserService.java").read_text()
AIDL = (ROOT / "companion/app/src/main/aidl/bismuth/companion/IBismuthUserService.aidl").read_text()
MANIFEST = (ROOT / "companion/app/src/main/AndroidManifest.xml").read_text()
APP_GRADLE = (ROOT / "companion/app/build.gradle").read_text()
GEODE = (ROOT / "geode-mod/src/main.cpp").read_text()


class BridgeContractTests(unittest.TestCase):
    def test_loopback_endpoint_matches(self):
        self.assertIn("PORT = 38471", USER_SERVICE)
        self.assertIn('InetAddress.getByName("127.0.0.1")', USER_SERVICE)
        self.assertIn("htons(38471)", GEODE)
        self.assertIn("htonl(INADDR_LOOPBACK)", GEODE)

    def test_csv_protocol_has_six_fields_on_both_sides(self):
        self.assertIn('"%.2f,%.2f,%.2f,%.2f,%d,%d"', USER_SERVICE)
        self.assertIn("std::array<double, 6> values", GEODE)
        self.assertIn("std::getline(in, field, ',')", GEODE)

    def test_android_network_permission_and_supported_target(self):
        self.assertIn('android:name="android.permission.INTERNET"', MANIFEST)
        self.assertIn("targetSdk 35", APP_GRADLE)

    def test_shizuku_user_service_binder_contract(self):
        self.assertIn("extends IBismuthUserService.Stub", USER_SERVICE)
        for method in ("startBridge(", "stopBridge(", "getStatus(", "destroy("):
            self.assertIn(method, AIDL)
            self.assertIn(method, USER_SERVICE)

    def test_native_parser_rejects_malformed_or_stale_telemetry(self):
        self.assertIn("std::isfinite(values[i])", GEODE)
        self.assertIn("parsed != field.size()", GEODE)
        self.assertIn("validPercent(values[0])", GEODE)
        self.assertIn("g_shizukuSnapshot = ShizukuSnapshot{}", GEODE)
        self.assertIn("telemetry timeout after 2s", GEODE)
        self.assertIn("fcntl: ", GEODE)

    def test_bridge_worker_cannot_survive_stop_restart(self):
        self.assertIn("private volatile int generation", USER_SERVICE)
        self.assertIn("final int runId = ++generation", USER_SERVICE)
        self.assertIn("++generation; // Invalidate the old worker", USER_SERVICE)
        self.assertIn("server.close()", USER_SERVICE)
        self.assertIn("isCurrentRun(runId)", USER_SERVICE)


if __name__ == "__main__":
    unittest.main(verbosity=2)
