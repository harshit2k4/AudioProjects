# Simple Audio Player CLI

A command-line application written in C to decode and play audio files using the **libmad** decoder (for MP3) and the **libFLAC** decoder (for FLAC), utilizing the **ALSA** (Advanced Linux Sound Architecture) library for audio output.

## Features

* **Dual Codec Support:** Separate optimized players for MP3 and FLAC files.
* **16-bit PCM Output:** Decodes audio data into 16-bit, signed, little-endian PCM format.
* **Dynamic ALSA Configuration:** Automatically configures the ALSA device (sample rate, channels) based on file metadata.
* **Interactive Controls (FLAC Player Only):** Supports **Pause** (`p`), **Resume** (`r`), **Stop** (`s`), and **Quit** (`q`) functionality via console input.

---

## Requirements & Dependencies

This project requires a C compiler (`gcc`) and the development header files for **libmad**, **libFLAC**, and **ALSA**.
As of now, compilation and other support is available solely on Linux Environment. You can use others but it will be self-service way

### 1. Install Dependencies

You must install the necessary development packages for your Linux distribution before compiling.

#### Debian/Ubuntu-based Systems (using apt)

```bash
sudo apt update
sudo apt install gcc libmad0-dev libasound2-dev libflac-dev
```

#### RHEL/Fedora-based Systems (using dnf)
```Bash
sudo dnf update
sudo dnf install gcc libmad-devel alsa-lib-devel flac-devel
```

#### Arch-based Systems (using pacman)
```Bash
sudo pacman -Syu
sudo pacman -S gcc libmad alsa-lib flac
```

## MP3 Player Compilation and Usage

## Compilation & Usage

You have two distinct C source files that result in two different players. Choose the appropriate player based on the audio file format.

### 1. MP3 Player (`mp3player.c`) - Basic Playback

This player is built specifically for MP3 files using **libmad**. It provides basic, non-interactive playback (it plays the file from start to finish).

| Action | Command | Linking Flags |
| :--- | :--- | :--- |
| **Source File** | mp3player.c | |
| **Compile** | gcc -o mp3player mp3player.c -lmad -lasound | -lmad -lasound |
| **Usage** | ./mp3player <file>.mp3 | |

Example Usage:
```bash
./mp3player ~/Music/my_podcast.mp3
```

## FLAC Player Compilation and Usage

### 2. FLAC Player (`flacplayer.c`) - Advanced Controls

This player is built specifically for FLAC files using **libFLAC**. It includes the added features for interactive control (pause, stop, resume).

| Action | Command | Linking Flags |
| :--- | :--- | :--- |
| **Source File** | flacplayer.c | |
| **Compile** | gcc -o flacplayer flacplayer.c -lFLAC -lasound -pthread | -lFLAC -lasound -pthread |
| **Usage** | ./flacplayer <file>.flac | |

Example Usage:
```bash
./flacplayer /data/lossless/album_track.flac
```

## Why Two Players?

The two players use fundamentally different decoding libraries (**libmad** vs. **libFLAC**) and different program structures (basic blocking vs. multi-threaded control). Keeping them separate ensures each executable is optimized for its specific codec and feature set. The MP3 player is simpler and only requires `-lmad`, while the FLAC player adds multi-threading features and requires `-lFLAC -pthread`.
