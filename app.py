from __future__ import annotations

import json
import math
import os
import statistics
import time
from datetime import datetime, timezone
from threading import RLock, Thread
from typing import Any

from flask import Flask, Response, jsonify, render_template, request
from sklearn.cluster import KMeans
from sklearn.linear_model import LinearRegression

app = Flask(__name__)
app.json.sort_keys = False

_swings: list[dict[str, Any]] = []
_swings_lock = RLock()
_LEVEL_NAMES = ("輕擊", "標準扣球", "強力重扣")
_serial_thread: Thread | None = None


def _summarize_swings(swings: list[dict[str, Any]]) -> dict[str, Any]:
    intensities = [float(swing["peak_dps"]) for swing in swings]
    count = len(intensities)

    if count:
        mean = statistics.fmean(intensities)
        std_dev = statistics.stdev(intensities) if count > 1 else 0.0
        cv = std_dev / mean * 100 if mean else 0.0
    else:
        mean = std_dev = cv = 0.0

    clusters: list[dict[str, Any]] = []
    latest_grade = "資料不足"
    if count >= 3 and len(set(intensities)) >= 3:
        cluster_count = min(3, len(set(intensities)))
        model = KMeans(n_clusters=cluster_count, n_init=10, random_state=42)
        labels = model.fit_predict([[value] for value in intensities])
        ordered_ids = sorted(range(cluster_count), key=lambda index: model.cluster_centers_[index][0])
        names = {
            cluster_id: _LEVEL_NAMES[min(rank, len(_LEVEL_NAMES) - 1)]
            for rank, cluster_id in enumerate(ordered_ids)
        }
        for cluster_id in ordered_ids:
            values = [value for value, label in zip(intensities, labels) if label == cluster_id]
            clusters.append({
                "name": names[cluster_id],
                "count": len(values),
                "center_dps": float(model.cluster_centers_[cluster_id][0]),
                "min_dps": min(values),
                "max_dps": max(values),
            })
        latest_grade = names[int(labels[-1])]

    fatigue: dict[str, Any] = {
        "status": "資料不足",
        "slope_dps_per_swing": 0.0,
        "change_percent": 0.0,
        "message": "至少需要 3 次揮臂才能估計趨勢。",
    }
    if count >= 3:
        model = LinearRegression().fit([[index] for index in range(count)], intensities)
        slope = float(model.coef_[0])
        baseline = float(model.predict([[0]])[0])
        change_percent = slope * (count - 1) / baseline * 100 if baseline else 0.0
        declining = slope < -max(mean * 0.02, 1.0)
        fatigue = {
            "status": "發力衰退" if declining else "趨勢穩定",
            "slope_dps_per_swing": slope,
            "change_percent": change_percent,
            "message": (
                f"峰值角速度每次約變化 {slope:+.1f} deg/s，整體變化 {change_percent:+.1f}%。"
            ),
        }

    return {
        "count": count,
        "mean_dps": mean,
        "std_dev_dps": std_dev,
        "cv_percent": cv,
        "consistency": "穩定" if count > 1 and cv <= 15 else "持續觀察",
        "latest_grade": latest_grade,
        "clusters": clusters,
        "fatigue": fatigue,
        "swings": swings,
    }


def _record_swing(payload: Any) -> tuple[dict[str, Any] | None, str | None]:
    if not isinstance(payload, dict):
        return None, "請提供 JSON 物件。"

    try:
        peak_dps = float(payload["peak_dps"])
        duration_ms = float(payload["duration_ms"])
        timestamp = payload.get("timestamp") or datetime.now(timezone.utc).isoformat()
        if not math.isfinite(peak_dps) or not math.isfinite(duration_ms):
            raise ValueError
        if peak_dps < 0 or duration_ms < 0:
            raise ValueError
        if not isinstance(timestamp, (str, int, float)):
            raise ValueError
    except (KeyError, TypeError, ValueError, OverflowError):
        return None, "peak_dps、duration_ms 與 timestamp 格式不正確。"

    swing: dict[str, Any] = {
        "id": None,
        "timestamp": str(timestamp),
        "peak_dps": peak_dps,
        "duration_ms": duration_ms,
    }
    with _swings_lock:
        swing["id"] = len(_swings) + 1
        _swings.append(swing)
        count = len(_swings)
    return {**swing, "count": count}, None


def _read_serial(port: str) -> None:
    import serial

    while True:
        try:
            with serial.Serial(port, 115200, timeout=1) as connection:
                app.logger.info("Reading M5Stack USB serial on %s", port)
                time.sleep(2)
                connection.write(b"SYNC\n")
                while True:
                    line = connection.readline()
                    if not line:
                        continue
                    try:
                        payload = json.loads(line.decode("utf-8").strip())
                    except (UnicodeDecodeError, json.JSONDecodeError):
                        continue
                    if isinstance(payload, dict) and payload.get("_sync") == "complete":
                        connection.write(b"ACK\n")
                        app.logger.info(
                            "M5Stack sync complete; received %s records.",
                            payload.get("count", 0),
                        )
                        continue
                    swing, error = _record_swing(payload)
                    if error is None and swing is not None:
                        app.logger.info("Recorded USB swing #%s", swing["id"])
        except serial.SerialException as error:
            app.logger.warning("Cannot read %s (%s); retrying in 3 seconds.", port, error)
            time.sleep(3)


def _start_serial_reader() -> None:
    global _serial_thread
    port = os.environ.get("SERIAL_PORT", "").strip()
    if not port or (_serial_thread is not None and _serial_thread.is_alive()):
        return
    _serial_thread = Thread(target=_read_serial, args=(port,), daemon=True)
    _serial_thread.start()


@app.get("/")
def index() -> str:
    return render_template("index.html")


@app.get("/api/report")
def report() -> Any:
    with _swings_lock:
        return jsonify(_summarize_swings(list(_swings)))


@app.post("/api/swing")
def add_swing() -> Any:
    swing, error = _record_swing(request.get_json(silent=True))
    if error:
        return jsonify({"error": error}), 400
    return jsonify({"message": "揮臂資料已記錄。", "id": swing["id"], "count": swing["count"]}), 201


@app.post("/api/reset")
def reset() -> Any:
    with _swings_lock:
        _swings.clear()
    return jsonify({"message": "訓練資料已重設。"})


@app.get("/api/export.csv")
def export_csv() -> Response:
    with _swings_lock:
        swings = list(_swings)
    rows = ["id,timestamp,peak_dps,duration_ms"]
    rows.extend(
        f'{swing["id"]},{swing["timestamp"]},{swing["peak_dps"]},{swing["duration_ms"]}'
        for swing in swings
    )
    return Response(
        "\ufeff" + "\n".join(rows),
        mimetype="text/csv; charset=utf-8",
        headers={"Content-Disposition": "attachment; filename=volleyball_training.csv"},
    )


if __name__ == "__main__":
    _start_serial_reader()
    app.run(host="0.0.0.0", port=int(os.environ.get("PORT", "5000")), debug=False)