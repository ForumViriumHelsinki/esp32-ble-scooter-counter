#!/usr/bin/env python3
"""
BLE scan data analyzer.

Generates statistics and insights from the SQLite database of BLE scans.
"""

import argparse
import sqlite3
from datetime import datetime
from pathlib import Path
from typing import Optional, Tuple

import matplotlib.dates as mdates
import matplotlib.pyplot as plt
import pandas as pd
import seaborn as sns


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Analyze BLE scan data from SQLite database")
    parser.add_argument(
        "database",
        type=Path,
        help="SQLite database file path",
    )
    parser.add_argument(
        "--start-time",
        type=str,
        help="Start time filter (ISO format: YYYY-MM-DDTHH:MM:SS)",
    )
    parser.add_argument(
        "--end-time",
        type=str,
        help="End time filter (ISO format: YYYY-MM-DDTHH:MM:SS)",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path("plots"),
        help="Output directory for plots (default: plots/)",
    )
    parser.add_argument(
        "--no-plots",
        action="store_true",
        help="Disable plot generation",
    )
    return parser.parse_args()


def get_time_filter_clause(start_time: Optional[str], end_time: Optional[str]) -> Tuple[str, list]:
    """
    Build WHERE clause for time filtering.

    Returns:
        Tuple of (where_clause, params) for SQL query
    """
    clauses = []
    params = []

    if start_time:
        clauses.append("received_at >= ?")
        params.append(start_time)

    if end_time:
        clauses.append("received_at <= ?")
        params.append(end_time)

    if clauses:
        return " AND " + " AND ".join(clauses), params
    return "", params


def print_section(title: str) -> None:
    """Print a section header."""
    print(f"\n{'=' * 60}")
    print(f" {title}")
    print("=" * 60)


def analyze_basic_stats(cur: sqlite3.Cursor, time_filter: Tuple[str, list]) -> None:
    """Print basic database statistics."""
    print_section("BASIC STATISTICS")

    where_clause, params = time_filter

    cur.execute(f"SELECT COUNT(*) FROM scans WHERE 1=1 {where_clause}", params)
    total_scans = cur.fetchone()[0]
    print(f"Total scans:           {total_scans:,}")

    cur.execute(f"SELECT COUNT(DISTINCT mac) FROM scans WHERE 1=1 {where_clause}", params)
    unique_devices = cur.fetchone()[0]
    print(f"Unique devices (MAC):  {unique_devices:,}")

    cur.execute(f"SELECT MIN(received_at), MAX(received_at) FROM scans WHERE 1=1 {where_clause}", params)
    min_time, max_time = cur.fetchone()
    if min_time and max_time:
        print(f"First scan:            {min_time}")
        print(f"Last scan:             {max_time}")

        # Calculate duration
        try:
            t1 = datetime.fromisoformat(min_time.replace("Z", "+00:00"))
            t2 = datetime.fromisoformat(max_time.replace("Z", "+00:00"))
            duration = t2 - t1
            print(f"Data span:             {duration}")
        except (ValueError, AttributeError):
            pass

    if unique_devices > 0:
        print(f"Avg scans per device:  {total_scans / unique_devices:.1f}")


def analyze_devices(cur: sqlite3.Cursor, time_filter: Tuple[str, list]) -> None:
    """Analyze device distribution."""
    print_section("DEVICE ANALYSIS")

    where_clause, params = time_filter

    # Most seen devices
    print("\nTop 15 most frequently seen devices:")
    print("-" * 55)
    cur.execute(
        f"""
        SELECT mac, name, COUNT(*) as cnt,
               MIN(received_at) as first_seen,
               MAX(received_at) as last_seen
        FROM scans
        WHERE 1=1 {where_clause}
        GROUP BY mac
        ORDER BY cnt DESC
        LIMIT 15
    """,
        params,
    )
    print(f"{'MAC':<20} {'Name':<20} {'Count':>6}")
    print("-" * 55)
    for mac, name, cnt, first, last in cur.fetchall():
        name_display = (name or "?")[:18]
        print(f"{mac:<20} {name_display:<20} {cnt:>6}")

    # Scooter statistics
    print("\nScooter detection:")
    print("-" * 40)
    cur.execute(
        f"""
        SELECT is_scooter, COUNT(*) as cnt, COUNT(DISTINCT mac) as devices
        FROM scans
        WHERE 1=1 {where_clause}
        GROUP BY is_scooter
    """,
        params,
    )
    for is_scooter, cnt, devices in cur.fetchall():
        label = "Scooter" if is_scooter else "Other" if is_scooter == 0 else "Unknown"
        print(f"{label:<12} {cnt:>8,} scans, {devices:>6,} unique devices")


