# Samsung Frame 2024 TV Video File Checker

This utility checks whether your video files are compatible with the Samsung Frame 2024 TV. It analyzes video, audio, and subtitle streams, as well as the container format, and suggests `ffmpeg` commands for remuxing or transcoding files that are not supported.

## Features

- **Checks video, audio, and subtitle codecs** for Samsung Frame 2024 TV compatibility.
- **Analyzes container format** support.
- **Brief or verbose output** modes.
- **Suggests `ffmpeg` commands** to fix unsupported files (remux or transcode).
- **Recursive directory scan** with directory exclusion support.
- **Color-coded output** for easy reading.
- **Summary statistics** at the end.

## Requirements

- GCC or compatible C compiler
- [FFmpeg development libraries](https://ffmpeg.org/download.html) (libavformat, libavcodec, libavutil)
- CMake 3.5+
- POSIX system (Linux, macOS, etc.)

## Build

### Using CMake

```sh
mkdir build
cd build
cmake ..
make
```

The resulting binary will be named `check_tv_compat`.

### Using GCC Directly

```sh
gcc -o check_tv_compat check_tv_compat.c $(pkg-config --cflags --libs libavformat libavcodec libavutil)
```

## Usage

```sh
./check_tv_compat <file-or-directory> [options]
```

### Options

- `--exclude <pattern>`   Exclude directories or files matching the pattern (can be used multiple times, uses `fnmatch`).
- `--fullpath`            Show full file paths in output.
- `--brief`               Print one-line summary per file (suitable for scripting).
- `--skip-ok`             Skip files that are already fully compatible.
- `--skip-unfixable`      Skip files that cannot be fixed by transcoding.
- `-h`, `--help`          Show usage.

### Examples

Check a single file:
```sh
./check_tv_compat my_movie.mkv
```

Check all files in a directory, skipping compatible files:
```sh
./check_tv_compat /media/videos --skip-ok
```

Check a directory, excluding a subdirectory:
```sh
./check_tv_compat /media/videos --exclude "*/Trailers/*"
```

Brief output for scripting:
```sh
./check_tv_compat /media/videos --brief
```

## Output

- **Verbose mode** (default): Shows details for each stream, container, and suggested `ffmpeg` commands for fixing unsupported files.
- **Brief mode**: One line per file, color-coded, showing which streams are supported or not.

## How It Works

- Uses FFmpeg libraries to probe each file.
- Compares codecs and container to a list of formats supported by Samsung Frame 2024 TV.
- For unsupported files, suggests either a remux (container change) or a transcode (codec change) using `ffmpeg`.
- Handles text and bitmap subtitles appropriately.

## Limitations

- The list of supported codecs and containers is based on public Samsung documentation and may not be exhaustive.
- Only works on POSIX systems (Linux, macOS).
- Does not handle every possible subtitle format or exotic codec.
- Requires FFmpeg libraries to be installed.

## License

MIT License

---

## Credits

- Co-authored and improved with ChatGPT (OpenAI)
- XviD/MPEG-4 ASP detection improvements by Claude (Anthropic) - October 2025

---

**Contributions and pull requests are welcome!**

If you have any questions or suggestions, please open an issue or PR.

---

**Enjoy your perfectly compatible video library!**
