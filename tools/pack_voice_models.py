"""Pack the pinned ESP-SR WakeNet/VAD models into the application's rodata.

Uses ESP-SR's srmodel_load binary format. No external model partition is needed,
so signed OTA and rollback always keep the code and its models together.
"""
import argparse
from pathlib import Path
import struct


def pack(component: Path, output: Path):
    models = []
    for family, name in (("wakenet_model", "wn9_nihaoxiaozhi_tts"),
                         ("vadnet_model", "vadnet1_medium")):
        directory = component / "model" / family / name
        files = [(p.name, p.read_bytes()) for p in sorted(directory.iterdir()) if p.is_file()]
        if not files or not any(n == "_MODEL_INFO_" for n, _ in files):
            raise ValueError(f"Incomplete speech model: {directory}")
        models.append((name, files))
    offset = 4 + sum(36 + 40 * len(files) for _, files in models)
    header = bytearray(struct.pack("<I", len(models)))
    payload = bytearray()
    for name, files in models:
        header.extend(struct.pack("<32sI", name.encode(), len(files)))
        for name, data in files:
            padding = -(offset + len(payload)) % 16
            payload.extend(b"\0" * padding)
            header.extend(struct.pack("<32sII", name.encode(), offset + len(payload), len(data)))
            payload.extend(data)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(header + payload)
    print(f"Embedded voice models: {len(header) + len(payload)} bytes")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("component", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    pack(args.component, args.output)
