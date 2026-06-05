from pathlib import Path
import json
import sys

if len(sys.argv) != 3:
    print("Usage: python create_jsons.py /path/to/folder START_OFFSET")
    sys.exit(1)

folder = Path(sys.argv[1])
start_offset = int(sys.argv[2])

wav_files = sorted(folder.glob("*.wav"))

for index, wav_file in enumerate(wav_files, start=start_offset):
    json_file = wav_file.with_suffix(".json")

    with open(json_file, "w", encoding="utf-8") as f:
        json.dump({"number": index}, f)

    print(f"Created {json_file} with number {index}")