def analyze_names(cur: sqlite3.Cursor, time_filter: Tuple[str, list]) -> None:
    """Analyze device name distribution."""
    print_section("DEVICE NAME ANALYSIS")

    where_clause, params = time_filter

    cur.execute(
        f"""
        SELECT name, COUNT(*) as cnt, COUNT(DISTINCT mac) as devices
        FROM scans
        WHERE name IS NOT NULL AND name != '' {where_clause}
        GROUP BY name
        ORDER BY devices DESC
        LIMIT 20
    """,
        params,
    )
    results = cur.fetchall()

    print("\nTop 20 device names (by unique device count):")
    print("-" * 55)
    print(f"{'Name':<30} {'Scans':>8} {'Devices':>8}")
    print("-" * 55)
    for name, cnt, devices in results:
        name_display = name[:28] if name else "?"
        print(f"{name_display:<30} {cnt:>8,} {devices:>8,}")

    # Name prefix analysis (useful for scooter brands)
    print("\nName prefixes (first 4 chars):")
    print("-" * 40)
    cur.execute(
        f"""
        SELECT SUBSTR(name, 1, 4) as prefix,
               COUNT(*) as cnt,
               COUNT(DISTINCT mac) as devices
        FROM scans
        WHERE name IS NOT NULL AND LENGTH(name) >= 4 {where_clause}
        GROUP BY prefix
        ORDER BY devices DESC
        LIMIT 15
    """,
        params,
    )
    print(f"{'Prefix':<10} {'Scans':>8} {'Devices':>8}")
    print("-" * 40)
    for prefix, cnt, devices in cur.fetchall():
        print(f"{prefix:<10} {cnt:>8,} {devices:>8,}")


def analyze_rssi(cur: sqlite3.Cursor, time_filter: Tuple[str, list]) -> None:
    """Analyze RSSI signal strength distribution."""
    print_section("RSSI ANALYSIS")

    where_clause, params = time_filter

    cur.execute(
        f"""
        SELECT
            MIN(rssi) as min_rssi,
            MAX(rssi) as max_rssi,
            AVG(rssi) as avg_rssi,
            COUNT(*) as cnt
        FROM scans
        WHERE rssi IS NOT NULL {where_clause}
    """,
        params,
    )
    min_rssi, max_rssi, avg_rssi, cnt = cur.fetchone()

    if cnt > 0:
        print(f"RSSI range:    {min_rssi} to {max_rssi} dBm")
        print(f"Average RSSI:  {avg_rssi:.1f} dBm")

        # RSSI distribution buckets
        print("\nRSSI distribution:")
        print("-" * 40)
        cur.execute(
            f"""
            SELECT
                CASE
                    WHEN rssi >= -50 THEN 'Excellent (>= -50)'
                    WHEN rssi >= -60 THEN 'Good (-60 to -50)'
                    WHEN rssi >= -70 THEN 'Fair (-70 to -60)'
                    WHEN rssi >= -80 THEN 'Weak (-80 to -70)'
                    WHEN rssi >= -90 THEN 'Very weak (-90 to -80)'
                    ELSE 'Marginal (< -90)'
                END as bucket,
                COUNT(*) as cnt
            FROM scans
            WHERE rssi IS NOT NULL {where_clause}
            GROUP BY bucket
            ORDER BY MIN(rssi) DESC
        """,
            params,
        )
        for bucket, cnt in cur.fetchall():
            print(f"{bucket:<25} {cnt:>8,}")


