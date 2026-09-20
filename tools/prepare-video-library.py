#!/usr/bin/env python3
"""Audit local snaps and make verified uFlow copies; never modify input media."""
import argparse
import collections
import concurrent.futures
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
from fractions import Fraction

ROOTS = {
    "Wii": "Nintendo Wii (Videos Snaps)(HD)(Redump)(EM 1.2)",
    "Wii U": "Nintendo Wii U Videos",
}


def run(args, timeout=300):
    result = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            text=True, timeout=timeout)
    if result.returncode:
        raise RuntimeError(result.stderr[-3000:] or f"Command failed: {args[0]}")
    return result.stdout


def probe(path):
    return json.loads(run(["ffprobe", "-v", "error", "-show_streams", "-show_format",
                           "-of", "json", str(path)], 60))


def rate(value):
    try:
        number = float(Fraction(value))
        return number if math.isfinite(number) else 0
    except (ValueError, ZeroDivisionError, TypeError):
        return 0


def classify(info):
    video = next((s for s in info["streams"] if s["codec_type"] == "video"
                  and not s.get("disposition", {}).get("attached_pic")), None)
    audio = next((s for s in info["streams"] if s["codec_type"] == "audio"), None)
    if video is None:
        raise ValueError("No playable video stream")
    reasons = []
    if video["codec_name"] not in {"h264", "mpeg4", "mjpeg"}:
        reasons.append("unsupported video codec")
    if video["codec_name"] == "h264":
        if video.get("profile") not in {"Baseline", "Constrained Baseline", "Main", "High"}:
            reasons.append("unsupported H264 profile")
        if not 0 < video.get("level", 0) <= 42:
            reasons.append("unsupported H264 level")
    if video.get("pix_fmt") not in {"yuv420p", "yuvj420p"}:
        reasons.append("normalize pixel format")
    if video["width"] > 1280 or video["height"] > 720:
        reasons.append("reduce resolution to tested preview budget")
    if max(rate(video.get("avg_frame_rate")), rate(video.get("r_frame_rate"))) > 30.01:
        reasons.append("reduce frame rate to tested preview budget")
    if video.get("field_order", "unknown") not in {"unknown", "progressive"}:
        reasons.append("deinterlace")
    if audio and (audio["codec_name"] not in {"aac", "mp3", "pcm_s16le", "pcm_s16be", "pcm_u8"}
                  or audio.get("channels", 2) > 2):
        reasons.append("normalize audio")
    return video, audio, reasons


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def audit(item):
    platform, source, relative = item
    stat = source.stat()
    entry = dict(platform=platform, source=str(source), relative=str(relative),
                 output=str(Path(platform) / "Videos" / relative.with_suffix(".mp4")),
                 size=stat.st_size, mtime_ns=stat.st_mtime_ns)
    try:
        info = probe(source)
        video, audio, reasons = classify(info)
        if source.suffix.lower() != ".mp4":
            reasons.append("normalize container")
        entry.update(video=video, audio=audio, reasons=reasons,
                     duration=float(video.get("duration") or info["format"].get("duration", 0)),
                     action="convert" if reasons else "copy")
    except Exception as exc:
        entry.update(action="error", error=str(exc))
    return entry


