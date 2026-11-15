# Raye Settings

The settings app of Raye Linux: a start page of categories, a navigation
pane on the left, the page on the right, and changes that apply as you
make them. It answers for the settings of the whole desktop.

- `src/` the app (Qt 6, QML, Kirigami, KCMUtils)
- `src/qml/` the pages
- `data/layout.json` the categories and what goes in each
- `portal/` the device portal used by Developer Mode
- `tests/` the tests

## Build

    cmake -S . -B build -G Ninja && cmake --build build

## Test

    ctest --test-dir build --output-on-failure

To build the package: `dpkg-buildpackage -b -us -uc`.

## License

GPL-3.0-or-later (`LICENSE`).

## Links

- Website: https://rayelinux.com
- Discord: https://discord.gg/TrxhUkabyB (questions, bug reports, news)