def analyze_time_patterns(cur: sqlite3.Cursor, time_filter: Tuple[str, list]) -> None:
    """Analyze time-based patterns."""
    print_section("TIME PATTERNS")

    where_clause, params = time_filter

    # Scans per hour
    print("\nScans by hour of day (UTC):")
    print("-" * 40)
    cur.execute(
        f"""
        SELECT
            CAST(SUBSTR(received_at, 12, 2) AS INTEGER) as hour,
            COUNT(*) as cnt,
            COUNT(DISTINCT mac) as devices
        FROM scans
        WHERE received_at IS NOT NULL AND LENGTH(received_at) >= 13 {where_clause}
        GROUP BY hour
        ORDER BY hour
    """,
        params,
    )
    results = cur.fetchall()
    if results:
        print(f"{'Hour':>4} {'Scans':>10} {'Devices':>10}")
        print("-" * 40)
        for hour, cnt, devices in results:
            bar = "█" * min(40, cnt // max(1, max(r[1] for r in results) // 40))
            print(f"{hour:>4} {cnt:>10,} {devices:>10,} {bar}")


def analyze_manufacturers(cur: sqlite3.Cursor, time_filter: Tuple[str, list]) -> None:
    """Analyze manufacturer ID distribution."""
    print_section("MANUFACTURER ANALYSIS")

    where_clause, params = time_filter

    cur.execute(
        f"""
        SELECT mfg_id, COUNT(*) as cnt, COUNT(DISTINCT mac) as devices
        FROM scans
        WHERE mfg_id IS NOT NULL AND mfg_id != '' {where_clause}
        GROUP BY mfg_id
        ORDER BY devices DESC
        LIMIT 15
    """,
        params,
    )
    results = cur.fetchall()

    if results:
        print("\nTop manufacturer IDs:")
        print("-" * 40)
        print(f"{'Mfg ID':<10} {'Scans':>10} {'Devices':>10}")
        print("-" * 40)
        for mfg_id, cnt, devices in results:
            print(f"{mfg_id:<10} {cnt:>10,} {devices:>10,}")


def analyze_address_types(cur: sqlite3.Cursor, time_filter: Tuple[str, list]) -> None:
    """Analyze BLE address type distribution."""
    print_section("ADDRESS TYPE ANALYSIS")

    where_clause, params = time_filter

    cur.execute(
        f"""
        SELECT addr_type, COUNT(*) as cnt, COUNT(DISTINCT mac) as devices
        FROM scans
        WHERE 1=1 {where_clause}
        GROUP BY addr_type
        ORDER BY cnt DESC
    """,
        params,
    )
    results = cur.fetchall()

    print(f"{'Address Type':<15} {'Scans':>10} {'Devices':>10}")
    print("-" * 40)
    for addr_type, cnt, devices in results:
        addr_display = addr_type or "NULL"
        print(f"{addr_display:<15} {cnt:>10,} {devices:>10,}")


def analyze_dwell_time(cur: sqlite3.Cursor, time_filter: Tuple[str, list]) -> None:
    """Estimate device dwell times."""
    print_section("DWELL TIME ANALYSIS")

    where_clause, params = time_filter

    cur.execute(
        f"""
        SELECT
            mac,
            name,
            MIN(received_at) as first_seen,
            MAX(received_at) as last_seen,
            COUNT(*) as observations
        FROM scans
        WHERE 1=1 {where_clause}
        GROUP BY mac
        HAVING COUNT(*) > 1
        ORDER BY observations DESC
        LIMIT 10
    """,
        params,
    )
    results = cur.fetchall()

    if results:
        print("\nTop 10 devices by observation count (with dwell time):")
        print("-" * 70)
        print(f"{'Name':<20} {'Observations':>12} {'Dwell Time':>20}")
        print("-" * 70)
        for mac, name, first_seen, last_seen, observations in results:
            name_display = (name or mac)[:18]
            try:
                t1 = datetime.fromisoformat(first_seen.replace("Z", "+00:00"))
                t2 = datetime.fromisoformat(last_seen.replace("Z", "+00:00"))
                dwell = t2 - t1
                print(f"{name_display:<20} {observations:>12,} {str(dwell):>20}")
            except (ValueError, AttributeError):
                print(f"{name_display:<20} {observations:>12,} {'?':>20}")


def plot_unique_devices_over_time(
    cur: sqlite3.Cursor, output_dir: Path, time_filter: Tuple[str, list], window_minutes: int = 5
) -> None:
    """Plot unique devices over time with specified time window."""
    where_clause, params = time_filter

    query = f"""
        SELECT received_at, mac, is_scooter
        FROM scans
        WHERE 1=1 {where_clause}
        ORDER BY received_at
    """

    df = pd.read_sql_query(query, cur.connection, params=params)

    if df.empty:
        print("No data to plot")
        return

    df["received_at"] = pd.to_datetime(df["received_at"])
    df["time_bucket"] = df["received_at"].dt.floor(f"{window_minutes}min")

    # Count unique devices per time bucket
    total_devices = df.groupby("time_bucket")["mac"].nunique()
    scooter_devices = df[df["is_scooter"] == 1].groupby("time_bucket")["mac"].nunique()
    other_devices = df[df["is_scooter"] == 0].groupby("time_bucket")["mac"].nunique()

    fig, ax = plt.subplots(figsize=(14, 6))

    ax.plot(total_devices.index, total_devices.values, label="All devices", linewidth=2, marker="o", markersize=4)
    ax.plot(scooter_devices.index, scooter_devices.values, label="Scooters", linewidth=2, marker="s", markersize=4)
    ax.plot(other_devices.index, other_devices.values, label="Other devices", linewidth=2, marker="^", markersize=4)

    ax.set_xlabel("Time", fontsize=12)
    ax.set_ylabel("Unique devices", fontsize=12)
    ax.set_title(f"Unique Devices Over Time ({window_minutes}-minute windows)", fontsize=14, fontweight="bold")
    ax.legend()
    ax.grid(True, alpha=0.3)

    # Format x-axis
    ax.xaxis.set_major_formatter(mdates.DateFormatter("%Y-%m-%d %H:%M"))
    plt.xticks(rotation=45, ha="right")

    plt.tight_layout()
    output_path = output_dir / "unique_devices_timeline.png"
    plt.savefig(output_path, dpi=150)
    plt.close()
    print(f"  Saved: {output_path}")


def plot_rssi_distribution(cur: sqlite3.Cursor, output_dir: Path, time_filter: Tuple[str, list]) -> None:
    """Plot RSSI signal strength distribution."""
    where_clause, params = time_filter

    query = f"""
        SELECT rssi
        FROM scans
        WHERE rssi IS NOT NULL {where_clause}
    """

    df = pd.read_sql_query(query, cur.connection, params=params)

    if df.empty:
        print("No RSSI data to plot")
        return

    fig, ax = plt.subplots(figsize=(10, 6))

    ax.hist(df["rssi"], bins=50, edgecolor="black", alpha=0.7)
    ax.axvline(df["rssi"].mean(), color="red", linestyle="--", linewidth=2, label=f"Mean: {df['rssi'].mean():.1f} dBm")
    ax.axvline(
        df["rssi"].median(),
        color="orange",
        linestyle="--",
        linewidth=2,
        label=f"Median: {df['rssi'].median():.1f} dBm",
    )

    ax.set_xlabel("RSSI (dBm)", fontsize=12)
    ax.set_ylabel("Frequency", fontsize=12)
    ax.set_title("RSSI Signal Strength Distribution", fontsize=14, fontweight="bold")
    ax.legend()
    ax.grid(True, alpha=0.3, axis="y")

    plt.tight_layout()
    output_path = output_dir / "rssi_distribution.png"
    plt.savefig(output_path, dpi=150)
    plt.close()
    print(f"  Saved: {output_path}")


def plot_hourly_activity(cur: sqlite3.Cursor, output_dir: Path, time_filter: Tuple[str, list]) -> None:
    """Plot scans and devices by hour of day."""
    where_clause, params = time_filter

    query = f"""
        SELECT received_at, mac
        FROM scans
        WHERE received_at IS NOT NULL {where_clause}
    """

    df = pd.read_sql_query(query, cur.connection, params=params)

    if df.empty:
        print("No data to plot")
        return

    df["received_at"] = pd.to_datetime(df["received_at"])
    df["hour"] = df["received_at"].dt.hour

    scans_per_hour = df.groupby("hour").size()
    devices_per_hour = df.groupby("hour")["mac"].nunique()

    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(12, 10))

    # Scans per hour
    hours = range(24)
    scan_values = [scans_per_hour.get(h, 0) for h in hours]
    ax1.bar(hours, scan_values, color="steelblue", edgecolor="black", alpha=0.7)
    ax1.set_xlabel("Hour of Day (UTC)", fontsize=12)
    ax1.set_ylabel("Number of Scans", fontsize=12)
    ax1.set_title("Scan Activity by Hour", fontsize=14, fontweight="bold")
    ax1.set_xticks(hours)
    ax1.grid(True, alpha=0.3, axis="y")

    # Unique devices per hour
    device_values = [devices_per_hour.get(h, 0) for h in hours]
    ax2.bar(hours, device_values, color="coral", edgecolor="black", alpha=0.7)
    ax2.set_xlabel("Hour of Day (UTC)", fontsize=12)
    ax2.set_ylabel("Unique Devices", fontsize=12)
    ax2.set_title("Unique Devices by Hour", fontsize=14, fontweight="bold")
    ax2.set_xticks(hours)
    ax2.grid(True, alpha=0.3, axis="y")

    plt.tight_layout()
    output_path = output_dir / "hourly_activity.png"
    plt.savefig(output_path, dpi=150)
    plt.close()
    print(f"  Saved: {output_path}")