def convert(entry, output_root, threads):
    started = time.monotonic()
    source = Path(entry["source"])
    stat = source.stat()
    if (stat.st_size, stat.st_mtime_ns) != (entry["size"], entry["mtime_ns"]):
        raise RuntimeError("Source changed since audit; rescan before proceeding")
    destination = output_root / entry["output"]
    if destination.exists():
        raise RuntimeError("Refusing to overwrite an existing unverified output")
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".uflow-convert-", dir=destination.parent) as temporary:
        candidate = Path(temporary) / "preview.mp4"
        if entry["action"] == "copy":
            shutil.copyfile(source, candidate)
        else:
            video, audio = entry["video"], entry["audio"]
            factor = min(1, 1280 / video["width"], 720 / video["height"])
            width, height = [max(2, int(n * factor) // 2 * 2) for n in (video["width"], video["height"])]
            fps = video.get("avg_frame_rate", "30/1")
            if not 0 < rate(fps) <= 30:
                fps = "30/1"
            filters = (["yadif"] if "deinterlace" in entry["reasons"] else [])
            filters += [f"scale={width}:{height}:flags=lanczos", f"fps={fps}"]
            command = ["ffmpeg", "-nostdin", "-hide_banner", "-v", "error", "-n",
                       "-threads", str(threads), "-i", str(source), "-map", f"0:{video['index']}"]
            if audio:
                command += ["-map", f"0:{audio['index']}"]
            command += ["-map_metadata", "-1", "-map_chapters", "-1", "-sn", "-dn",
                        "-filter_threads", "1", "-vf", ",".join(filters), "-c:v", "libx264",
                        "-threads:v", str(threads), "-preset", "fast", "-crf", "22",
                        "-profile:v", "main", "-level:v", "3.1", "-pix_fmt", "yuv420p",
                        "-refs", "3", "-bf", "2", "-maxrate", "4M", "-bufsize", "8M",
                        "-c:a", "aac", "-b:a", "160k", "-ar", "48000", "-ac", "2",
                        "-movflags", "+faststart", str(candidate)]
            run(command, max(300, entry["duration"] * 20))
        info = probe(candidate)
        video, audio, reasons = classify(info)
        if reasons:
            raise RuntimeError("Output failed compatibility validation: " + ", ".join(reasons))
        duration = float(video.get("duration") or info["format"].get("duration", 0))
        if abs(duration - entry["duration"]) > max(.3, entry["duration"] * .005):
            raise RuntimeError(f"Duration mismatch: {entry['duration']} -> {duration}")
        if bool(audio) != bool(entry["audio"]):
            raise RuntimeError("Audio stream lost or unexpectedly added")
        run(["ffmpeg", "-nostdin", "-v", "error", "-xerror", "-threads", str(threads),
             "-i", str(candidate), "-map", "0:v:0", "-map", "0:a:0?", "-f", "null", "-"],
            max(120, duration * 5))
        checksum = digest(candidate)
        final_stat = source.stat()
        if (final_stat.st_size, final_stat.st_mtime_ns) != (entry["size"], entry["mtime_ns"]):
            raise RuntimeError("Source changed during conversion; no output published")
        # Exclusive create: a concurrently created file must never be overwritten.
        with destination.open("xb") as target, candidate.open("rb") as data:
            shutil.copyfileobj(data, target)
            target.flush()
            os.fsync(target.fileno())
        if digest(destination) != checksum:
            raise RuntimeError("Final copy hash mismatch")
    return dict(output=entry["output"], source=entry["source"], size=entry["size"],
                mtime_ns=entry["mtime_ns"], action=entry["action"], sha256=checksum,
                output_bytes=destination.stat().st_size, duration=duration,
                seconds=round(time.monotonic() - started, 2), status="verified")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=Path, default=Path("/media"))
    parser.add_argument("--output", type=Path, default=Path("/output"))
    parser.add_argument("--scan-only", action="store_true")
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--threads", type=int, default=2)
    parser.add_argument("--platform", choices=ROOTS, action="append")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    plan_path = args.output / "conversion-plan.json"
    if args.scan_only or not plan_path.exists():
        items = [(platform, file, file.relative_to(args.input / folder))
                 for platform, folder in ROOTS.items()
                 for file in sorted((args.input / folder).rglob("*"))
                 if file.is_file() and file.suffix.lower() in {".mp4", ".avi", ".mkv", ".mov"}]
        with concurrent.futures.ThreadPoolExecutor(max_workers=4) as executor:
            entries = []
            for entry in executor.map(audit, items):
                entries.append(entry)
                if len(entries) % 100 == 0:
                    print(f"Scanned {len(entries)}/{len(items)}", flush=True)
        outputs = [entry["output"].casefold() for entry in entries]
        if len(outputs) != len(set(outputs)):
            raise RuntimeError("Output filename collision; no conversion performed")
        plan_path.write_text(json.dumps(entries, indent=2, ensure_ascii=False), encoding="utf-8")
    else:
        entries = json.loads(plan_path.read_text(encoding="utf-8"))
    if args.platform:
        entries = [entry for entry in entries if entry["platform"] in args.platform]
    print(json.dumps(dict(files=len(entries), actions=dict(collections.Counter(e["action"] for e in entries)),
                          reasons=dict(collections.Counter(r for e in entries for r in e.get("reasons", []))),
                          video_hours=round(sum(e.get("duration", 0) for e in entries) / 3600, 2)), indent=2), flush=True)
    if args.scan_only:
        return
    results_path = args.output / "conversion-results.jsonl"
    completed = {}
    if results_path.exists():
        for line in results_path.read_text(encoding="utf-8").splitlines():
            try:
                record = json.loads(line)
                if record.get("status") == "verified":
                    completed[record["source"]] = record
            except json.JSONDecodeError:
                pass
    pending = []
    for entry in entries:
        if entry["action"] == "error":
            print(f"AUDIT ERROR: {entry['source']}: {entry['error']}", flush=True)
            continue
        previous = completed.get(entry["source"])
        destination = args.output / entry["output"]
        if previous and destination.exists() and all(previous[k] == entry[k] for k in ("size", "mtime_ns", "output")) and digest(destination) == previous["sha256"]:
            continue
        pending.append(entry)
    began = time.monotonic()
    errors = sum(e["action"] == "error" for e in entries)
    with results_path.open("a", encoding="utf-8") as log, concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as executor:
        futures = {executor.submit(convert, e, args.output, args.threads): e for e in pending}
        for n, future in enumerate(concurrent.futures.as_completed(futures), 1):
            entry = futures[future]
            try:
                record = future.result()
            except Exception as exc:
                errors += 1
                record = dict(source=entry["source"], output=entry["output"], status="error", error=str(exc))
            log.write(json.dumps(record, ensure_ascii=False) + "\n")
            log.flush()
            elapsed = time.monotonic() - began
            eta = elapsed / n * (len(pending) - n) / 60
            print(f"[{n}/{len(pending)}] {record['status']} {entry['output']} | elapsed={elapsed/60:.1f}m ETA={eta:.1f}m errors={errors}", flush=True)
    print(f"DONE: {len(pending)} processed, {errors} errors. Originals untouched.", flush=True)
    if errors:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
