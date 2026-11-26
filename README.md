# Findex

Findex is a free, open-source, and fast tool for searching files on your computer.

It reads the NTFS Master File Table (`FSCTL_ENUM_USN_DATA`) to index every file
and folder in seconds, keeps the index up to date live with the USN change
journal, and saves it to `%LOCALAPPDATA%\Findex` so the next start is instant.

> **Note:** this latest version (the MFT indexer rewrite, the terminal interface
> and the background service) was made with the help of AI.

## Usage

```
findex                     interactive search (live as you type)
findex <words...>          print the best matches and exit

-d, --drive <letter>       volume to search (default C)
-n, --limit <n>            max results to print, 0 = all (default 30)
-f, --files / -F, --folders
-p, --paths                plain paths, one per line (for scripts)
    --local                index in this process instead of using the service
    --rescan               ignore the saved index and read the MFT again
```

Run `findex --help` for the full list.

In interactive mode: `↑↓` move, `Enter` open, `Ctrl+O` show in Explorer,
`Ctrl+Y` copy path, `Tab` switch all / files / folders, `Esc` clear or quit.

### Background service (no admin prompt)

Reading the MFT needs administrator rights. To avoid the UAC prompt every time,
install the service once:

```
findex --install-service        # or: findex --install-service C D
findex --status
findex --uninstall-service
```

It copies `findex.exe` to `C:\Program Files\Findex`, runs it as an auto-start
service that keeps the index of the chosen drives up to date, and answers
searches over a local named pipe that only the installing user and
administrators can open. The client checks that the pipe really belongs to the
service. Without the service (and without admin rights) interactive mode asks
for elevation and one-shot searches use the last saved index.

## Build requirements

**Windows**

> **Note: Findex is only available for windows**

> **Other compilers have not been tested. Windows API is required.**


- **Operating System:** 64-bit Windows
- **C++17 compiler (only tested with MSVC 19.50)**
- **Build Generator:** CMake ≥ 3.16
- **Build System:** MSBuild or Ninja
- No other dependencies (Qt is no longer needed)

## Building

1. Clone the repository:

```sh
git clone https://github.com/Espoachan/Findex.git
cd findex
```

2. Configure the project
```sh
cmake -S . -B build -G "Visual Studio 18 2026"
```

3. Build the project
```sh
cmake --build build --config Release
```

The executable is `build\Release\findex.exe` (or `build\findex.exe` with Ninja).

## Contributing

Pull requests are welcome. For major changes, please open an issue first
to discuss what you would like to change.