def plot_scooter_distribution(cur: sqlite3.Cursor, output_dir: Path, time_filter: Tuple[str, list]) -> None:
    """Plot scooter vs other devices as pie charts."""
    where_clause, params = time_filter

    query = f"""
        SELECT is_scooter, COUNT(*) as scans, COUNT(DISTINCT mac) as devices
        FROM scans
        WHERE 1=1 {where_clause}
        GROUP BY is_scooter
    """

    df = pd.read_sql_query(query, cur.connection, params=params)

    if df.empty:
        print("No data to plot")
        return

    df["label"] = df["is_scooter"].map({1: "Scooters", 0: "Other", None: "Unknown"})

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 6))

    # Scans pie chart
    colors = ["#ff6b6b", "#4ecdc4", "#95a5a6"]
    ax1.pie(df["scans"], labels=df["label"], autopct="%1.1f%%", startangle=90, colors=colors)
    ax1.set_title("Distribution by Scans", fontsize=14, fontweight="bold")

    # Devices pie chart
    ax2.pie(df["devices"], labels=df["label"], autopct="%1.1f%%", startangle=90, colors=colors)
    ax2.set_title("Distribution by Unique Devices", fontsize=14, fontweight="bold")

    plt.tight_layout()
    output_path = output_dir / "scooter_distribution.png"
    plt.savefig(output_path, dpi=150)
    plt.close()
    print(f"  Saved: {output_path}")


