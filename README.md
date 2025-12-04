# Audio Player CLI

A simple command-line application written in C to decode and play audio files using the **libmad** decoder and the **ALSA** (Advanced Linux Sound Architecture) library for audio output.

## Requirements

This project requires a C compiler (`gcc`) and the development header files for `libmad` and `ALSA`.

### 1. Install Dependencies

You must install the necessary development packages for your Linux distribution before compiling.

#### Debian/Ubuntu-based Systems (using `apt`)

```bash
sudo apt update
sudo apt install gcc libmad0-dev libasound2-dev
```
#### RHEL/Fedora-based Systems (using dnf)

```bash
sudo dnf update
sudo dnf install gcc libmad-devel alsa-lib-devel
```
#### Arch-based Systems (using pacman)
```bash
sudo pacman -Syu
sudo pacman -S gcc libmad alsa-lib
```

### 2. Compilation

Save the C code as `mp3player.c`. Compile the file using the following command, ensuring the linking flags (`-lmad` and `-lasound`) are included:

```bash
gcc -o mp3player mp3player.c -lmad -lasound
```
This command will create an executable file named mp3player.

### Usage
Run the compiled executable by providing the path to an MP3 file as an argument:

```bash
./mp3player /path/to/your/audio.mp3
```
Example:
```bash
./mp3player ~/Music/my_podcast.mp3
```
