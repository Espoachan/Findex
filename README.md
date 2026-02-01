# Findex

Findex is a free, open-source, and fast tool for searching files on your computer.
## Build requirements

**Windows**

> **Note: Findex is only available for windows**

> **Other compilers have not been tested. Windows API is required.**


- **Operating System:** 64-bit Windows
- **C++11 compatible compiler**
- **Tested with:**
**MSVC 19.50**
- **Build Generator:** CMake ≥ 3.12
- **Build System:** MSBuild or Ninja

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

4. Build the project
```sh
cmake --build build --config Release
```

## Contributing

Pull requests are welcome. For major changes, please open an issue first
to discuss what you would like to change.

Please make sure to update tests as appropriate.