def plot_dwell_time_distribution(cur: sqlite3.Cursor, output_dir: Path, time_filter: Tuple[str, list]) -> None:
    """Plot distribution of device dwell times."""
    where_clause, params = time_filter

    query = f"""
        SELECT
            mac,
            MIN(received_at) as first_seen,
            MAX(received_at) as last_seen
        FROM scans
        WHERE 1=1 {where_clause}
        GROUP BY mac
        HAVING COUNT(*) > 1
    """

    df = pd.read_sql_query(query, cur.connection, params=params)

    if df.empty:
        print("No dwell time data to plot")
        return

    df["first_seen"] = pd.to_datetime(df["first_seen"])
    df["last_seen"] = pd.to_datetime(df["last_seen"])
    df["dwell_minutes"] = (df["last_seen"] - df["first_seen"]).dt.total_seconds() / 60

    fig, ax = plt.subplots(figsize=(10, 6))

    ax.hist(df["dwell_minutes"], bins=50, edgecolor="black", alpha=0.7, color="mediumpurple")
    ax.axvline(
        df["dwell_minutes"].mean(),
        color="red",
        linestyle="--",
        linewidth=2,
        label=f"Mean: {df['dwell_minutes'].mean():.1f} min",
    )
    ax.axvline(
        df["dwell_minutes"].median(),
        color="orange",
        linestyle="--",
        linewidth=2,
        label=f"Median: {df['dwell_minutes'].median():.1f} min",
    )

    ax.set_xlabel("Dwell Time (minutes)", fontsize=12)
    ax.set_ylabel("Number of Devices", fontsize=12)
    ax.set_title("Device Dwell Time Distribution", fontsize=14, fontweight="bold")
    ax.legend()
    ax.grid(True, alpha=0.3, axis="y")

    plt.tight_layout()
    output_path = output_dir / "dwell_time_distribution.png"
    plt.savefig(output_path, dpi=150)
    plt.close()
    print(f"  Saved: {output_path}")


