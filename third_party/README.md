# Third-Party Dependencies

To build Sentinel AntiCheat, you need the following third-party libraries:

## Required

1. **SQLite3** — `third_party/sqlite3/`
   - Download `sqlite3.h` and `sqlite3.c` from https://www.sqlite.org/download.html
   - Place them in `third_party/sqlite3/`

2. **nlohmann/json** — `third_party/json/`
   - Download `json.hpp` from https://github.com/nlohmann/json/releases
   - Place in `third_party/json/`

3. **yaml-cpp** — `third_party/yaml-cpp/include/`
   - Download from https://github.com/jbeder/yaml-cpp/releases
   - Extract headers to `third_party/yaml-cpp/include/yaml-cpp/`

4. **cpp-httplib** — `third_party/httplib/`
   - Download `httplib.h` from https://github.com/yhirose/cpp-httplib
   - Place in `third_party/httplib/`

## Alternative: System Packages

```bash
# Ubuntu/Debian
sudo apt-get install libsqlite3-dev libyaml-cpp-dev

# macOS (Homebrew)
brew install sqlite3 yaml-cpp

# Windows (vcpkg)
vcpkg install sqlite3 yaml-cpp
```
