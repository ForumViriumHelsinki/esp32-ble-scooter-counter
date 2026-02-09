#!/usr/bin/env python3
"""
MQTT to SQLite logger for BLE scanner data.

Subscribes to MQTT topics and stores BLE scan messages in SQLite database.
Supports multiple ESP32 scanners, each publishing to scooter-counter/{MAC}/devices.
Can subscribe to all scanners (wildcard) or specific scanner IDs.
"""

import argparse
import json
import logging
import signal
import sqlite3
import threading
from datetime import datetime, timezone
from pathlib import Path
from typing import Optional

import paho.mqtt.client as mqtt


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Log BLE scanner MQTT messages to SQLite database")
    parser.add_argument(
        "database",
        type=Path,
        help="SQLite database file path (created if not exists)",
    )
    parser.add_argument(
        "--host",
        default="localhost",
        help="MQTT broker host (default: localhost)",
    )
    parser.add_argument(
        "--port",
        type=int,
        default=1883,
        help="MQTT broker port (default: 1883)",
    )
    parser.add_argument(
        "--topic",
        default="scooter-counter/+/devices",
        help="MQTT topic to subscribe (default: scooter-counter/+/devices)",
    )
    parser.add_argument(
        "--scanner-ids",
        nargs="+",
        help="Specific scanner IDs to subscribe to (MAC addresses without colons, e.g., AABBCCDDEEFF). If not specified, subscribes to all scanners using wildcard.",
    )
    parser.add_argument(
        "--username",
        help="MQTT username (optional)",
    )
    parser.add_argument(
        "--password",
        help="MQTT password (optional)",
    )
    parser.add_argument(
        "--client-id",
        default="ble-sqlite-logger",
        help="MQTT client ID (default: ble-sqlite-logger)",
    )
    parser.add_argument(
        "--log",
        choices=["DEBUG", "INFO", "WARNING", "ERROR", "CRITICAL"],
        default="INFO",
        help="Logging level (default: INFO)",
    )
    parser.add_argument(
        "--batch-size",
        type=int,
        default=100,
        help="Number of messages to buffer before writing to database (default: 100)",
    )
    parser.add_argument(
        "--batch-timeout",
        type=float,
        default=10.0,
        help="Maximum seconds to wait before flushing buffer (default: 10.0)",
    )
    args = parser.parse_args()
    logging.basicConfig(
        level=args.log,
        format="%(asctime)s - %(levelname)s - %(message)s",
        datefmt="%Y-%m-%d %H:%M:%S",
    )
    return args


def init_database(db_path: Path) -> sqlite3.Connection:
    """Initialize SQLite database and create tables if needed."""
    # check_same_thread=False allows using connection from timer thread
    # This is safe because BufferedInserter uses locks for all DB operations
    conn = sqlite3.connect(db_path, check_same_thread=False)
    conn.execute("""
        CREATE TABLE IF NOT EXISTS scans (
            id INTEGER PRIMARY KEY,
            received_at TEXT NOT NULL,
            isotime TEXT NULL,
            scanner_id TEXT NOT NULL,
            mac TEXT NOT NULL,
            addr_type TEXT,
            rssi INTEGER,
            tx_power INTEGER NULL,
            name TEXT,
            mfg_id TEXT,
            mfg_data TEXT,
            services TEXT,
            connectable INTEGER,
            adv_type INTEGER,
            is_scooter INTEGER,
            device_timestamp INTEGER,
            raw_json TEXT
        )
    """)
    conn.execute("CREATE INDEX IF NOT EXISTS idx_received_at ON scans(received_at)")
    conn.execute("CREATE INDEX IF NOT EXISTS idx_scanner_id ON scans(scanner_id)")
    conn.execute("CREATE INDEX IF NOT EXISTS idx_scanner_time ON scans(scanner_id, received_at)")
    conn.execute("CREATE INDEX IF NOT EXISTS idx_mac ON scans(mac)")
    conn.execute("CREATE INDEX IF NOT EXISTS idx_name ON scans(name)")
    conn.commit()
    return conn