def plot_top_devices(cur: sqlite3.Cursor, output_dir: Path, time_filter: Tuple[str, list], top_n: int = 15) -> None:
    """Plot top N most frequently seen devices."""
    where_clause, params = time_filter

    query = f"""
        SELECT mac, name, COUNT(*) as cnt
        FROM scans
        WHERE 1=1 {where_clause}
        GROUP BY mac
        ORDER BY cnt DESC
        LIMIT {top_n}
    """

    df = pd.read_sql_query(query, cur.connection, params=params)

    if df.empty:
        print("No data to plot")
        return

    df["display_name"] = df.apply(lambda row: (row["name"][:15] if row["name"] else row["mac"][:17]), axis=1)

    fig, ax = plt.subplots(figsize=(10, 8))

    y_pos = range(len(df))
    ax.barh(y_pos, df["cnt"], color="teal", edgecolor="black", alpha=0.7)
    ax.set_yticks(y_pos)
    ax.set_yticklabels(df["display_name"])
    ax.invert_yaxis()
    ax.set_xlabel("Number of Scans", fontsize=12)
    ax.set_title(f"Top {top_n} Most Frequently Seen Devices", fontsize=14, fontweight="bold")
    ax.grid(True, alpha=0.3, axis="x")

    plt.tight_layout()
    output_path = output_dir / "top_devices.png"
    plt.savefig(output_path, dpi=150)
    plt.close()
    print(f"  Saved: {output_path}")


def plot_rssi_timeline(cur: sqlite3.Cursor, output_dir: Path, time_filter: Tuple[str, list], top_n: int = 50) -> None:
    """Plot RSSI over time for top N devices with longest visibility."""
    where_clause, params = time_filter

    # Get devices with longest dwell time
    query = f"""
        SELECT
            mac,
            name,
            MIN(received_at) as first_seen,
            MAX(received_at) as last_seen,
            COUNT(*) as observations
        FROM scans
        WHERE rssi IS NOT NULL {where_clause}
        GROUP BY mac
        HAVING COUNT(*) > 1
        ORDER BY (julianday(MAX(received_at)) - julianday(MIN(received_at))) DESC
        LIMIT {top_n}
    """

    top_devices_df = pd.read_sql_query(query, cur.connection, params=params)

    if top_devices_df.empty:
        print("No RSSI timeline data to plot")
        return

    # Get all scans for these devices
    top_macs = top_devices_df["mac"].tolist()
    placeholders = ",".join("?" * len(top_macs))

    query2 = f"""
        SELECT received_at, mac, rssi, name
        FROM scans
        WHERE mac IN ({placeholders}) AND rssi IS NOT NULL {where_clause}
        ORDER BY received_at
    """

    all_params = top_macs + params
    df = pd.read_sql_query(query2, cur.connection, params=all_params)

    df["received_at"] = pd.to_datetime(df["received_at"])

    fig, ax = plt.subplots(figsize=(16, 10))

    # Use a colormap for different devices
    colors = plt.cm.tab20(range(min(20, len(top_macs))))
    if len(top_macs) > 20:
        colors = plt.cm.nipy_spectral([i / len(top_macs) for i in range(len(top_macs))])

    for idx, mac in enumerate(top_macs[:top_n]):
        device_data = df[df["mac"] == mac]
        color = colors[idx % len(colors)]
        ax.scatter(device_data["received_at"], device_data["rssi"], s=20, alpha=0.6, color=color, label=mac[:17])

    ax.set_xlabel("Time", fontsize=12)
    ax.set_ylabel("RSSI (dBm)", fontsize=12)
    ax.set_title(f"RSSI Timeline for Top {top_n} Longest-Visible Devices", fontsize=14, fontweight="bold")
    ax.grid(True, alpha=0.3)

    # Format x-axis
    ax.xaxis.set_major_formatter(mdates.DateFormatter("%Y-%m-%d %H:%M"))
    plt.xticks(rotation=45, ha="right")

    # Legend outside plot area
    ax.legend(bbox_to_anchor=(1.05, 1), loc="upper left", fontsize=8, ncol=2)

    plt.tight_layout()
    output_path = output_dir / "rssi_timeline.png"
    plt.savefig(output_path, dpi=150, bbox_inches="tight")
    plt.close()
    print(f"  Saved: {output_path}")


