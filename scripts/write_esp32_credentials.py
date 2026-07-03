#!/usr/bin/env python3
"""Generate ESP32 Kconfig credentials from environment variables."""

from __future__ import annotations

import os
import sys
from pathlib import Path


def kconfig_string(value: str) -> str:
    escaped = value.replace("\\", "\\\\").replace('"', '\\"')
    return f'"{escaped}"'


def main() -> int:
    args = [arg for arg in sys.argv[1:] if arg != "--require-wifi"]
    require_wifi = len(args) != len(sys.argv[1:])
    output = Path(args[0]) if args else Path("application/esp32_app/credentials.conf")

    wifi_ssid = os.environ.get("WIFI_SSID", "")
    wifi_password = os.environ.get("WIFI_PASSWORD", "")
    mqtt_username = os.environ.get("GLOVE_MQTT_USERNAME", "")
    mqtt_password = os.environ.get("GLOVE_MQTT_PASSWORD", "")

    if not wifi_ssid or not wifi_password:
        print(
            "ESP32 WiFi secrets not set; credentials.conf was not generated. "
            "Add WIFI_SSID and WIFI_PASSWORD in GitHub Actions secrets."
        )
        return 2 if require_wifi else 0

    lines = [
        f"CONFIG_GLOVE_WIFI_SSID={kconfig_string(wifi_ssid)}",
        f"CONFIG_GLOVE_WIFI_PASSWORD={kconfig_string(wifi_password)}",
    ]

    if mqtt_username and mqtt_password:
        lines.extend(
            [
                "CONFIG_GLOVE_MQTT_AUTH=y",
                f"CONFIG_GLOVE_MQTT_USERNAME={kconfig_string(mqtt_username)}",
                f"CONFIG_GLOVE_MQTT_PASSWORD={kconfig_string(mqtt_password)}",
            ]
        )
    else:
        print("MQTT secrets not both set; generating WiFi-only credentials.")

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"Generated {output} from GitHub Actions secrets.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