class BufferedInserter:
    """Buffered database inserter with automatic flushing based on size and timeout."""

    def __init__(self, conn: sqlite3.Connection, batch_size: int, batch_timeout: float):
        """
        Initialize buffered inserter.

        Args:
            conn: SQLite database connection
            batch_size: Number of records to buffer before flushing
            batch_timeout: Maximum seconds to wait before flushing buffer
        """
        self.conn = conn
        self.batch_size = batch_size
        self.batch_timeout = batch_timeout
        self.buffer: list[tuple] = []
        self.lock = threading.Lock()
        self.timer: Optional[threading.Timer] = None
        self.total_inserted = 0
        self.flush_count = 0

    def add(self, payload: dict, received_at: str) -> None:
        """Add a scan record to the buffer."""
        services = payload.get("services")
        if services is not None:
            services = json.dumps(services)

        record = (
            received_at,
            payload.get("isotime"),
            payload.get("scanner_id"),
            payload.get("mac"),
            payload.get("addr_type"),
            payload.get("rssi"),
            payload.get("tx_power"),
            payload.get("name"),
            payload.get("mfg_id"),
            payload.get("mfg_data"),
            services,
            payload.get("connectable"),
            payload.get("adv_type"),
            payload.get("is_scooter"),
            payload.get("timestamp"),
            json.dumps(payload),
        )

        with self.lock:
            self.buffer.append(record)

            # Start or reset timeout timer
            if self.timer is not None:
                self.timer.cancel()
            self.timer = threading.Timer(self.batch_timeout, self._timeout_flush)
            self.timer.daemon = True
            self.timer.start()

            # Flush if buffer is full
            if len(self.buffer) >= self.batch_size:
                self._flush_locked()

    def _timeout_flush(self) -> None:
        """Flush buffer due to timeout (called by timer thread)."""
        with self.lock:
            if self.buffer:
                self._flush_locked()

    def _flush_locked(self) -> None:
        """
        Flush buffer to database (must be called with lock held).

        This internal method assumes the caller has already acquired self.lock.
        """
        if not self.buffer:
            return

        try:
            self.conn.executemany(
                """
                INSERT INTO scans (
                    received_at, isotime, scanner_id, mac, addr_type, rssi, tx_power, name, mfg_id, mfg_data,
                    services, connectable, adv_type, is_scooter, device_timestamp, raw_json
                ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                """,
                self.buffer,
            )
            self.conn.commit()

            count = len(self.buffer)
            self.total_inserted += count
            self.flush_count += 1
            logging.info(
                f"Flushed {count} records to database (total: {self.total_inserted}, flushes: {self.flush_count})"
            )

            self.buffer.clear()

            # Cancel timer if it exists
            if self.timer is not None:
                self.timer.cancel()
                self.timer = None

        except sqlite3.Error as e:
            logging.error(f"Database error during flush: {e}")
            # Keep buffer intact on error so we can retry

    def flush(self) -> None:
        """Flush buffer to database (thread-safe, public method)."""
        with self.lock:
            self._flush_locked()

    def close(self) -> None:
        """Flush remaining records and cleanup."""
        with self.lock:
            if self.timer is not None:
                self.timer.cancel()
                self.timer = None
            self._flush_locked()

        logging.info(
            f"BufferedInserter closed: {self.total_inserted} total records inserted in {self.flush_count} flushes"
        )


def main() -> None:
    args = parse_args()

    conn = init_database(args.database)
    logging.info(f"Database: {args.database}")
    logging.info(f"Batch size: {args.batch_size}, Batch timeout: {args.batch_timeout}s")

    # Initialize buffered inserter
    inserter = BufferedInserter(conn, args.batch_size, args.batch_timeout)

    # Setup signal handlers for graceful shutdown
    def signal_handler(signum, frame):
        """Handle shutdown signals by flushing buffer and exiting."""
        sig_name = signal.Signals(signum).name
        logging.info(f"Received signal {sig_name}, flushing buffer and shutting down...")
        inserter.close()
        client.disconnect()
        conn.close()
        logging.info("Shutdown complete")
        exit(0)

    signal.signal(signal.SIGINT, signal_handler)  # Ctrl+C
    signal.signal(signal.SIGTERM, signal_handler)  # kill <pid>

    # Determine topics to subscribe
    if args.scanner_ids:
        topics = [f"scooter-counter/{scanner_id}/devices" for scanner_id in args.scanner_ids]
    else:
        topics = [args.topic]

    def on_connect(client, userdata, flags, reason_code, properties):
        logging.info(f"Connected to {args.host}:{args.port}")
        for topic in topics:
            client.subscribe(topic)
            logging.info(f"Subscribed to topic: {topic}")

    def on_message(client, userdata, msg):
        received_at = datetime.now(timezone.utc).isoformat()
        try:
            payload = json.loads(msg.payload.decode("utf-8"))
            inserter.add(payload, received_at)
            isotime = payload.get("isotime", "?")
            scanner_id = payload.get("scanner_id", "?")
            name = payload.get("name", "?")
            mac = payload.get("mac", "?")
            rssi = payload.get("rssi", "?")
            logging.debug(f"[{received_at}] [{isotime}] [{scanner_id}] {name} ({mac}) RSSI: {rssi}")
        except json.JSONDecodeError as e:
            logging.error(f"[{received_at}] Invalid JSON: {e}")
        except Exception as e:
            logging.error(f"[{received_at}] Error processing message: {e}")

    def on_disconnect(client, userdata, flags, reason_code, properties):
        logging.info(f"Disconnected (reason: {reason_code})")

    client = mqtt.Client(
        callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
        client_id=args.client_id,
    )

    if args.username:
        client.username_pw_set(args.username, args.password)

    client.on_connect = on_connect
    client.on_message = on_message
    client.on_disconnect = on_disconnect

    try:
        logging.info(f"Connecting to {args.host}:{args.port}...")
        client.connect(args.host, args.port, keepalive=60)
        client.loop_forever()
    except KeyboardInterrupt:
        logging.info("Shutting down...")
    finally:
        inserter.close()
        client.disconnect()
        conn.close()


if __name__ == "__main__":
    main()