def plot_activity_heatmap(cur: sqlite3.Cursor, output_dir: Path, time_filter: Tuple[str, list]) -> None:
    """Plot heatmap of activity by day of week and hour."""
    where_clause, params = time_filter

    query = f"""
        SELECT received_at, mac
        FROM scans
        WHERE received_at IS NOT NULL {where_clause}
    """

    df = pd.read_sql_query(query, cur.connection, params=params)

    if df.empty:
        print("No data to plot")
        return

    df["received_at"] = pd.to_datetime(df["received_at"])
    df["day_of_week"] = df["received_at"].dt.day_name()
    df["hour"] = df["received_at"].dt.hour

    # Count unique devices per day-hour combination
    heatmap_data = df.groupby(["day_of_week", "hour"])["mac"].nunique().reset_index()
    heatmap_pivot = heatmap_data.pivot(index="day_of_week", columns="hour", values="mac").fillna(0)

    # Reorder days
    day_order = ["Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"]
    heatmap_pivot = heatmap_pivot.reindex([d for d in day_order if d in heatmap_pivot.index])

    if heatmap_pivot.empty or len(heatmap_pivot) == 0:
        print("Not enough data for heatmap")
        return

    fig, ax = plt.subplots(figsize=(14, 6))

    sns.heatmap(heatmap_pivot, annot=True, fmt=".0f", cmap="YlOrRd", ax=ax, cbar_kws={"label": "Unique Devices"})

    ax.set_xlabel("Hour of Day (UTC)", fontsize=12)
    ax.set_ylabel("Day of Week", fontsize=12)
    ax.set_title("Activity Heatmap: Unique Devices by Day and Hour", fontsize=14, fontweight="bold")

    plt.tight_layout()
    output_path = output_dir / "activity_heatmap.png"
    plt.savefig(output_path, dpi=150)
    plt.close()
    print(f"  Saved: {output_path}")


def generate_all_plots(
    cur: sqlite3.Cursor, output_dir: Path, start_time: Optional[str], end_time: Optional[str]
) -> None:
    """Generate all visualization plots."""
    print_section("GENERATING VISUALIZATIONS")

    output_dir.mkdir(parents=True, exist_ok=True)

    time_filter = get_time_filter_clause(start_time, end_time)

    if start_time or end_time:
        print(f"Time filter: {start_time or 'beginning'} to {end_time or 'end'}")

    print("\nGenerating plots...")

    try:
        plot_unique_devices_over_time(cur, output_dir, time_filter)
    except Exception as e:
        print(f"  Error generating unique devices timeline: {e}")

    try:
        plot_rssi_distribution(cur, output_dir, time_filter)
    except Exception as e:
        print(f"  Error generating RSSI distribution: {e}")

    try:
        plot_hourly_activity(cur, output_dir, time_filter)
    except Exception as e:
        print(f"  Error generating hourly activity: {e}")

    try:
        plot_scooter_distribution(cur, output_dir, time_filter)
    except Exception as e:
        print(f"  Error generating scooter distribution: {e}")

    try:
        plot_dwell_time_distribution(cur, output_dir, time_filter)
    except Exception as e:
        print(f"  Error generating dwell time distribution: {e}")

    try:
        plot_top_devices(cur, output_dir, time_filter)
    except Exception as e:
        print(f"  Error generating top devices: {e}")

    try:
        plot_rssi_timeline(cur, output_dir, time_filter)
    except Exception as e:
        print(f"  Error generating RSSI timeline: {e}")

    try:
        plot_activity_heatmap(cur, output_dir, time_filter)
    except Exception as e:
        print(f"  Error generating activity heatmap: {e}")

    print(f"\nAll plots saved to: {output_dir.absolute()}")


def main() -> None:
    args = parse_args()

    if not args.database.exists():
        print(f"Error: Database not found: {args.database}")
        return

    conn = sqlite3.connect(args.database)
    cur = conn.cursor()

    print(f"Analyzing: {args.database}")

    # Build time filter
    time_filter = get_time_filter_clause(args.start_time, args.end_time)

    if args.start_time or args.end_time:
        print(f"Time filter: {args.start_time or 'beginning'} to {args.end_time or 'end'}")

    analyze_basic_stats(cur, time_filter)
    analyze_devices(cur, time_filter)
    analyze_names(cur, time_filter)
    analyze_rssi(cur, time_filter)
    analyze_time_patterns(cur, time_filter)
    analyze_manufacturers(cur, time_filter)
    analyze_address_types(cur, time_filter)
    analyze_dwell_time(cur, time_filter)

    # Generate plots
    if not args.no_plots:
        generate_all_plots(cur, args.output_dir, args.start_time, args.end_time)

    conn.close()
    print("\n" + "=" * 60)
    print(" Analysis complete")
    print("=" * 60)


if __name__ == "__main__":
    main()
